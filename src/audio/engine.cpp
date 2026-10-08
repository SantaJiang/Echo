#include "audio/engine.h"

#include <core/engine/fadingdefs.h>

#include "audio/convert.h"
#include "audio/decoder.h"
#include "audio/output.h"
#include "audio/ringbuffer.h"

#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <deque>
#include <utility>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace echo {
namespace {

struct Marker {
    qint64 queuedFrame = 0;
    qint64 token = 0;
    qint64 startMs = 0;
    int sampleRate = 0;
};

struct Loaded {
    PlaybackItem item;
    std::unique_ptr<Decoder> decoder;
    SampleFormat format;
    qint64 audibleFrames = 0;
};

qint64 itemFrames(const Decoder& decoder, const PlaybackItem& item)
{
    if (item.endMs >= 0) {
        const qint64 start = item.startMs * decoder.format().sampleRate / 1000;
        const qint64 end = item.endMs * decoder.format().sampleRate / 1000;
        return std::max<qint64>(0, end - start);
    }
    const qint64 duration = decoder.durationMs();
    if (duration <= 0) {
        return 0;
    }
    return duration * decoder.format().sampleRate / 1000;
}

} // namespace

struct AudioEngine::Worker {
    AudioEngine* owner = nullptr;
    std::unique_ptr<AudioOutput> output = createAudioOutput();
    RingBuffer ring;
    QMutex mutex;
    QWaitCondition wait;
    std::deque<int> commands;
    PlaybackItem pendingCurrent;
    PlaybackItem pendingUpcoming;
    qint64 pendingSeekMs = 0;
    std::atomic<float> volume {1.f};
    std::atomic<bool> exclusive {false};
    bool quit = false;
    bool wantExclusiveReopen = false;

    Loaded current;
    Loaded upcoming;
    DeviceFormat device;
    bool deviceReady = false;
    SwrContext* resampler = nullptr;
    int resampleRate = 0;
    uint32_t dither = 0xC0FFEEU;
    float activeGain = 1.f;
    qint64 framesQueued = 0;
    bool draining = false;
    enum class AfterTail { None, Switch, Stop, Reopen };
    AfterTail afterTail = AfterTail::None;
    std::vector<uint8_t> raw;
    std::vector<uint8_t> converted;
    std::vector<uint8_t> floatBlock;
    std::vector<uint8_t> sourceFloat;
    std::atomic<qint64> framesPlayed {0};
    std::vector<Marker> markers;
    QThread* thread = nullptr;
    enum class OutputFade { None, In, OutPause, OutStop, OutSeek };
    std::atomic<int> fadeKind {static_cast<int>(OutputFade::None)};
    std::atomic<int> fadeTotal {0};
    std::atomic<int> fadeDone {0};
    float smoothedVolume = 1.f;

    enum Command { Play = 1, Upcoming, Pause, Resume, Stop, Seek, Volume, Exclusive };

    void post(Command command)
    {
        QMutexLocker locker(&mutex);
        commands.push_back(command);
        wait.wakeAll();
    }

    void run()
    {
        while (true) {
            int command = 0;
            {
                QMutexLocker locker(&mutex);
                if (commands.empty() && !quit && owner->m_state.load() != static_cast<int>(State::Playing)) {
                    wait.wait(&mutex);
                }
                if (!commands.empty()) {
                    command = commands.front();
                    commands.pop_front();
                }
                if (quit && commands.empty()) {
                    break;
                }
            }
            if (command != 0) {
                handle(command);
                continue;
            }
            if (owner->m_state.load() != static_cast<int>(State::Playing)) {
                continue;
            }
            pump();
            publishPosition();
            if (fadeKind.load() != static_cast<int>(OutputFade::None) && fadeKind.load() != static_cast<int>(OutputFade::In)
                && ring.availableRead() == 0) {
                fadeDone.store(fadeTotal.load());
            }
            completeFade();
        }
        releaseDevice();
        current = {};
        upcoming = {};
    }

    void handle(int command)
    {
        switch (command) {
        case Play:
            startPlayback();
            break;
        case Upcoming:
            loadUpcoming();
            break;
        case Pause:
            if (!beginFade(OutputFade::OutPause, 120)) {
                if (output) {
                    output->pause();
                }
                setState(State::Paused);
            }
            break;
        case Resume:
            if (deviceReady && output) {
                beginFade(OutputFade::In, 120);
                output->start();
                setState(State::Playing);
            }
            break;
        case Stop:
            if (!beginFade(OutputFade::OutStop, 300)) {
                stopPlayback();
            }
            break;
        case Seek:
            if (!beginFade(OutputFade::OutSeek, 120)) {
                seekCurrent();
            }
            break;
        case Volume:
            break;
        case Exclusive:
            wantExclusiveReopen = true;
            break;
        default:
            break;
        }
    }

    void setState(State state)
    {
        owner->m_state.store(static_cast<int>(state));
        emit owner->stateChanged(static_cast<int>(state));
    }

    void startPlayback()
    {
        PlaybackItem item;
        PlaybackItem next;
        {
            QMutexLocker locker(&mutex);
            item = pendingCurrent;
            next = pendingUpcoming;
        }
        stopPlayback();
        if (!item.valid()) {
            return;
        }
        if (!load(current, item)) {
            emit owner->errorOccurred(QStringLiteral("无法解码 ") + item.path);
            emit owner->finished();
            return;
        }
        if (!ensureDevice(current.format)) {
            return;
        }
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        pushMarker(current);
        if (next.valid()) {
            pendingUpcoming = next;
            loadUpcoming();
        }
        output->setPull([this](uint8_t* dst, int frames) { return pull(dst, frames); });
        cancelFade();
        output->start();
        setState(State::Playing);
        emit owner->trackStarted(current.item.token);
    }

    void stopPlayback()
    {
        if (output && output->isOpen()) {
            output->pause();
        }
        cancelFade();
        ring.clear();
        draining = false;
        afterTail = AfterTail::None;
        current = {};
        upcoming = {};
        freeResampler();
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        owner->m_activeToken.store(0);
        owner->m_positionMs.store(0);
        setState(State::Stopped);
    }

    void loadUpcoming()
    {
        PlaybackItem next;
        {
            QMutexLocker locker(&mutex);
            next = pendingUpcoming;
        }
        upcoming = {};
        if (!next.valid() || next.token == current.item.token) {
            return;
        }
        load(upcoming, next);
    }

    bool load(Loaded& slot, const PlaybackItem& item)
    {
        slot = {};
        auto decoder = std::make_unique<Decoder>();
        QString error;
        if (!decoder->open(item.path, &error)) {
            return false;
        }
        const int rate = decoder->format().sampleRate;
        const qint64 startFrame = item.startMs > 0 ? item.startMs * rate / 1000 : 0;
        const qint64 endFrame = item.endMs >= 0 ? item.endMs * rate / 1000 : -1;
        decoder->setRange(startFrame, endFrame);
        slot.decoder = std::move(decoder);
        slot.format = slot.decoder->format();
        slot.item = item;
        slot.audibleFrames = itemFrames(*slot.decoder, item);
        return true;
    }

    bool ensureDevice(const SampleFormat& source, bool force = false)
    {
        if (!force && deviceReady && device.sampleRate == source.sampleRate && device.channels == source.channels
            && !wantExclusiveReopen && compatible(source)) {
            return true;
        }
        if (output->isOpen()) {
            output->pause();
            output->close();
            deviceReady = false;
        }
        QString error;
        DeviceFormat opened;
        const bool requestedExclusive = exclusive.load();
        if (!output->open(source, requestedExclusive, &opened, &error)) {
            emit owner->errorOccurred(error.isEmpty() ? QStringLiteral("无法打开输出设备") : error);
            setState(State::Stopped);
            return false;
        }
        if (requestedExclusive && !opened.exclusive) {
            emit owner->exclusiveFallback(QStringLiteral("设备被占用，已改用共享模式"));
        }
        device = opened;
        deviceReady = true;
        wantExclusiveReopen = false;
        owner->m_deviceRate.store(device.sampleRate);
        owner->m_deviceBits.store(device.bitsPerSample);
        owner->m_deviceExclusive.store(device.exclusive);
        emit owner->deviceOpened(source.sampleRate, source.bitsPerSample, source.isFloat, device.sampleRate, device.exclusive);
        freeResampler();
        if (source.sampleRate != device.sampleRate) {
            setupResampler(source);
        }
        return true;
    }

    bool compatible(const SampleFormat& source) const
    {
        if (source.sampleRate != device.sampleRate) {
            return false;
        }
        if (source.channels != device.channels) {
            return false;
        }
        if (source.isFloat != device.isFloat) {
            return false;
        }
        return true;
    }

    void setupResampler(const SampleFormat& source)
    {
        freeResampler();
        AVChannelLayout inLayout;
        AVChannelLayout outLayout;
        av_channel_layout_default(&inLayout, source.channels);
        av_channel_layout_default(&outLayout, device.channels);
        swr_alloc_set_opts2(&resampler, &outLayout, AV_SAMPLE_FMT_DBL, device.sampleRate, &inLayout, AV_SAMPLE_FMT_DBL,
                            source.sampleRate, 0, nullptr);
        if (resampler && av_opt_set(resampler, "resampler", "soxr", 0) == 0) {
            av_opt_set_double(resampler, "precision", 20.0, 0);
        }
        if (!resampler || swr_init(resampler) < 0) {
            freeResampler();
            swr_alloc_set_opts2(&resampler, &outLayout, AV_SAMPLE_FMT_DBL, device.sampleRate, &inLayout, AV_SAMPLE_FMT_DBL,
                                source.sampleRate, 0, nullptr);
            if (!resampler || swr_init(resampler) < 0) {
                freeResampler();
            }
        }
        resampleRate = resampler ? device.sampleRate : 0;
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
    }

    void freeResampler()
    {
        if (resampler) {
            swr_free(&resampler);
        }
        resampleRate = 0;
    }

    void pushMarker(const Loaded& slot)
    {
        const int rate = device.sampleRate > 0 ? device.sampleRate : slot.format.sampleRate;
        markers.push_back(Marker {framesQueued, slot.item.token, slot.item.startMs, rate});
        owner->m_activeToken.store(slot.item.token);
    }

    void pump()
    {
        if (!current.decoder) {
            stopPlayback();
            emit owner->finished();
            return;
        }
        if (wantExclusiveReopen && !draining) {
            wantExclusiveReopen = false;
            draining = true;
            afterTail = AfterTail::Reopen;
            return;
        }
        if (draining) {
            if (ring.availableRead() > 0) {
                QMutexLocker locker(&mutex);
                wait.wait(&mutex, 4);
                return;
            }
            const int waitMs = device.sampleRate > 0
                                   ? std::max(20, device.latencyFrames * 1000 / std::max(device.sampleRate, 1) + 15)
                                   : 40;
            QThread::msleep(static_cast<unsigned long>(waitMs));
            finishTail();
            return;
        }
        const int frameBytes = std::max(1, device.bytesPerFrame());
        const int low = device.sampleRate > 0 ? device.sampleRate / 5 * frameBytes : 16384;
        if (ring.availableRead() > low * 2) {
            QMutexLocker locker(&mutex);
            wait.wait(&mutex, 8);
            return;
        }
        PlaybackItem queuedNext;
        {
            QMutexLocker locker(&mutex);
            queuedNext = pendingUpcoming;
        }
        if (upcoming.decoder == nullptr && queuedNext.valid()) {
            loadUpcoming();
        }
        const int chunk = 4096;
        raw.resize(static_cast<size_t>(chunk * std::max(current.format.bytesPerFrame(), 4)));
        const int read = current.decoder->read(raw.data(), chunk);
        if (read < 0) {
            emit owner->errorOccurred(QStringLiteral("解码中断，已跳过"));
            advance(true);
            return;
        }
        if (read == 0) {
            flushResampler();
            advance(false);
            return;
        }
        writeFrames(raw.data(), read, current.format, current.item.gainDb);
    }

    void writeFloatBlock(const uint8_t* pcm, int count, float gain)
    {
        SampleFormat working;
        working.sampleRate = device.sampleRate;
        working.channels = device.channels > 0 ? device.channels : current.format.channels;
        working.pcm = Fooyin::SampleFormat::F64;
        working.bitsPerSample = 64;
        working.bytesPerSample = 8;
        working.isFloat = true;
        converted.resize(static_cast<size_t>(count) * static_cast<size_t>(device.bytesPerFrame()));
        convertSamples(pcm, working, count, converted.data(), device, gain, &dither);
        enqueueConverted(count);
    }

    void enqueueConverted(int count)
    {
        int offset = 0;
        const int bytes = count * std::max(1, device.bytesPerFrame());
        while (offset < bytes) {
            const int wrote = ring.write(converted.data() + offset, bytes - offset);
            if (wrote <= 0) {
                QMutexLocker locker(&mutex);
                wait.wait(&mutex, 4);
                if (owner->m_state.load() != static_cast<int>(State::Playing) && !draining) {
                    framesQueued += offset / std::max(1, device.bytesPerFrame());
                    return;
                }
                continue;
            }
            offset += wrote;
        }
        framesQueued += count;
    }

    void writeFrames(const uint8_t* src, int frames, const SampleFormat& format, float gainDb)
    {
        float gain = static_cast<float>(std::pow(10.0, static_cast<double>(gainDb) / 20.0));
        if (!std::isfinite(gain) || gain < 0.f) {
            gain = 0.f;
        }
        activeGain = gain;
        if (resampler && format.sampleRate != device.sampleRate) {
            const uint8_t* resampleInput = src;
            if (format.pcm != Fooyin::SampleFormat::F64) {
                DeviceFormat wide;
                wide.sampleRate = format.sampleRate;
                wide.channels = format.channels;
                wide.bitsPerSample = 64;
                wide.containerBytes = 8;
                wide.isFloat = true;
                sourceFloat.resize(static_cast<size_t>(frames) * static_cast<size_t>(std::max(format.channels, 1)) * sizeof(double));
                convertSamples(src, format, frames, sourceFloat.data(), wide, 1.f, nullptr);
                resampleInput = sourceFloat.data();
            }
            const int outEstimate = static_cast<int>(av_rescale_rnd(frames, device.sampleRate, format.sampleRate, AV_ROUND_UP)) + 64;
            floatBlock.resize(static_cast<size_t>(outEstimate) * static_cast<size_t>(std::max(device.channels, 1)) * sizeof(double));
            uint8_t* outputPlanes[] = {floatBlock.data()};
            const uint8_t* inputPlanes[] = {resampleInput};
            const int got = swr_convert(resampler, outputPlanes, outEstimate, inputPlanes, frames);
            if (got > 0) {
                writeFloatBlock(floatBlock.data(), got, gain);
            }
            return;
        }
        converted.resize(static_cast<size_t>(frames) * static_cast<size_t>(device.bytesPerFrame()));
        convertSamples(src, format, frames, converted.data(), device, gain, &dither);
        enqueueConverted(frames);
    }

    void flushResampler()
    {
        if (!resampler || !deviceReady) {
            return;
        }
        const int channels = std::max(1, device.channels);
        for (int pass = 0; pass < 64; ++pass) {
            const int outEstimate = 8192;
            floatBlock.resize(static_cast<size_t>(outEstimate) * static_cast<size_t>(channels) * sizeof(double));
            uint8_t* outputPlanes[] = {floatBlock.data()};
            const int got = swr_convert(resampler, outputPlanes, outEstimate, nullptr, 0);
            if (got <= 0) {
                break;
            }
            writeFloatBlock(floatBlock.data(), got, activeGain);
        }
    }

    void advance(bool failed)
    {
        const bool sameFormat = !failed && upcoming.decoder && upcoming.format.sampleRate == current.format.sampleRate
            && upcoming.format.channels == current.format.channels && upcoming.format.isFloat == current.format.isFloat
            && upcoming.format.bitsPerSample == current.format.bitsPerSample && upcoming.format.pcm == current.format.pcm;
        const bool contiguous = sameFormat && current.item.path == upcoming.item.path && current.item.endMs >= 0
            && std::abs(upcoming.item.startMs - current.item.endMs) <= 2;
        if (contiguous) {
            const int rate = std::max(current.format.sampleRate, 1);
            const qint64 endFrame = upcoming.item.endMs >= 0 ? upcoming.item.endMs * rate / 1000 : -1;
            current.decoder->extendRange(endFrame);
            current.item = upcoming.item;
            current.format = upcoming.format;
            current.audibleFrames = itemFrames(*current.decoder, current.item);
            upcoming = {};
            pushMarker(current);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        if (sameFormat) {
            current = std::move(upcoming);
            upcoming = {};
            pushMarker(current);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        draining = true;
        afterTail = upcoming.decoder ? AfterTail::Switch : AfterTail::Stop;
        if (failed && upcoming.decoder) {
            afterTail = AfterTail::Switch;
        }
    }

    void finishTail()
    {
        const AfterTail action = afterTail;
        draining = false;
        afterTail = AfterTail::None;
        if (action == AfterTail::Switch && upcoming.decoder) {
            current = std::move(upcoming);
            upcoming = {};
            freeResampler();
            if (!ensureDevice(current.format, true)) {
                emit owner->finished();
                return;
            }
            framesPlayed.store(0);
            framesQueued = 0;
            markers.clear();
            ring.clear();
            output->setPull([this](uint8_t* dst, int frames) { return pull(dst, frames); });
            output->start();
            pushMarker(current);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        if (action == AfterTail::Reopen && current.decoder) {
            const auto source = current.format;
            ring.clear();
            framesPlayed.store(0);
            framesQueued = 0;
            if (!ensureDevice(source, true)) {
                emit owner->finished();
                return;
            }
            output->setPull([this](uint8_t* dst, int frames) { return pull(dst, frames); });
            output->start();
            pushMarker(current);
            return;
        }
        stopPlayback();
        emit owner->finished();
    }

    void seekCurrent()
    {
        qint64 position = 0;
        {
            QMutexLocker locker(&mutex);
            position = pendingSeekMs;
        }
        if (!current.decoder) {
            return;
        }
        const bool wasPlaying = owner->m_state.load() == static_cast<int>(State::Playing);
        if (wasPlaying && output) {
            output->pause();
        }
        const qint64 frame = std::max<qint64>(0, (current.item.startMs + position) * current.format.sampleRate / 1000);
        ring.clear();
        if (resampler) {
            swr_init(resampler);
        }
        current.decoder->setRange(frame, current.item.endMs >= 0 ? current.item.endMs * current.format.sampleRate / 1000 : -1);
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        Marker marker;
        marker.queuedFrame = 0;
        marker.token = current.item.token;
        marker.startMs = current.item.startMs + position;
        marker.sampleRate = device.sampleRate > 0 ? device.sampleRate : current.format.sampleRate;
        markers.push_back(marker);
        if (wasPlaying && output && deviceReady) {
            output->start();
        }
    }

    bool beginFade(OutputFade kind, int milliseconds)
    {
        if (!deviceReady || device.sampleRate <= 0 || owner->m_state.load() != static_cast<int>(State::Playing)) {
            return false;
        }
        const int frames = std::max(1, device.sampleRate * milliseconds / 1000);
        fadeKind.store(static_cast<int>(OutputFade::None));
        fadeDone.store(0);
        fadeTotal.store(frames);
        fadeKind.store(static_cast<int>(kind));
        return true;
    }

    void cancelFade()
    {
        fadeKind.store(static_cast<int>(OutputFade::None));
        fadeDone.store(0);
        fadeTotal.store(0);
    }

    void completeFade()
    {
        const auto kind = static_cast<OutputFade>(fadeKind.load());
        const int total = fadeTotal.load();
        if (kind == OutputFade::None || total <= 0 || fadeDone.load() < total) {
            return;
        }
        fadeKind.store(static_cast<int>(OutputFade::None));
        if (kind == OutputFade::OutPause) {
            if (output) {
                output->pause();
            }
            setState(State::Paused);
            return;
        }
        if (kind == OutputFade::OutStop) {
            stopPlayback();
            return;
        }
        if (kind == OutputFade::OutSeek) {
            seekCurrent();
            beginFade(OutputFade::In, 120);
        }
    }

    void scalePulled(uint8_t* dst, int frames)
    {
        if (frames <= 0) {
            return;
        }
        const float target = volumeCurve(std::clamp(volume.load(), 0.f, 1.f));
        const int channels = std::max(1, device.channels);
        const int ramp = std::max(1, device.sampleRate / 50);
        const auto kind = static_cast<OutputFade>(fadeKind.load());
        const int total = fadeTotal.load();
        int done = fadeDone.load();
        const int sampleBytes = std::max(1, device.containerBytes);
        for (int frame = 0; frame < frames; ++frame) {
            smoothedVolume += (target - smoothedVolume) * std::min(1.f, 1.f / static_cast<float>(ramp));
            float envelope = smoothedVolume;
            if (kind != OutputFade::None && total > 0) {
                const double progress = std::clamp(static_cast<double>(done + frame) / static_cast<double>(total), 0.0, 1.0);
                const auto direction = kind == OutputFade::In ? Fooyin::Engine::FadeDirection::In : Fooyin::Engine::FadeDirection::Out;
                envelope *= static_cast<float>(Fooyin::Engine::gain01(progress, Fooyin::Engine::FadeCurve::Linear, direction));
            }
            if (std::fabs(envelope - 1.f) <= 0.0001f) {
                continue;
            }
            uint8_t* sample = dst + static_cast<size_t>(frame) * static_cast<size_t>(device.bytesPerFrame());
            for (int channel = 0; channel < channels; ++channel) {
                uint8_t* word = sample + static_cast<size_t>(channel) * static_cast<size_t>(sampleBytes);
                if (device.isFloat && sampleBytes == 4) {
                    float value = 0;
                    std::memcpy(&value, word, sizeof(value));
                    value = std::clamp(value * envelope, -1.f, 1.f);
                    std::memcpy(word, &value, sizeof(value));
                } else if (device.isFloat && sampleBytes == 8) {
                    double value = 0;
                    std::memcpy(&value, word, sizeof(value));
                    value = std::clamp(value * static_cast<double>(envelope), -1.0, 1.0);
                    std::memcpy(word, &value, sizeof(value));
                } else if (sampleBytes == 2) {
                    int16_t value = 0;
                    std::memcpy(&value, word, sizeof(value));
                    const auto scaled = static_cast<int32_t>(std::lround(static_cast<float>(value) * envelope));
                    value = static_cast<int16_t>(std::clamp(scaled, -32768, 32767));
                    std::memcpy(word, &value, sizeof(value));
                } else if (sampleBytes == 4) {
                    int32_t value = 0;
                    std::memcpy(&value, word, sizeof(value));
                    const auto scaled = std::llround(static_cast<double>(value) * static_cast<double>(envelope));
                    value = static_cast<int32_t>(std::clamp(scaled, static_cast<long long>(INT32_MIN), static_cast<long long>(INT32_MAX)));
                    std::memcpy(word, &value, sizeof(value));
                }
            }
        }
        if (kind != OutputFade::None) {
            fadeDone.store(done + frames);
        }
    }

    int pull(uint8_t* dst, int frames)
    {
        const int bytes = frames * device.bytesPerFrame();
        const int got = ring.read(dst, bytes);
        const int frameBytes = std::max(1, device.bytesPerFrame());
        const int whole = got / frameBytes;
        scalePulled(dst, whole);
        framesPlayed.fetch_add(whole);
        return whole;
    }

    void publishPosition()
    {
        const qint64 played = framesPlayed.load();
        const qint64 heard = std::max<qint64>(0, played - device.latencyFrames);
        Marker active;
        bool found = false;
        for (const auto& marker : markers) {
            if (marker.queuedFrame <= heard) {
                active = marker;
                found = true;
            }
        }
        if (!found || active.sampleRate <= 0) {
            return;
        }
        const qint64 into = heard - active.queuedFrame;
        owner->m_activeToken.store(active.token);
        owner->m_positionMs.store(active.startMs + into * 1000 / active.sampleRate);
    }

    void releaseDevice()
    {
        if (output) {
            output->pause();
            output->close();
        }
        freeResampler();
        deviceReady = false;
    }
};

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
    m_worker = new Worker();
    m_worker->owner = this;
    m_worker->thread = QThread::create([this] { m_worker->run(); });
    m_worker->thread->start();
}

AudioEngine::~AudioEngine()
{
    if (!m_worker) {
        return;
    }
    {
        QMutexLocker locker(&m_worker->mutex);
        m_worker->quit = true;
        m_worker->wait.wakeAll();
    }
    m_worker->thread->wait();
    delete m_worker->thread;
    delete m_worker;
    m_worker = nullptr;
}

void AudioEngine::play(const PlaybackItem& current, const PlaybackItem& upcoming)
{
    QMutexLocker locker(&m_worker->mutex);
    m_worker->pendingCurrent = current;
    m_worker->pendingUpcoming = upcoming;
    locker.unlock();
    m_worker->post(Worker::Play);
}

void AudioEngine::setUpcoming(const PlaybackItem& upcoming)
{
    QMutexLocker locker(&m_worker->mutex);
    m_worker->pendingUpcoming = upcoming;
    locker.unlock();
    m_worker->post(Worker::Upcoming);
}

void AudioEngine::pause()
{
    m_worker->post(Worker::Pause);
}

void AudioEngine::resume()
{
    m_worker->post(Worker::Resume);
}

void AudioEngine::stop()
{
    m_worker->post(Worker::Stop);
}

void AudioEngine::seekMs(qint64 positionMs)
{
    QMutexLocker locker(&m_worker->mutex);
    m_worker->pendingSeekMs = positionMs;
    locker.unlock();
    m_worker->post(Worker::Seek);
}

void AudioEngine::setVolume(float slider)
{
    m_worker->volume.store(std::clamp(slider, 0.f, 1.f));
}

void AudioEngine::setExclusive(bool exclusive)
{
    m_worker->exclusive.store(exclusive);
    m_worker->post(Worker::Exclusive);
}

AudioEngine::State AudioEngine::state() const
{
    return static_cast<State>(m_state.load());
}

qint64 AudioEngine::positionMs() const
{
    return m_positionMs.load();
}

qint64 AudioEngine::activeToken() const
{
    return m_activeToken.load();
}

DeviceFormat AudioEngine::deviceFormat() const
{
    DeviceFormat format;
    format.sampleRate = m_deviceRate.load();
    format.bitsPerSample = m_deviceBits.load();
    format.exclusive = m_deviceExclusive.load();
    return format;
}

} // namespace echo
