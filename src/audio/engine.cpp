#include "audio/engine.h"

#include "audio/convert.h"
#include "audio/decoder.h"
#include "audio/output.h"
#include "audio/ringbuffer.h"

#include <QElapsedTimer>
#include <QMutex>
#include <QHash>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
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
    PlaybackItem item;
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

constexpr int kFadeTransportMs = 300;
constexpr int kFadeShortMs = 120;
constexpr int kDecodeBlockFrames = 4096;
constexpr int kBufferLowDivisor = 5;
constexpr int kVolumeRampDivisor = 50;

struct AudioEngine::Worker {
    AudioEngine* owner = nullptr;
    std::unique_ptr<AudioOutput> output;
    RingBuffer ring;
    QMutex mutex;
    QWaitCondition wait;
    std::deque<int> commands;
    PlaybackItem pendingCurrent;
    PlaybackItem pendingUpcoming;
    qint64 pendingSeekMs = 0;
    QHash<qint64, float> pendingGains;
    std::atomic<float> volume {1.f};
    std::atomic<bool> exclusive {false};
    std::atomic<bool> spatial {false};
    QString lastSpatialStatus;
    bool fadingEnabled = true;
    bool pendingFading = true;
    bool quit = false;

    Loaded current;
    Loaded upcoming;
    struct OutputSession {
        DeviceFormat device;
        bool queued = false;
        bool ready = false;
    } session;
    struct ResampleStage {
        SwrContext* context = nullptr;
        int rate = 0;
        void reset()
        {
            if (context)
                swr_free(&context);
            rate = 0;
        }
        ~ResampleStage() { reset(); }
    } resample;
    SampleFormat configuredSource;
    uint32_t dither = 0xC0FFEEU;
    uint32_t outputDither = 0x1234567U;
    float activeGain = 1.f;
    qint64 framesQueued = 0;
    bool draining = false;
    enum class AfterTail { None, Switch, Stop };
    AfterTail afterTail = AfterTail::None;
    std::vector<uint8_t> raw;
    std::vector<uint8_t> converted;
    std::vector<uint8_t> floatBlock;
    std::vector<uint8_t> sourceFloat;
    std::vector<double> ramped;
    std::atomic<qint64> framesPlayed {0};
    std::vector<Marker> markers;
    QThread* thread = nullptr;
    enum class OutputFade { None, In, OutPause, OutStop, OutSeek, OutPlay };
    struct FadeController {
        std::atomic<int> kind {static_cast<int>(OutputFade::None)};
        std::atomic<int> total {0};
        std::atomic<int> done {0};
        float smoothedVolume = 1.f;
        std::atomic<float> level {1.f};
        std::atomic<float> start {0.f};
        QElapsedTimer clock;
        qint64 audibleStart = 0;
    } fader;

    enum Command { Play = 1, Upcoming, Pause, Resume, Stop, Seek, Volume, ConfigureOutput, Gain, ReleaseFiles, Fading };

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
            if (!session.queued && fader.kind.load() != static_cast<int>(OutputFade::None) && fader.kind.load() != static_cast<int>(OutputFade::In)
                && ring.availableRead() == 0) {
                fader.done.store(fader.total.load());
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
            if (!beginFade(OutputFade::OutPlay, kFadeTransportMs)) startPlayback();
            break;
        case Upcoming:
            loadUpcoming();
            break;
        case Pause:
            if (!beginFade(OutputFade::OutPause, kFadeShortMs)) {
                if (output) {
                    output->pause();
                }
                setState(State::Paused);
            }
            break;
        case Resume:
            if (session.ready && output) {
                beginFade(OutputFade::In, kFadeShortMs);
                output->start();
                setState(State::Playing);
            }
            break;
        case ReleaseFiles:
            stopPlayback();
            emit owner->filesReleased();
            break;
        case Stop:
            if (!beginFade(OutputFade::OutStop, kFadeTransportMs)) {
                stopPlayback();
            }
            break;
        case Seek:
            if (!beginFade(OutputFade::OutSeek, kFadeShortMs)) {
                seekCurrent();
            }
            break;
        case Volume:
            if (output) output->setVolume(volumeCurve(volume.load()) * (session.queued ? fader.level.load() : 1.f));
            break;
        case Fading: {
            { QMutexLocker locker(&mutex); fadingEnabled = pendingFading; }
            if (!fadingEnabled) {
                fader.done.store(fader.total.load());
                completeFade(true);
                cancelFade();
            }
            break;
        }
        case Gain: {
            QMutexLocker locker(&mutex);
            const auto apply = [this](PlaybackItem& item) {
                if (pendingGains.contains(item.token)) item.gainDb = pendingGains.value(item.token);
            };
            apply(current.item);
            apply(upcoming.item);
            apply(pendingCurrent);
            apply(pendingUpcoming);
            for (auto& marker : markers) apply(marker.item);
            pendingGains.clear();
            break;
        }
        case ConfigureOutput:
            if (const auto fade = static_cast<OutputFade>(fader.kind.load());
                fade != OutputFade::None && fade != OutputFade::In) {
                fader.done.store(fader.total.load());
                completeFade(true);
            }
            reconfigureOutput();
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
        beginFade(OutputFade::In, kFadeShortMs);
        output->start();
        setState(State::Playing);
        emit owner->trackStarted(current.item.token);
    }

    void stopPlayback()
    {
        if (output && output->isOpen()) {
            output->pause();
            output->flush();
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
        if (!force && session.ready && source == configuredSource) {
            if (!resample.context && (source.sampleRate != session.device.sampleRate || source.channels != session.device.channels
                || (source.channelMask && session.device.channelMask && source.channelMask != session.device.channelMask))) {
                setupResampler(source);
                if (!resample.context) {
                    emit owner->errorOccurred(QStringLiteral("无法初始化音频重采样或声道混音"));
                    output->close();
                    session.ready = false;
                    setState(State::Stopped);
                    return false;
                }
            }
            return true;
        }
        if (output->isOpen()) {
            output->pause();
            output->close();
            session.ready = false;
        }
        QString error;
        DeviceFormat opened;
        const bool requestedExclusive = exclusive.load();
        output->setSpatialEnabled(spatial.load());
        if (!output->open(source, requestedExclusive, &opened, &error)) {
            emit owner->errorOccurred(error.isEmpty() ? QStringLiteral("无法打开输出设备") : error);
            setState(State::Stopped);
            return false;
        }
        if (requestedExclusive && !opened.exclusive) {
            emit owner->exclusiveFallback(QStringLiteral("设备被占用，已改用共享模式"));
        }
        const QString status = output->spatialStatus();
        if (status != lastSpatialStatus) {
            lastSpatialStatus = status;
            emit owner->spatialStatusChanged(status);
        }
        session.queued = output->queuedPlayback();
        if (session.queued) fader.smoothedVolume = 1.f;
        output->setVolume(volumeCurve(volume.load()));
        configuredSource = source;
        session.device = opened;
        session.ready = true;
        owner->m_deviceRate.store(session.device.sampleRate);
        owner->m_deviceBits.store(session.device.bitsPerSample);
        owner->m_deviceExclusive.store(session.device.exclusive);
        emit owner->deviceOpened(source.sampleRate, source.bitsPerSample, source.isFloat, session.device.sampleRate, session.device.exclusive);
        freeResampler();
        if (source.sampleRate != session.device.sampleRate || source.channels != session.device.channels
            || (source.channelMask && session.device.channelMask && source.channelMask != session.device.channelMask)) {
            setupResampler(source);
            if (!resample.context) {
                emit owner->errorOccurred(QStringLiteral("无法初始化音频重采样或声道混音"));
                output->close();
                session.ready = false;
                setState(State::Stopped);
                return false;
            }
        }
        return true;
    }

    void setupResampler(const SampleFormat& source)
    {
        freeResampler();
        AVChannelLayout inLayout;
        AVChannelLayout outLayout;
        if (source.channelMask) av_channel_layout_from_mask(&inLayout, source.channelMask);
        else av_channel_layout_default(&inLayout, source.channels);
        if (session.device.channelMask) av_channel_layout_from_mask(&outLayout, session.device.channelMask);
        else av_channel_layout_default(&outLayout, session.device.channels);
        swr_alloc_set_opts2(&resample.context, &outLayout, AV_SAMPLE_FMT_DBL, session.device.sampleRate, &inLayout, AV_SAMPLE_FMT_DBL,
                            source.sampleRate, 0, nullptr);
        if (resample.context && std::strstr(swresample_configuration(), "--enable-libsoxr")
            && av_opt_set(resample.context, "resample.context", "soxr", 0) == 0) {
            av_opt_set_double(resample.context, "precision", 28.0, 0);
        }
        if (resample.context) {
            av_opt_set_int(resample.context, "filter_size", 64, 0);
            av_opt_set_int(resample.context, "exact_rational", 1, 0);
            av_opt_set_double(resample.context, "rematrix_maxval", 1., 0);
        }
        if (!resample.context || swr_init(resample.context) < 0) {
            freeResampler();
            swr_alloc_set_opts2(&resample.context, &outLayout, AV_SAMPLE_FMT_DBL, session.device.sampleRate, &inLayout, AV_SAMPLE_FMT_DBL,
                                source.sampleRate, 0, nullptr);
            if (resample.context) {
                av_opt_set_int(resample.context, "filter_size", 64, 0);
                av_opt_set_int(resample.context, "exact_rational", 1, 0);
                av_opt_set_double(resample.context, "rematrix_maxval", 1., 0);
            }
            if (!resample.context || swr_init(resample.context) < 0) {
                freeResampler();
            }
        }
        resample.rate = resample.context ? session.device.sampleRate : 0;
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_uninit(&outLayout);
    }

    void freeResampler()
    {
        resample.reset();
    }

    void pushMarker(const Loaded& slot, bool includeFilterDelay = false)
    {
        activeGain = std::pow(10.f, slot.item.gainDb / 20.f);
        const int rate = session.device.sampleRate > 0 ? session.device.sampleRate : slot.format.sampleRate;
        const qint64 delayed = includeFilterDelay && resample.context ? swr_get_delay(resample.context, rate) : 0;
        markers.push_back(Marker {framesQueued + delayed, slot.item.token, slot.item.startMs, rate, slot.item});
        owner->m_activeToken.store(slot.item.token);
    }

    void reconfigureOutput()
    {
        if (!current.decoder || !session.ready) {
            output->close();
            session.ready = false;
            freeResampler();
            owner->m_deviceRate.store(0);
            owner->m_deviceBits.store(0);
            owner->m_deviceExclusive.store(false);
            output->setSpatialEnabled(spatial.load());
            lastSpatialStatus = output->spatialStatus();
            emit owner->spatialStatusChanged(lastSpatialStatus);
            return;
        }
        const bool playing = owner->m_state.load() == static_cast<int>(State::Playing);
        output->pause();
        publishPosition();
        const qint64 position = owner->m_positionMs.load();
        const qint64 token = owner->m_activeToken.load();
        PlaybackItem item = current.item;
        PlaybackItem next;
        bool found = false;
        for (const auto& marker : markers) {
            if (marker.token == token && marker.item.valid()) { item = marker.item; found = true; }
            else if (found && marker.item.valid()) { next = marker.item; break; }
        }
        {
            QMutexLocker locker(&mutex);
            if (!next.valid() && pendingUpcoming.token != item.token) next = pendingUpcoming;
            pendingUpcoming = next;
        }
        output->flush();
        ring.clear();
        cancelFade();
        draining = false;
        afterTail = AfterTail::None;
        if (!load(current, item) || !ensureDevice(current.format, true)) {
            stopPlayback();
            emit owner->errorOccurred(QStringLiteral("切换音频输出失败，请重新播放。"));
            return;
        }
        current.decoder->setRange(position * current.format.sampleRate / 1000,
            item.endMs < 0 ? -1 : item.endMs * current.format.sampleRate / 1000);
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        pushMarker(current);
        markers.back().startMs = position;
        loadUpcoming();
        emit owner->trackStarted(current.item.token);
        output->setPull([this](uint8_t* dst, int frames) { return pull(dst, frames); });
        if (playing) {
            beginFade(OutputFade::In, kFadeShortMs);
            output->start();
        }
    }

    void pump()
    {
        const QString failure = output->takeError();
        if (!failure.isEmpty()) {
            reconfigureOutput();
            return;
        }
        if (!current.decoder) {
            stopPlayback();
            emit owner->finished();
            return;
        }
        if (draining) {
            if (ring.availableRead() == 0) output->finishInput();
            if (ring.availableRead() > 0 || output->pendingFrames() > 0) {
                QMutexLocker locker(&mutex);
                wait.wait(&mutex, 4);
                return;
            }
            const int waitMs = session.device.sampleRate > 0
                                   ? std::max(20, session.device.latencyFrames * 1000 / std::max(session.device.sampleRate, 1) + 15)
                                   : 40;
            QThread::msleep(static_cast<unsigned long>(waitMs));
            finishTail();
            return;
        }
        const int frameBytes = std::max(1, session.device.bytesPerFrame());
        const int low = session.device.sampleRate > 0 ? session.device.sampleRate / kBufferLowDivisor * frameBytes : 16384;
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
        const int chunk = kDecodeBlockFrames;
        raw.resize(static_cast<size_t>(chunk * std::max(current.format.bytesPerFrame(), 4)));
        const int read = current.decoder->read(raw.data(), chunk);
        if (read < 0) {
            emit owner->errorOccurred(QStringLiteral("解码中断，已跳过"));
            advance(true);
            return;
        }
        if (read == 0) {
            advance(false);
            return;
        }
        writeFrames(raw.data(), read, current.format, current.item.gainDb);
    }

    void queueFloatBlock(const uint8_t* pcm, int count, float gain = 1.f)
    {
        SampleFormat working;
        working.sampleRate = session.device.sampleRate;
        working.channels = session.device.channels;
        working.pcm = PcmType::F64;
        working.bitsPerSample = 64;
        working.bytesPerSample = 8;
        working.isFloat = true;
        working.channelMask = session.device.channelMask;
        converted.resize(static_cast<size_t>(count) * static_cast<size_t>(session.device.bytesPerFrame()));
        convertSamples(pcm, working, count, converted.data(), session.device, gain, &dither);
        enqueueConverted(count);
    }

    void writeFloatBlock(const uint8_t* pcm, int count, float gain)
    {
        if (std::abs(activeGain - gain) > 0.00001f) {
            const auto* samples = reinterpret_cast<const double*>(pcm);
            ramped.assign(samples, samples + count * session.device.channels);
            const float step = 1.f / std::max(1, session.device.sampleRate / 10);
            for (int frame = 0; frame < count; ++frame) {
                activeGain += std::clamp(gain - activeGain, -step, step);
                for (int ch = 0; ch < session.device.channels; ++ch) ramped[frame * session.device.channels + ch] *= activeGain;
            }
            pcm = reinterpret_cast<const uint8_t*>(ramped.data());
            gain = 1.f;
        } else {
            activeGain = gain;
        }
        queueFloatBlock(pcm, count, gain);
    }

    void enqueueConverted(int count)
    {
        int offset = 0;
        const int bytes = count * std::max(1, session.device.bytesPerFrame());
        while (offset < bytes) {
            const int wrote = ring.write(converted.data() + offset, bytes - offset);
            if (wrote <= 0) {
                QMutexLocker locker(&mutex);
                wait.wait(&mutex, 4);
                if (owner->m_state.load() != static_cast<int>(State::Playing) && !draining) {
                    framesQueued += offset / std::max(1, session.device.bytesPerFrame());
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
        if (resample.context) {
            const uint8_t* resampleInput = src;
            const bool scale = gain != 1.f || std::abs(activeGain - gain) > 0.00001f;
            if (format.pcm != PcmType::F64 || scale) {
                DeviceFormat wide;
                wide.sampleRate = format.sampleRate;
                wide.channels = format.channels;
                wide.bitsPerSample = 64;
                wide.containerBytes = 8;
                wide.isFloat = true;
                wide.channelMask = format.channelMask;
                sourceFloat.resize(static_cast<size_t>(frames) * static_cast<size_t>(std::max(format.channels, 1)) * sizeof(double));
                convertSamples(src, format, frames, sourceFloat.data(), wide, 1.f, nullptr);
                if (scale) {
                    // Apply gain to input samples so buffered filter tails keep their track's gain.
                    auto* samples = reinterpret_cast<double*>(sourceFloat.data());
                    const float step = 1.f / std::max(1, format.sampleRate / 10);
                    for (int frame = 0; frame < frames; ++frame) {
                        activeGain += std::clamp(gain - activeGain, -step, step);
                        for (int ch = 0; ch < format.channels; ++ch) samples[frame * format.channels + ch] *= activeGain;
                    }
                }
                resampleInput = sourceFloat.data();
            }
            const int estimated = swr_get_out_samples(resample.context, frames);
            const int outEstimate = estimated > 0 ? estimated : static_cast<int>(av_rescale_rnd(
                swr_get_delay(resample.context, format.sampleRate) + frames, session.device.sampleRate, format.sampleRate, AV_ROUND_UP)) + 256;
            floatBlock.resize(static_cast<size_t>(outEstimate) * static_cast<size_t>(std::max(session.device.channels, 1)) * sizeof(double));
            uint8_t* outputPlanes[] = {floatBlock.data()};
            const uint8_t* inputPlanes[] = {resampleInput};
            const int got = swr_convert(resample.context, outputPlanes, outEstimate, inputPlanes, frames);
            if (got > 0) {
                queueFloatBlock(floatBlock.data(), got);
            }
            return;
        }
        if (std::abs(activeGain - gain) > 0.00001f) {
            DeviceFormat wide {format.sampleRate, format.channels, 64, 8, true};
            sourceFloat.resize(static_cast<size_t>(frames) * format.channels * sizeof(double));
            convertSamples(src, format, frames, sourceFloat.data(), wide, 1.f, nullptr);
            writeFloatBlock(sourceFloat.data(), frames, gain);
            return;
        }
        converted.resize(static_cast<size_t>(frames) * static_cast<size_t>(session.device.bytesPerFrame()));
        convertSamples(src, format, frames, converted.data(), session.device, gain, &dither);
        enqueueConverted(frames);
    }

    void flushResampler()
    {
        if (!resample.context || !session.ready) {
            return;
        }
        const int channels = std::max(1, session.device.channels);
        for (int pass = 0; pass < 64; ++pass) {
            const int outEstimate = 8192;
            floatBlock.resize(static_cast<size_t>(outEstimate) * static_cast<size_t>(channels) * sizeof(double));
            uint8_t* outputPlanes[] = {floatBlock.data()};
            const int got = swr_convert(resample.context, outputPlanes, outEstimate, nullptr, 0);
            if (got <= 0) {
                break;
            }
            queueFloatBlock(floatBlock.data(), got);
        }
    }

    void advance(bool failed)
    {
        const bool sameFormat = !failed && upcoming.decoder && upcoming.format.sampleRate == current.format.sampleRate
            && upcoming.format.channels == current.format.channels && upcoming.format.isFloat == current.format.isFloat
            && upcoming.format.bitsPerSample == current.format.bitsPerSample && upcoming.format.pcm == current.format.pcm
            && upcoming.format.channelMask == current.format.channelMask;
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
            pushMarker(current, true);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        if (sameFormat) {
            current = std::move(upcoming);
            upcoming = {};
            pushMarker(current, true);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        // End the filter only at the end of this output stream, never at a gapless boundary.
        flushResampler();
        draining = true;
        afterTail = upcoming.decoder ? AfterTail::Switch : AfterTail::Stop;
        if (failed && upcoming.decoder) {
            afterTail = AfterTail::Switch;
        }
    }

    void finishTail()
    {
        const auto fade = static_cast<OutputFade>(fader.kind.load());
        if (fade != OutputFade::None && fade != OutputFade::In) {
            fader.done.store(fader.total.load());
            completeFade(true);
            return;
        }
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
        if (output) output->pause();
        publishPosition();
        const qint64 audibleToken = owner->m_activeToken.load();
        if (audibleToken != current.item.token) {
            PlaybackItem audible;
            PlaybackItem next;
            bool found = false;
            for (const auto& marker : markers) {
                if (marker.token == audibleToken) { audible = marker.item; found = true; }
                else if (found) { next = marker.item; break; }
            }
            if (audible.valid()) {
                if (!load(current, audible)) return;
                {
                    QMutexLocker locker(&mutex);
                    if (next.valid()) pendingUpcoming = next;
                }
                loadUpcoming();
                emit owner->trackStarted(current.item.token);
            }
        }
        const qint64 frame = std::max<qint64>(0, (current.item.startMs + position) * current.format.sampleRate / 1000);
        if (output) output->flush();
        ring.clear();
        draining = false;
        afterTail = AfterTail::None;
        if (resample.context) {
            swr_init(resample.context);
        }
        current.decoder->setRange(frame, current.item.endMs >= 0 ? current.item.endMs * current.format.sampleRate / 1000 : -1);
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        Marker marker;
        marker.queuedFrame = 0;
        marker.token = current.item.token;
        marker.item = current.item;
        marker.startMs = current.item.startMs + position;
        marker.sampleRate = session.device.sampleRate > 0 ? session.device.sampleRate : current.format.sampleRate;
        markers.push_back(marker);
        owner->m_positionMs.store(marker.startMs);
        owner->m_activeToken.store(marker.token);
        emit owner->positionChanged();
        cancelFade();
        if (wasPlaying && output && session.ready) {
            beginFade(OutputFade::In, kFadeShortMs);
            output->start();
        }
    }

    bool beginFade(OutputFade kind, int milliseconds)
    {
        if (!fadingEnabled || !session.ready || session.device.sampleRate <= 0
            || (kind != OutputFade::In && owner->m_state.load() != static_cast<int>(State::Playing))) {
            return false;
        }
        if (session.queued) updateQueuedFade();
        const float start = kind == OutputFade::In
            ? (fader.kind.load() == static_cast<int>(OutputFade::None) ? 0.f : fader.level.load()) : fader.level.load();
        const int frames = std::max(1, session.device.sampleRate * milliseconds / 1000);
        fader.kind.store(static_cast<int>(OutputFade::None));
        fader.start.store(start);
        fader.level.store(start);
        fader.clock.start();
        if (session.queued) {
            fader.audibleStart = std::max<qint64>(0, framesPlayed.load() - output->pendingFrames());
            output->setVolume(volumeCurve(volume.load()) * start);
        }
        fader.done.store(0);
        fader.total.store(frames);
        fader.kind.store(static_cast<int>(kind));
        return true;
    }

    void cancelFade()
    {
        fader.kind.store(static_cast<int>(OutputFade::None));
        fader.done.store(0);
        fader.total.store(0);
        fader.level.store(1.f);
        if (session.queued && output) output->setVolume(volumeCurve(volume.load()));
    }

    void updateQueuedFade()
    {
        const auto kind = static_cast<OutputFade>(fader.kind.load());
        if (!session.queued || kind == OutputFade::None || fader.total.load() <= 0) return;
        // Fade in against the audible clock: native queued output may need preroll before it starts.
        const qint64 elapsedFrames = kind == OutputFade::In
            ? std::max<qint64>(0, framesPlayed.load() - output->pendingFrames() - fader.audibleStart)
            : fader.clock.elapsed() * session.device.sampleRate / 1000;
        const int done = static_cast<int>(std::min<qint64>(fader.total.load(), elapsedFrames));
        fader.done.store(done);
        const float progress = static_cast<float>(done) / fader.total.load();
        const float end = kind == OutputFade::In ? 1.f : 0.f;
        fader.level.store(fader.start.load() + (end - fader.start.load()) * progress);
        output->setVolume(volumeCurve(volume.load()) * fader.level.load());
    }

    void completeFade(bool force = false)
    {
        if (!force) updateQueuedFade();
        const auto kind = static_cast<OutputFade>(fader.kind.load());
        const int total = fader.total.load();
        if (kind == OutputFade::None || total <= 0 || fader.done.load() < total) {
            return;
        }
        // Keep an outgoing envelope at silence until the callback has actually stopped.
        if (kind == OutputFade::In) fader.kind.store(static_cast<int>(OutputFade::None));
        fader.level.store(kind == OutputFade::In ? 1.f : 0.f);
        if (session.queued && output) output->setVolume(volumeCurve(volume.load()) * fader.level.load());
        if (output && !session.queued && kind != OutputFade::In) output->drain();
        if (kind == OutputFade::OutPlay) {
            startPlayback();
            return;
        }
        if (kind == OutputFade::OutPause) {
            if (output) {
                output->pause();
                publishPosition();
            }
            fader.kind.store(static_cast<int>(OutputFade::None));
            setState(State::Paused);
            return;
        }
        if (kind == OutputFade::OutStop) {
            stopPlayback();
            return;
        }
        if (kind == OutputFade::OutSeek) {
            seekCurrent();
        }
    }

    // Per-frame convertSamples rebuilds a SampleFormat, but a 512-frame 48 kHz
    // float callback spends about 6 microseconds on it (measured at -O3: 3.3x a
    // constant-gain block, well under 0.1% of the callback budget). The envelope
    // also changes every frame, so a single block gain would not be byte-identical.
    void scalePulled(uint8_t* dst, int frames)
    {
        if (frames <= 0) {
            return;
        }
        const float target = session.queued ? 1.f : volumeCurve(std::clamp(volume.load(), 0.f, 1.f));
        const int channels = std::max(1, session.device.channels);
        const int ramp = std::max(1, session.device.sampleRate / kVolumeRampDivisor);
        const auto kind = static_cast<OutputFade>(fader.kind.load());
        const int total = fader.total.load();
        const int done = fader.done.load();
        const float start = fader.start.load();
        float lastLevel = fader.level.load();
        const int sampleBytes = std::max(1, session.device.containerBytes);
        for (int frame = 0; frame < frames; ++frame) {
            fader.smoothedVolume += (target - fader.smoothedVolume) * std::min(1.f, 1.f / static_cast<float>(ramp));
            if (std::fabs(target - fader.smoothedVolume) < 0.0000001f) fader.smoothedVolume = target;
            float envelope = fader.smoothedVolume;
            if (!session.queued && kind != OutputFade::None && total > 0) {
                const double progress = std::clamp(static_cast<double>(done + frame) / static_cast<double>(total), 0.0, 1.0);
                const float end = kind == OutputFade::In ? 1.f : 0.f;
                lastLevel = start + (end - start) * static_cast<float>(progress);
                envelope *= lastLevel;
            }
            if (std::fabs(envelope - 1.f) <= 0.0001f) {
                continue;
            }
            uint8_t* sample = dst + static_cast<size_t>(frame) * static_cast<size_t>(session.device.bytesPerFrame());
            SampleFormat source;
            source.sampleRate = session.device.sampleRate;
            source.channels = channels;
            source.bitsPerSample = session.device.bitsPerSample;
            source.bytesPerSample = sampleBytes;
            source.isFloat = session.device.isFloat;
            source.highAligned = session.device.highAligned;
            source.channelMask = session.device.channelMask;
            // Quantize at the actual device depth, including packed 24-bit and aligned containers.
            convertSamples(sample, source, 1, sample, session.device, envelope, &outputDither);
        }
        if (!session.queued && kind != OutputFade::None) {
            fader.level.store(lastLevel);
            fader.done.store(done + frames);
        }
    }

    int pull(uint8_t* dst, int frames)
    {
        const int bytes = frames * session.device.bytesPerFrame();
        const int got = ring.read(dst, bytes);
        const int frameBytes = std::max(1, session.device.bytesPerFrame());
        const int whole = got / frameBytes;
        scalePulled(dst, whole);
        framesPlayed.fetch_add(whole);
        return whole;
    }

    void publishPosition()
    {
        const qint64 played = framesPlayed.load();
        const qint64 heard = std::max<qint64>(0, played - output->pendingFrames() - session.device.latencyFrames);
        Marker active;
        bool found = false;
        for (const auto& marker : markers) {
            if (marker.queuedFrame <= heard) {
                active = marker;
                found = true;
            }
        }
        // Retain only the audible marker and its successors during long-running playback.
        while (markers.size() > 1 && markers[1].queuedFrame <= heard) markers.erase(markers.begin());
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
        session.ready = false;
    }
};

AudioEngine::AudioEngine(QObject* parent)
    : AudioEngine(createAudioOutput(), parent)
{
}

AudioEngine::AudioEngine(std::unique_ptr<AudioOutput> output, QObject* parent)
    : QObject(parent)
{
    m_worker = std::make_unique<Worker>();
    m_worker->output = std::move(output);
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
    m_worker.reset();
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

void AudioEngine::releaseFiles()
{
    m_worker->post(Worker::ReleaseFiles);
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
    m_worker->post(Worker::Volume);
}

void AudioEngine::setFadingEnabled(bool enabled)
{
    { QMutexLocker locker(&m_worker->mutex); m_worker->pendingFading = enabled; }
    m_worker->post(Worker::Fading);
}

void AudioEngine::setExclusive(bool exclusive)
{
    m_worker->exclusive.store(exclusive);
    if (exclusive) m_worker->spatial.store(false);
    m_worker->post(Worker::ConfigureOutput);
}

void AudioEngine::setSpatialEnabled(bool enabled)
{
    m_worker->spatial.store(enabled);
    if (enabled) m_worker->exclusive.store(false);
    m_worker->post(Worker::ConfigureOutput);
}

void AudioEngine::setTrackGain(qint64 token, float gainDb)
{
    setTrackGains({{token, gainDb}});
}

void AudioEngine::setTrackGains(const QHash<qint64, float>& gains)
{
    if (gains.isEmpty()) return;
    QMutexLocker locker(&m_worker->mutex);
    for (auto it = gains.cbegin(); it != gains.cend(); ++it) m_worker->pendingGains.insert(it.key(), it.value());
    if (std::find(m_worker->commands.begin(), m_worker->commands.end(), Worker::Gain) == m_worker->commands.end())
        m_worker->commands.push_back(Worker::Gain);
    m_worker->wait.wakeAll();
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
