#include "audio/engine.h"

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
#include <cmath>
#include <deque>
#include <utility>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
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
    qint64 framesQueued = 0;
    std::atomic<qint64> framesPlayed {0};
    std::vector<Marker> markers;
    QThread* thread = nullptr;

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
            if (output) {
                output->pause();
            }
            setState(State::Paused);
            break;
        case Resume:
            if (deviceReady && output) {
                output->start();
                setState(State::Playing);
            }
            break;
        case Stop:
            stopPlayback();
            break;
        case Seek:
            seekCurrent();
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
        output->start();
        setState(State::Playing);
        emit owner->trackStarted(current.item.token);
    }

    void stopPlayback()
    {
        if (output && output->isOpen()) {
            output->pause();
        }
        ring.clear();
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

    bool ensureDevice(const SampleFormat& source)
    {
        if (deviceReady && device.sampleRate == source.sampleRate && device.channels == source.channels
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
        emit owner->deviceOpened(device.sampleRate, device.bitsPerSample, device.exclusive);
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
        const AVSampleFormat format = source.isFloat ? AV_SAMPLE_FMT_FLT
                                                      : (source.bytesPerSample == 2 ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S32);
        swr_alloc_set_opts2(&resampler, &outLayout, format, device.sampleRate, &inLayout, format, source.sampleRate, 0, nullptr);
        if (!resampler || swr_init(resampler) < 0) {
            freeResampler();
        }
        resampleRate = device.sampleRate;
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
        markers.push_back(Marker {framesQueued, slot.item.token, slot.item.startMs, slot.format.sampleRate});
        owner->m_activeToken.store(slot.item.token);
    }

    void pump()
    {
        if (!current.decoder) {
            stopPlayback();
            emit owner->finished();
            return;
        }
        if (wantExclusiveReopen) {
            const auto source = current.format;
            ring.clear();
            ensureDevice(source);
            if (owner->m_state.load() == static_cast<int>(State::Playing) && output) {
                output->start();
            }
        }
        const int frameBytes = std::max(1, device.bytesPerFrame());
        const int low = device.sampleRate > 0 ? device.sampleRate / 5 * frameBytes : 16384;
        if (ring.availableRead() > low * 3) {
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
        std::vector<uint8_t> raw(static_cast<size_t>(2048 * std::max(current.format.bytesPerFrame(), 4)));
        const int read = current.decoder->read(raw.data(), 2048);
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

    void writeFrames(const uint8_t* src, int frames, const SampleFormat& format, float gainDb)
    {
        float gain = volumeCurve(volume.load()) * static_cast<float>(std::pow(10.0, static_cast<double>(gainDb) / 20.0));
        if (!std::isfinite(gain) || gain < 0.f) {
            gain = 0.f;
        }
        const uint8_t* pcm = src;
        int count = frames;
        SampleFormat working = format;
        std::vector<uint8_t> resampled;
        if (resampler && format.sampleRate != device.sampleRate) {
            const int outEstimate = static_cast<int>(av_rescale_rnd(frames, device.sampleRate, format.sampleRate, AV_ROUND_UP)) + 32;
            resampled.resize(static_cast<size_t>(outEstimate) * static_cast<size_t>(format.bytesPerFrame()));
            uint8_t* output[] = {resampled.data()};
            const uint8_t* input[] = {src};
            const int got = swr_convert(resampler, output, outEstimate, input, frames);
            if (got > 0) {
                pcm = resampled.data();
                count = got;
                working.sampleRate = device.sampleRate;
            }
        }
        std::vector<uint8_t> converted(static_cast<size_t>(count) * static_cast<size_t>(device.bytesPerFrame()));
        convertSamples(pcm, working, count, converted.data(), device, gain, &dither);
        int offset = 0;
        const int bytes = static_cast<int>(converted.size());
        while (offset < bytes) {
            const int wrote = ring.write(converted.data() + offset, bytes - offset);
            if (wrote <= 0) {
                QMutexLocker locker(&mutex);
                wait.wait(&mutex, 4);
                continue;
            }
            offset += wrote;
        }
        framesQueued += count;
    }

    void advance(bool failed)
    {
        if (!failed && upcoming.decoder && upcoming.format.sampleRate == current.format.sampleRate
            && upcoming.format.channels == current.format.channels && upcoming.format.isFloat == current.format.isFloat
            && upcoming.format.bitsPerSample == current.format.bitsPerSample) {
            current = std::move(upcoming);
            upcoming = {};
            pushMarker(current);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        if (upcoming.decoder) {
            drain();
            current = std::move(upcoming);
            upcoming = {};
            if (!ensureDevice(current.format)) {
                emit owner->finished();
                return;
            }
            output->setPull([this](uint8_t* dst, int frames) { return pull(dst, frames); });
            output->start();
            pushMarker(current);
            emit owner->trackStarted(current.item.token);
            loadUpcoming();
            return;
        }
        drain();
        stopPlayback();
        emit owner->finished();
    }

    void drain()
    {
        const int guard = 0;
        Q_UNUSED(guard);
        for (int i = 0; i < 400; ++i) {
            if (framesPlayed.load() + device.latencyFrames >= framesQueued) {
                break;
            }
            QThread::msleep(5);
        }
        ring.clear();
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
        current.decoder->setRange(frame, current.item.endMs >= 0 ? current.item.endMs * current.format.sampleRate / 1000 : -1);
        framesPlayed.store(0);
        framesQueued = 0;
        markers.clear();
        Marker marker;
        marker.queuedFrame = 0;
        marker.token = current.item.token;
        marker.startMs = current.item.startMs + position;
        marker.sampleRate = current.format.sampleRate;
        markers.push_back(marker);
        if (wasPlaying && output && deviceReady) {
            output->start();
        }
    }

    int pull(uint8_t* dst, int frames)
    {
        const int bytes = frames * device.bytesPerFrame();
        const int got = ring.read(dst, bytes);
        const int frameBytes = std::max(1, device.bytesPerFrame());
        framesPlayed.fetch_add(got / frameBytes);
        return got / frameBytes;
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
