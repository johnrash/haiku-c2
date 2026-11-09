// Copyright 2026, haiku-odroid-c2 port authors.
// Distributed under the terms of the MIT License.

#include "qhaikuaudiodevices_p.h"
#include "qhaikuaudiosink_p.h"

#include <QtMultimedia/private/qaudiodevice_p.h>

QT_BEGIN_NAMESPACE

QList<QAudioDevice> QHaikuAudioDevices::findAudioOutputs() const
{
    // BSoundPlayer takes these formats and rates; the system mixer converts
    // to what the output plays
    QAudioDevicePrivate::AudioDeviceFormat format;
    format.minimumChannelCount = 1;
    format.maximumChannelCount = 2;
    format.minimumSampleRate = 8000;
    format.maximumSampleRate = 192000;
    format.supportedSampleFormats = qAllSupportedSampleFormats();
    format.preferredFormat.setChannelCount(2);
    format.preferredFormat.setSampleFormat(QAudioFormat::Float);
    format.preferredFormat.setSampleRate(48000);

    auto device = std::make_unique<QAudioDevicePrivate>(
            QByteArrayLiteral("haiku-system-mixer"), QAudioDevice::Output,
            QStringLiteral("System mixer"), true, format);
    return { QAudioDevicePrivate::createQAudioDevice(std::move(device)) };
}

QPlatformAudioSink *QHaikuAudioDevices::createAudioSink(const QAudioDevice &device,
                                                        const QAudioFormat &format,
                                                        QObject *parent)
{
    return new QHaikuAudioSink(device, format, parent);
}

QT_END_NAMESPACE
