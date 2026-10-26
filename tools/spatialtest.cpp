#include "audio/engine.h"
#include "test_support/support.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <atomic>
#include <cmath>

namespace {
using echo::test::check;
using echo::test::waitFor;
}
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    // The old 80 ms queue advanced its clock indefinitely without ever meeting Apple's preroll.
    // Assert readiness and sustained delivery of nonzero PCM, not just a moving timeline.
    auto renderer = echo::createSpatialAudioOutput();
    echo::SampleFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    format.bitsPerSample = 32;
    format.bytesPerSample = 4;
    format.isFloat = true;
    format.pcm = Fooyin::SampleFormat::F32;
    echo::DeviceFormat device;
    QString rendererError;
    check(renderer->open(format, false, &device, &rendererError), "open native buffering regression output");
    renderer->setVolume(.25f);
    check(std::abs(renderer->diagnostics().volume - .25f) < .001, "native volume reaches the renderer directly");
    renderer->setVolume(0); // Keep automated tests inaudible; PCM itself remains nonzero.
    std::atomic<qint64> supplied {0};
    renderer->setPull([&](uint8_t* bytes, int frames) {
        auto* samples = reinterpret_cast<float*>(bytes);
        const qint64 start = supplied.fetch_add(frames);
        for (int frame = 0; frame < frames; ++frame) {
            const float value = .1f * std::sin(2 * 3.141592653589793 * 440 * (start + frame) / 48000.);
            samples[frame * 2] = samples[frame * 2 + 1] = value;
        }
        return frames;
    });
    renderer->start();
    check(waitFor([&] { const auto d = renderer->diagnostics(); return d.prerollReached && d.sourcePeak > .09 && d.maximumBufferedFrames >= 24000; }),
          "native output reaches system preroll with audible PCM content");
    const qint64 initialFrames = renderer->diagnostics().submittedFrames;
    check(waitFor([&] { return renderer->diagnostics().submittedFrames > initialFrames + 96000; }),
          "nonzero PCM continues for more than two seconds after initial preroll");
    renderer->pause();
    const qint64 pausedFrames = renderer->diagnostics().submittedFrames;
    QThread::msleep(100);
    check(renderer->diagnostics().submittedFrames == pausedFrames, "native pause immediately stops further delivery");
    renderer->flush();
    supplied = 0;
    renderer->setPull([&](uint8_t* bytes, int frames) {
        const int count = std::min<qint64>(frames, std::max<qint64>(0, 2400 - supplied.load()));
        auto* samples = reinterpret_cast<float*>(bytes);
        for (int index = 0; index < count * 2; ++index) samples[index] = .01f;
        supplied += count;
        return count;
    });
    renderer->start();
    check(waitFor([&] { return supplied == 2400; }), "queue a track shorter than system preroll");
    renderer->finishInput();
    check(waitFor([&] { return renderer->pendingFrames() == 0; }), "confirmed short-track EOF bypasses preroll and drains");
    renderer->close();
    QTemporaryDir temp;
    QProcess ffmpeg;
    const QString path = temp.filePath(QStringLiteral("stereo.wav"));
    ffmpeg.start(QStringLiteral("ffmpeg"), {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=8:sample_rate=44100"),
        QStringLiteral("-ac"), QStringLiteral("2"), path});
    check(ffmpeg.waitForFinished(10000) && ffmpeg.exitCode() == 0, "create native spatial test fixture");
    echo::AudioEngine engine;
    QString status;
    QString failure;
    QObject::connect(&engine, &echo::AudioEngine::spatialStatusChanged, &application, [&](const QString& text) { status = text; qInfo() << text; });
    QObject::connect(&engine, &echo::AudioEngine::errorOccurred, &application, [&](const QString& text) { failure = text; qWarning() << text; });
    // Exercise the real output route without playing a test tone into the person's headphones.
    engine.setVolume(0);
    engine.setSpatialEnabled(true);
    echo::PlaybackItem first;
    first.path = path;
    first.token = 1;
    first.endMs = 2000;
    echo::PlaybackItem second = first;
    second.token = 2;
    second.startMs = 2000;
    second.endMs = 8000;
    engine.play(first, second);
    check(waitFor([&] { return status.contains(QStringLiteral("已接入")) && engine.positionMs() >= 400; }), "AVFoundation renderer advances its real audio clock");
    engine.pause();
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Paused; }), "spatial pause completes");
    const qint64 paused = engine.positionMs();
    QThread::msleep(200);
    check(engine.positionMs() == paused, "paused spatial clock is stable");
    engine.seekMs(850);
    check(waitFor([&] { return engine.positionMs() >= 840 && engine.positionMs() <= 860; }), "paused seek flushes queued spatial audio");
    engine.setSpatialEnabled(false);
    check(waitFor([&] { return status.contains(QStringLiteral("已关闭")); }), "switch from spatial output");
    check(engine.state() == echo::AudioEngine::State::Paused && engine.positionMs() >= 840, "switch output preserves paused position");
    engine.setSpatialEnabled(true);
    check(waitFor([&] { return status.contains(QStringLiteral("已接入")); }), "switch back to spatial output");
    engine.resume();
    check(waitFor([&] { return engine.positionMs() > 1250; }), "spatial resume continues after seek");
    const qint64 beforeSwitch = engine.positionMs();
    engine.setSpatialEnabled(false);
    check(waitFor([&] { return status.contains(QStringLiteral("已关闭")); }), "switch output while playing");
    check(engine.state() == echo::AudioEngine::State::Playing && engine.positionMs() >= beforeSwitch - 100, "switch output preserves playing timeline");
    engine.setSpatialEnabled(true);
    check(waitFor([&] { return status.contains(QStringLiteral("已接入")); }), "return to spatial while playing");
    check(waitFor([&] { return engine.activeToken() == 2 && engine.positionMs() > 2200; }), "spatial gapless track transition follows audible timeline");
    engine.stop();
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Stopped; }), "spatial stop drains and flushes");
    check(failure.isEmpty(), "native spatial output did not fail");
    engine.play(first, {});
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Playing && engine.positionMs() > 250 && engine.positionMs() < 1000; }), "restart uses a fresh spatial timeline");
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Stopped; }), "spatial EOF drains all queued audio");
    auto fallback = echo::createMacAudioOutput();
    fallback->setSpatialEnabled(true);
    echo::SampleFormat unknownLayout;
    unknownLayout.sampleRate = 48000;
    unknownLayout.channels = 3;
    unknownLayout.bitsPerSample = 32;
    unknownLayout.bytesPerSample = 4;
    unknownLayout.isFloat = true;
    echo::DeviceFormat opened;
    QString fallbackError;
    check(fallback->open(unknownLayout, false, &opened, &fallbackError), "unknown spatial layout falls back to CoreAudio");
    check(fallback->spatialStatus().contains(QStringLiteral("回退")), "fallback is reported instead of claiming spatial rendering");
    fallback->close();
    qInfo() << "Native spatial render, pause, seek, route switching, gapless, restart, EOF and fallback checks passed";
}
