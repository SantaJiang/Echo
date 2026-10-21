#include "audio/engine.h"
#include "audio/convert.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QDebug>
#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>

namespace {
struct Capture {
    std::mutex mutex;
    std::vector<float> samples;
    std::atomic<int> opens {0};
    std::atomic<int> starts {0};
};
class CaptureOutput final : public echo::AudioOutput {
public:
    explicit CaptureOutput(Capture* capture, int forcedRate = 0, int bits = 0, int bytes = 0, bool aligned = false, int outputChannels = 0)
        : capture(capture), forcedRate(forcedRate), bits(bits), bytes(bytes), aligned(aligned), outputChannels(outputChannels) {}
    ~CaptureOutput() override { close(); }
    bool open(const echo::SampleFormat& source, bool, echo::DeviceFormat* opened, QString*) override
    {
        channels = outputChannels ? outputChannels : source.channels;
        *opened = {forcedRate ? forcedRate : source.sampleRate, channels, bits ? bits : 32, bytes ? bytes : 4, !bits, aligned};
        rate = opened->sampleRate;
        isOpened = true;
        ++capture->opens;
        return true;
    }
    void close() override { pause(); isOpened = false; }
    void setPull(Pull value) override { pull = std::move(value); }
    void start() override
    {
        pause();
        running = true;
        ++capture->starts;
        thread = std::thread([this] {
            const int frames = rate / 200;
            std::vector<float> block(frames * channels);
            std::vector<uint8_t> raw(frames * channels * (bytes ? bytes : 4));
            echo::SampleFormat input;
            input.sampleRate = rate; input.channels = channels; input.bitsPerSample = bits ? bits : 32;
            input.bytesPerSample = bytes ? bytes : 4; input.isFloat = !bits; input.highAligned = aligned;
            echo::DeviceFormat floating {rate, channels, 32, 4, true};
            while (running) {
                const int got = pull(raw.data(), frames);
                echo::convertSamples(raw.data(), input, got, reinterpret_cast<uint8_t*>(block.data()), floating, 1, nullptr);
                { std::lock_guard lock(capture->mutex); capture->samples.insert(capture->samples.end(), block.begin(), block.begin() + got * channels); }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }
    void pause() override { running = false; if (thread.joinable()) thread.join(); }
    bool isOpen() const override { return isOpened; }
private:
    Capture* capture;
    Pull pull;
    int forcedRate = 0;
    int bits = 0, bytes = 0;
    bool aligned = false;
    int outputChannels = 0;
    int channels = 0;
    int rate = 0;
    bool isOpened = false;
    std::atomic<bool> running {false};
    std::thread thread;
};
void check(bool ok, const char* text) { if (!ok) qFatal("%s", text); }
bool waitFor(const std::function<bool()>& condition, int timeout = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(5);
    }
    return false;
}
double tailRms(Capture& capture)
{
    std::lock_guard lock(capture.mutex);
    double sum = 0;
    const int count = std::min<int>(capture.samples.size(), 4800);
    for (auto it = capture.samples.end() - count; it != capture.samples.end(); ++it) sum += *it * *it;
    return count ? std::sqrt(sum / count) : 0;
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("tone.wav"));
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"), {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("sine=frequency=1000:duration=12:sample_rate=48000"), QStringLiteral("-c:a"), QStringLiteral("pcm_f32le"), path});
    check(ffmpeg.waitForFinished(10000) && ffmpeg.exitCode() == 0, "generate audio fixture");
    Capture capture;
    echo::AudioEngine engine(std::make_unique<CaptureOutput>(&capture));
    echo::PlaybackItem item;
    item.path = path;
    item.token = 1;
    engine.play(item, {});
    check(waitFor([&] { return tailRms(capture) > .08; }), "initial PCM reaches output");
    const auto position = engine.positionMs();
    const int opens = capture.opens;
    const int starts = capture.starts;
    engine.setTrackGain(1, -12.f);
    check(waitFor([&] { const auto rms = tailRms(capture); return rms > .021 && rms < .023; }), "live gain reaches actual PCM output");
    check(engine.state() == echo::AudioEngine::State::Playing && engine.positionMs() > position,
          "live gain preserves playback and position");
    check(capture.opens == opens && capture.starts == starts, "live gain does not restart device");
    const qint64 beforeModeChange = engine.positionMs();
    engine.setSpatialEnabled(true);
    check(waitFor([&] { return capture.opens > opens && engine.positionMs() > beforeModeChange + 250; }), "output mode reopens at the audible position");
    check(tailRms(capture) > .021 && tailRms(capture) < .023, "output reconfiguration preserves the latest normalization gain");

    engine.pause();
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Paused; }), "pause completes");
    const auto paused = engine.positionMs();
    engine.setTrackGain(1, 0.f);
    QThread::msleep(50);
    check(engine.state() == echo::AudioEngine::State::Paused && engine.positionMs() == paused, "gain change preserves pause");
    engine.resume();
    check(waitFor([&] { return tailRms(capture) > .08; }), "disabling gain restores output volume");
    engine.stop();
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Stopped; }), "stop completes");
    const int opensBeforeStoppedSwitch = capture.opens;
    engine.setSpatialEnabled(false);
    engine.play(item, {});
    check(waitFor([&] { return engine.positionMs() > 250; }), "play after changing mode while stopped");
    check(capture.opens > opensBeforeStoppedSwitch, "stopped output mode change invalidates the cached floating PCM device");
    engine.stop();
    check(waitFor([&] { return engine.state() == echo::AudioEngine::State::Stopped; }), "stop after mode switch");

    // A quiet-to-loud transition must start with the next track's gain, including contiguous CUE ranges.
    Capture gapless;
    echo::AudioEngine gaplessEngine(std::make_unique<CaptureOutput>(&gapless));
    item.endMs = 1000;
    item.gainDb = 12;
    echo::PlaybackItem next = item;
    next.token = 2;
    next.startMs = 1000;
    next.endMs = 2000;
    next.gainDb = -12;
    gaplessEngine.play(item, next);
    check(waitFor([&] { std::lock_guard lock(gapless.mutex); return gapless.samples.size() >= 96000; }, 5000), "gapless ranges reach output");
    {
        std::lock_guard lock(gapless.mutex);
        double peak = 0;
        for (int i = 48000; i < 96000; ++i) peak = std::max(peak, std::abs(static_cast<double>(gapless.samples[i])));
        check(peak < .033, "next track starts at its own gain without a loud transient");
    }
    check(gapless.opens == 1 && gapless.starts == 1, "normalization preserves gapless device continuity");
    Capture resampled;
    echo::AudioEngine resampleEngine(std::make_unique<CaptureOutput>(&resampled, 44100));
    item.endMs = -1;
    item.gainDb = 0;
    resampleEngine.play(item, {});
    check(waitFor([&] { return tailRms(resampled) > .08; }), "resampled PCM reaches output");
    resampleEngine.setTrackGain(1, -12);
    check(waitFor([&] { const auto rms = tailRms(resampled); return rms > .021 && rms < .023; }), "live gain works through resampling");
    for (const auto format : {echo::DeviceFormat{48000, 1, 24, 3, false},
             echo::DeviceFormat{48000, 1, 24, 4, false}, echo::DeviceFormat{48000, 1, 24, 4, false, true},
             echo::DeviceFormat{48000, 1, 16, 2, false}, echo::DeviceFormat{48000, 1, 32, 4, false}}) {
        Capture integer;
        echo::AudioEngine integerEngine(std::make_unique<CaptureOutput>(&integer, 0, format.bitsPerSample,
            format.containerBytes, format.highAligned));
        integerEngine.play(item, {});
        check(waitFor([&] { return tailRms(integer) > .08; }), "integer PCM starts at correct level");
        integerEngine.setVolume(0);
        check(waitFor([&] { return tailRms(integer) < .000001; }), "all integer container formats mute");
        integerEngine.setVolume(1);
        check(waitFor([&] { return tailRms(integer) > .08; }), "integer volume restores");
        integerEngine.pause();
        check(waitFor([&] { return integerEngine.state() == echo::AudioEngine::State::Paused; }), "integer pause fade completes");
        check(tailRms(integer) < .06, "integer pause actually fades the PCM");
        integerEngine.seekMs(1200);
        integerEngine.resume();
        check(waitFor([&] { return integerEngine.positionMs() > 1300 && tailRms(integer) > .08; }), "integer seek and resume restore PCM");
    }
    const QString multichannel = directory.filePath("center.wav");
    ffmpeg.start(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-f", "lavfi", "-i",
        "aevalsrc=0|0|0.4*sin(2*PI*440*t)|0|0|0:s=48000:d=3:c=5.1", "-c:a", "pcm_f32le", multichannel});
    check(ffmpeg.waitForFinished(10000) && ffmpeg.exitCode() == 0, "generate multichannel fixture");
    Capture centerCapture;
    echo::AudioEngine centerEngine(std::make_unique<CaptureOutput>(&centerCapture, 48000, 0, 0, false, 2));
    echo::PlaybackItem centerItem; centerItem.path = multichannel; centerItem.token = 90;
    centerEngine.play(centerItem, {});
    check(waitFor([&] { return tailRms(centerCapture) > .05; }), "same-rate engine downmix retains center content");
    // A contiguous CUE boundary must not drain/restart the resampling filter mid-stream.
    Capture fullResampled, splitResampled;
    echo::AudioEngine fullResampleEngine(std::make_unique<CaptureOutput>(&fullResampled, 44100));
    echo::AudioEngine splitResampleEngine(std::make_unique<CaptureOutput>(&splitResampled, 44100));
    echo::PlaybackItem whole; whole.path = path; whole.token = 300; whole.endMs = 2000;
    echo::PlaybackItem firstRange = whole; firstRange.endMs = 1000;
    echo::PlaybackItem secondRange = whole; secondRange.token = 301; secondRange.startMs = 1000;
    fullResampleEngine.play(whole, {});
    splitResampleEngine.play(firstRange, secondRange);
    check(waitFor([&] { return fullResampleEngine.state() == echo::AudioEngine::State::Playing
        && splitResampleEngine.state() == echo::AudioEngine::State::Playing; }), "resampled CUE comparison starts");
    check(waitFor([&] { return fullResampleEngine.state() == echo::AudioEngine::State::Stopped
        && splitResampleEngine.state() == echo::AudioEngine::State::Stopped; }, 5000), "resampled CUE comparison drains");
    {
        std::scoped_lock lock(fullResampled.mutex, splitResampled.mutex);
        check(fullResampled.samples.size() == splitResampled.samples.size(), "resampled CUE keeps total sample count");
        double maximumError = 0;
        for (size_t i = 0; i < fullResampled.samples.size(); ++i)
            maximumError = std::max(maximumError, double(std::abs(fullResampled.samples[i] - splitResampled.samples[i])));
        check(maximumError < .000001, "resampled CUE seam equals continuous decode PCM");
    }
    qInfo() << "Live normalization PCM, position, pause, gapless and resampling checks passed";
}
