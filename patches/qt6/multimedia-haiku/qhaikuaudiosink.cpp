// Copyright 2026, haiku-odroid-c2 port authors.
// Distributed under the terms of the MIT License.

#include "qhaikuaudiosink_p.h"

#include <QtCore/qcoreapplication.h>
#include <QtCore/qdebug.h>
#include <QtCore/qloggingcategory.h>
#include <QtMultimedia/private/qaudiohelpers_p.h>

#include <MediaDefs.h>
#include <SoundPlayer.h>

#include <cstring>

QT_BEGIN_NAMESPACE

Q_STATIC_LOGGING_CATEGORY(lcHaikuAudioSink, "qt.multimedia.haiku.audiosink");

using namespace QtMultimediaPrivate;

namespace {

uint32 haikuSampleFormat(QAudioFormat::SampleFormat format)
{
    switch (format) {
    case QAudioFormat::UInt8:
        return media_raw_audio_format::B_AUDIO_UCHAR;
    case QAudioFormat::Int16:
        return media_raw_audio_format::B_AUDIO_SHORT;
    case QAudioFormat::Int32:
        return media_raw_audio_format::B_AUDIO_INT;
    case QAudioFormat::Float:
    default:
        return media_raw_audio_format::B_AUDIO_FLOAT;
    }
}

// frames per BSoundPlayer buffer unless the application asked for a period
constexpr uint32_t defaultPeriodFrames = 1024;

} // namespace

QHaikuAudioSinkStream::QHaikuAudioSinkStream(QAudioDevice device, const QAudioFormat &format,
                                             std::optional<qsizetype> ringbufferSize,
                                             QHaikuAudioSink *parent, float volume,
                                             std::optional<NativePeriodFrames> nativePeriodFrames,
                                             AudioEndpointRole /*role*/)
    : QPlatformAudioSinkStream{
          std::move(device),
          format,
          ringbufferSize,
          nativePeriodFrames,
          volume,
      },
      m_parent{ parent }
{
}

QHaikuAudioSinkStream::~QHaikuAudioSinkStream()
{
    stopPlayer();
}

bool QHaikuAudioSinkStream::open()
{
    // The player takes the application's format; the system mixer converts
    media_raw_audio_format format = media_raw_audio_format::wildcard;
    format.frame_rate = float(m_format.sampleRate());
    format.channel_count = uint32(m_format.channelCount());
    format.format = haikuSampleFormat(m_format.sampleFormat());
    format.byte_order = B_MEDIA_HOST_ENDIAN;
    const uint32_t periodFrames = m_nativePeriodFrames ? uint32_t(*m_nativePeriodFrames)
                                                       : defaultPeriodFrames;
    format.buffer_size = size_t(m_format.bytesForFrames(periodFrames));

    QByteArray name = QCoreApplication::applicationName().toUtf8();
    if (name.isEmpty())
        name = QByteArrayLiteral("Qt audio");
    m_player = std::make_unique<BSoundPlayer>(&format, name.constData(), &playBuffer, nullptr,
                                              this);
    if (m_player->InitCheck() != B_OK) {
        qCWarning(lcHaikuAudioSink) << "BSoundPlayer failed:"
                                    << strerror(m_player->InitCheck());
        m_player.reset();
        return false;
    }
    return true;
}

bool QHaikuAudioSinkStream::startPlayer()
{
    if (!m_player || m_player->Start() != B_OK) {
        requestStop();
        return false;
    }
    m_player->SetHasData(true);
    return true;
}

void QHaikuAudioSinkStream::stopPlayer()
{
    // returns once the player thread is done: no callback after this
    if (m_player) {
        m_player->Stop(true, true);
        m_player.reset();
    }
}

bool QHaikuAudioSinkStream::start(QIODevice *device)
{
    setQIODevice(device);
    createQIODeviceConnections(device);
    pullFromQIODevice();
    return startPlayer();
}

QIODevice *QHaikuAudioSinkStream::start()
{
    QIODevice *device = createRingbufferWriterDevice();
    setQIODevice(device);
    createQIODeviceConnections(device);
    setIdleState(true);
    if (!startPlayer())
        return nullptr;
    return device;
}

bool QHaikuAudioSinkStream::start(AudioCallback audioCallback)
{
    m_audioCallback = std::move(audioCallback);
    return startPlayer();
}

void QHaikuAudioSinkStream::suspend()
{
    m_suspended.store(true, std::memory_order_release);
}

void QHaikuAudioSinkStream::resume()
{
    m_suspended.store(false, std::memory_order_release);
}

void QHaikuAudioSinkStream::stop(ShutdownPolicy shutdownPolicy)
{
    if (m_suspended.load(std::memory_order_acquire) || m_audioCallback)
        shutdownPolicy = ShutdownPolicy::DiscardRingbuffer;
    m_shutdownPolicy.store(shutdownPolicy, std::memory_order_release);
    requestStop();
    disconnectQIODeviceConnections();
    m_parent.store(nullptr, std::memory_order_release);

    switch (shutdownPolicy) {
    case ShutdownPolicy::DiscardRingbuffer:
        stopPlayer();
        return;
    case ShutdownPolicy::DrainRingbuffer:
        // the player goes on until the ring buffer is empty; the stream
        // lives until then
        m_ringbufferDrained.callOnActivated([self = shared_from_this()]() mutable {
            self->stopPlayer();
            self = {};
        });
        return;
    }
}

void QHaikuAudioSinkStream::updateStreamIdle(bool idle)
{
    if (QHaikuAudioSink *parent = m_parent.load(std::memory_order_acquire))
        parent->updateStreamIdle(idle);
}

void QHaikuAudioSinkStream::playBuffer(void *cookie, void *buffer, size_t size,
                                       const media_raw_audio_format &)
{
    static_cast<QHaikuAudioSinkStream *>(cookie)->fillBuffer(
            QSpan<std::byte>{ static_cast<std::byte *>(buffer), qsizetype(size) });
}

void QHaikuAudioSinkStream::fillBuffer(QSpan<std::byte> hostBuffer) noexcept
        Q_DECL_NONBLOCKING_FUNCTION
{
    if (isStopRequested(std::memory_order_acquire)
        && m_shutdownPolicy.load(std::memory_order_acquire) == ShutdownPolicy::DiscardRingbuffer) {
        QAudioHelperInternal::fillSilence(hostBuffer, m_format);
        return;
    }
    if (m_suspended.load(std::memory_order_acquire)) {
        QAudioHelperInternal::fillSilence(hostBuffer, m_format);
        return;
    }
    if (m_audioCallback) {
        runAudioCallback(*m_audioCallback, hostBuffer, m_format, volume());
        return;
    }

    const qsizetype frames = m_format.framesForBytes(hostBuffer.size());
    // process() fills what the ring buffer can't with silence
    const uint64_t framesWritten = QPlatformAudioSinkStream::process(hostBuffer, frames);
    if (isStopRequested(std::memory_order_acquire) && framesWritten < uint64_t(frames)
        && !m_drainedSignalled.exchange(true))
        m_ringbufferDrained.set();
}

QHaikuAudioSink::QHaikuAudioSink(QAudioDevice device, const QAudioFormat &format,
                                 QObject *parent)
    : BaseClass(std::move(device), format, parent)
{
}

QT_END_NAMESPACE
