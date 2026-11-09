// Copyright 2026, haiku-odroid-c2 port authors.
// Distributed under the terms of the MIT License.

#ifndef QHAIKUAUDIODEVICES_P_H
#define QHAIKUAUDIODEVICES_P_H

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

#include <QtMultimedia/private/qplatformaudiodevices_p.h>

QT_BEGIN_NAMESPACE

// Haiku's audio output: one device, the media server's system mixer, which
// plays through whatever output Media preferences selected (PLAN.md M83 of
// the haiku-odroid-c2 port). No input yet.
class QHaikuAudioDevices final : public QPlatformAudioDevices
{
public:
    QPlatformAudioSink *createAudioSink(const QAudioDevice &, const QAudioFormat &,
                                        QObject *parent) override;
    QLatin1String backendName() const override { return QLatin1String{ "Haiku" }; }
    bool hasCallbackApi() const override { return true; }

protected:
    QList<QAudioDevice> findAudioOutputs() const override;
};

QT_END_NAMESPACE

#endif // QHAIKUAUDIODEVICES_P_H
