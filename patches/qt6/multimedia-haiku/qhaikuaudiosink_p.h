// Copyright 2026, haiku-odroid-c2 port authors.
// Distributed under the terms of the MIT License.

#ifndef QHAIKUAUDIOSINK_P_H
#define QHAIKUAUDIOSINK_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtMultimedia/private/qaudio_platform_implementation_support_p.h>
#include <QtMultimedia/private/qaudiosystem_p.h>
#include <QtMultimedia/private/qaudiosystem_platform_stream_support_p.h>

#include <atomic>
#include <memory>
#include <optional>

class BSoundPlayer;
struct media_raw_audio_format;

QT_BEGIN_NAMESPACE

class QHaikuAudioSink;

// A stream plays through a BSoundPlayer, whose real-time thread asks for
// each buffer: from the ring buffer (QIODevice push or pull), or from the
// application's callback.
struct QHaikuAudioSinkStream final
    : std::enable_shared_from_this<QHaikuAudioSinkStream>,
      QtMultimediaPrivate::QPlatformAudioSinkStream
{
    using SinkType = QHaikuAudioSink;
    using AudioCallback = QtMultimediaPrivate::QPlatformAudioSinkStream::AudioCallback;
    using ShutdownPolicy = QtMultimediaPrivate::QPlatformAudioIOStream::ShutdownPolicy;
    using AudioEndpointRole = QtMultimediaPrivate::AudioEndpointRole;
    using NativePeriodFrames = QtMultimediaPrivate::NativePeriodFrames;

    QHaikuAudioSinkStream(QAudioDevice, const QAudioFormat &,
                          std::optional<qsizetype> ringbufferSize, QHaikuAudioSink *parent,
                          float volume, std::optional<NativePeriodFrames> nativePeriodFrames,
                          AudioEndpointRole);
    Q_DISABLE_COPY_MOVE(QHaikuAudioSinkStream)
    ~QHaikuAudioSinkStream();

    bool open();

    using QtMultimediaPrivate::QPlatformAudioSinkStream::bytesFree;
    using QtMultimediaPrivate::QPlatformAudioSinkStream::processedDuration;
    using QtMultimediaPrivate::QPlatformAudioSinkStream::ringbufferSizeInBytes;
    using QtMultimediaPrivate::QPlatformAudioSinkStream::setVolume;

    bool start(QIODevice *);
    QIODevice *start();
    bool start(AudioCallback);
    void suspend();
    void resume();
    void stop(ShutdownPolicy);

    void updateStreamIdle(bool) override;

private:
    static void playBuffer(void *cookie, void *buffer, size_t size,
                           const media_raw_audio_format &);
    void fillBuffer(QSpan<std::byte>) noexcept Q_DECL_NONBLOCKING_FUNCTION;
    bool startPlayer();
    void stopPlayer();

    std::unique_ptr<BSoundPlayer> m_player;
    std::optional<AudioCallback> m_audioCallback;
    std::atomic_bool m_suspended = false;
    std::atomic<ShutdownPolicy> m_shutdownPolicy = ShutdownPolicy::DiscardRingbuffer;
    QtPrivate::QAutoResetEvent m_ringbufferDrained;
    std::atomic_bool m_drainedSignalled = false;
    std::atomic<QHaikuAudioSink *> m_parent{ nullptr };
};

class QHaikuAudioSink final
    : public QtMultimediaPrivate::QPlatformAudioSinkImplementation<QHaikuAudioSinkStream,
                                                                   QHaikuAudioSink>
{
    using BaseClass = QtMultimediaPrivate::QPlatformAudioSinkImplementation<QHaikuAudioSinkStream,
                                                                            QHaikuAudioSink>;

public:
    QHaikuAudioSink(QAudioDevice, const QAudioFormat &, QObject *parent);
};

QT_END_NAMESPACE

#endif // QHAIKUAUDIOSINK_P_H
