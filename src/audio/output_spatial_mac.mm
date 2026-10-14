#include "audio/output.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <vector>
#include <utility>
#include <QLoggingCategory>

#ifdef slots
#undef slots
#endif
#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMedia/CoreMedia.h>

namespace echo {
namespace {

Q_LOGGING_CATEGORY(spatialLog, "echo.spatial", QtWarningMsg)

// FFmpeg's native channel mask is ordered by bit number. Preserve that order
// explicitly instead of giving multichannel PCM an ambiguous channel count.
std::vector<uint8_t> channelLayout(const SampleFormat& source)
{
    const size_t bytes = offsetof(AudioChannelLayout, mChannelDescriptions) + source.channels * sizeof(AudioChannelDescription);
    std::vector<uint8_t> storage(bytes, 0);
    auto* layout = reinterpret_cast<AudioChannelLayout*>(storage.data());
    if (source.channels <= 2) {
        layout->mChannelLayoutTag = source.channels == 1 ? kAudioChannelLayoutTag_Mono : kAudioChannelLayoutTag_Stereo;
        return storage;
    }
    if (!source.channelMask) return {};
    constexpr AudioChannelLabel labels[] = {
        kAudioChannelLabel_Left, kAudioChannelLabel_Right, kAudioChannelLabel_Center, kAudioChannelLabel_LFEScreen,
        kAudioChannelLabel_RearSurroundLeft, kAudioChannelLabel_RearSurroundRight,
        kAudioChannelLabel_LeftCenter, kAudioChannelLabel_RightCenter, kAudioChannelLabel_CenterSurround,
        kAudioChannelLabel_LeftSurround, kAudioChannelLabel_RightSurround,
        kAudioChannelLabel_TopCenterSurround, kAudioChannelLabel_VerticalHeightLeft,
        kAudioChannelLabel_VerticalHeightCenter, kAudioChannelLabel_VerticalHeightRight,
        kAudioChannelLabel_TopBackLeft, kAudioChannelLabel_TopBackCenter, kAudioChannelLabel_TopBackRight
    };
    int count = 0;
    for (int bit = 0; bit < 64; ++bit) {
        if (!(source.channelMask & (quint64(1) << bit))) continue;
        if (bit >= static_cast<int>(std::size(labels)) || count >= source.channels) return {};
        layout->mChannelDescriptions[count++].mChannelLabel = labels[bit];
    }
    if (count != source.channels) return {};
    layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
    layout->mNumberChannelDescriptions = count;
    return storage;
}

class SpatialAudioOutput final : public AudioOutput {
public:
    ~SpatialAudioOutput() override { close(); }

    bool open(const SampleFormat& source, bool, DeviceFormat* opened, QString* error) override
    {
        close();
        @autoreleasepool {
            if (@available(macOS 12.0, *)) {
                if (!source.valid() || source.channels > 8) {
                    if (error) *error = QStringLiteral("系统空间音频不支持此音频格式。");
                    return false;
                }
                auto layout = channelLayout(source);
                if (layout.empty()) {
                    if (error) *error = QStringLiteral("无法识别此音频的声道布局。");
                    return false;
                }
                m_rate = source.sampleRate;
                m_channels = source.channels;
                AudioStreamBasicDescription stream {};
                stream.mSampleRate = m_rate;
                stream.mFormatID = kAudioFormatLinearPCM;
                stream.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
                stream.mBitsPerChannel = 32;
                stream.mChannelsPerFrame = m_channels;
                stream.mFramesPerPacket = 1;
                stream.mBytesPerFrame = m_channels * sizeof(float);
                stream.mBytesPerPacket = stream.mBytesPerFrame;
                const OSStatus result = CMAudioFormatDescriptionCreate(kCFAllocatorDefault, &stream, layout.size(),
                    reinterpret_cast<const AudioChannelLayout*>(layout.data()), 0, nullptr, nullptr, &m_description);
                if (result != noErr) {
                    if (error) *error = QStringLiteral("创建系统音频格式失败（%1）。").arg(result);
                    return false;
                }
                m_renderer = [AVSampleBufferAudioRenderer new];
                m_renderer.allowedAudioSpatializationFormats = AVAudioSpatializationFormatMonoStereoAndMultichannel;
                // Leave audioOutputDeviceUniqueID nil: follow the system's route, including AirPods switching.
                m_sync = [AVSampleBufferRenderSynchronizer new];
                m_sync.delaysRateChangeUntilHasSufficientMediaData = YES;
                [m_sync addRenderer:m_renderer];
                [m_sync setRate:0 time:kCMTimeZero];
                m_scratch.resize(std::max(1, m_rate / 100) * m_channels);
                m_failure.clear();
                m_nextFrame = 0;
                m_clockStarted = false;
                m_starved = false;
                m_endOfInput = false;
                m_prerollReached = false;
                m_maximumBufferedFrames = 0;
                m_sourcePeak = 0;
                *opened = {m_rate, m_channels, 32, 4, true, false, false, 0};
                return true;
            }
            if (error) *error = QStringLiteral("空间音频需要 macOS 12 或更新版本。");
            return false;
        }
    }

    void close() override
    {
        pause();
        @autoreleasepool {
            std::lock_guard lock(m_mutex);
            [m_renderer flush];
            m_renderer = nil;
            m_sync = nil;
            if (m_description) CFRelease(m_description);
            m_description = nullptr;
            m_nextFrame = 0;
        }
    }

    void setPull(Pull pull) override { std::lock_guard lock(m_mutex); m_pull = std::move(pull); }

    void start() override
    {
        pause();
        if (!m_renderer) return;
        m_running = true;
        m_thread = std::thread([this] {
            auto lastLog = std::chrono::steady_clock::now();
            while (m_running.load()) {
                @autoreleasepool {
                    std::lock_guard lock(m_mutex);
                    if (m_renderer.status == AVQueuedSampleBufferRenderingStatusFailed) {
                        m_failure = QString::fromNSString(m_renderer.error.localizedDescription ?: @"系统音频渲染失败");
                        m_running = false;
                        break;
                    }
                    const auto wallTime = std::chrono::steady_clock::now();
                    if (wallTime - lastLog >= std::chrono::milliseconds(500)) {
                        qCDebug(spatialLog) << "clock" << clockFrame() << "queued" << m_nextFrame
                            << "ready" << bool(m_renderer.readyForMoreMediaData)
                            << "preroll" << bool(m_renderer.hasSufficientMediaDataForReliablePlaybackStart)
                            << "rate" << m_sync.rate << "volume" << m_renderer.volume << "muted" << bool(m_renderer.muted);
                        lastLog = wallTime;
                    }
                    qint64 now = m_starved ? m_resumeFrame : clockFrame();
                    if (m_clockStarted && !m_starved && now >= m_nextFrame) {
                        [m_sync setRate:0 time:CMTimeMake(m_nextFrame, m_rate)];
                        now = m_nextFrame;
                        m_resumeFrame = now;
                        m_starved = true;
                    }
                    const bool needData = !m_clockStarted || m_starved || m_nextFrame - now < m_rate * 2;
                    int got = -1;
                    if (needData && m_renderer.readyForMoreMediaData && m_pull) {
                        got = m_pull(reinterpret_cast<uint8_t*>(m_scratch.data()), m_scratch.size() / m_channels);
                        if (got > 0) enqueue(got);
                    }
                    m_prerollReached |= bool(m_renderer.hasSufficientMediaDataForReliablePlaybackStart);
                    if ((!m_clockStarted || m_starved) && m_nextFrame > now
                        && (m_renderer.hasSufficientMediaDataForReliablePlaybackStart || m_endOfInput)) {
                        // Only a confirmed end of input may bypass preroll (very short tracks).
                        m_sync.delaysRateChangeUntilHasSufficientMediaData = !m_endOfInput;
                        [m_sync setRate:1 time:CMTimeMake(now, m_rate)];
                        m_clockStarted = true;
                        m_starved = false;
                    } else if (m_clockStarted && !m_starved && now >= m_nextFrame && got == 0) {
                        [m_sync setRate:0 time:CMTimeMake(m_nextFrame, m_rate)];
                        m_resumeFrame = m_nextFrame;
                        m_starved = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    void pause() override
    {
        stopFeeding();
        @autoreleasepool {
            std::lock_guard lock(m_mutex);
            m_resumeFrame = m_starved ? m_resumeFrame : std::min(clockFrame(), m_nextFrame);
            if (m_sync) [m_sync setRate:0 time:CMTimeMake(m_resumeFrame, std::max(m_rate, 1))];
            // Resume starts the same queued timeline, never a fresh zero timestamp.
            m_starved = true;
        }
    }

    void flush() override
    {
        @autoreleasepool {
            std::lock_guard lock(m_mutex);
            [m_sync setRate:0 time:kCMTimeZero];
            [m_renderer flush];
            m_nextFrame = 0;
            m_resumeFrame = 0;
            m_clockStarted = false;
            m_starved = false;
            m_endOfInput = false;
            m_prerollReached = false;
            m_maximumBufferedFrames = 0;
            m_sourcePeak = 0;
        }
    }

    void drain() override
    {
        stopFeeding();
        // Fades count frames supplied to the renderer. Let their queued tail become audible before pausing.
        for (int attempt = 0; attempt < 1500 && pendingFrames() > 0; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    qint64 pendingFrames() const override
    {
        @autoreleasepool {
            std::lock_guard lock(m_mutex);
            return m_sync ? std::max<qint64>(0, m_nextFrame - (m_starved ? m_resumeFrame : clockFrame())) : 0;
        }
    }
    OutputDiagnostics diagnostics() const override
    {
        @autoreleasepool {
            std::lock_guard lock(m_mutex);
            return {m_nextFrame, std::max<qint64>(0, m_nextFrame - (m_starved ? m_resumeFrame : clockFrame())),
                m_maximumBufferedFrames, m_prerollReached, m_sourcePeak, m_renderer.volume};
        }
    }
    void finishInput() override { std::lock_guard lock(m_mutex); m_endOfInput = true; }
    void setVolume(float volume) override { std::lock_guard lock(m_mutex); m_renderer.volume = std::clamp(volume, 0.f, 1.f); }
    bool queuedPlayback() const override { return true; }
    QString takeError() override { std::lock_guard lock(m_mutex); return std::exchange(m_failure, {}); }
    QString spatialStatus() const override { return QStringLiteral("已接入系统空间音频输出。播放时可在控制中心选择固定或头部跟踪。"); }
    bool isOpen() const override { return m_renderer != nil; }

private:
    void stopFeeding() { m_running = false; if (m_thread.joinable()) m_thread.join(); }
    qint64 clockFrame() const
    {
        const CMTime time = [m_sync currentTime];
        if (!CMTIME_IS_NUMERIC(time)) return 0;
        return std::max<qint64>(0, CMTimeConvertScale(time, m_rate, kCMTimeRoundingMethod_RoundTowardNegativeInfinity).value);
    }
    void enqueue(int frames)
    {
        CMBlockBufferRef block = nullptr;
        CMSampleBufferRef sample = nullptr;
        const size_t bytes = frames * m_channels * sizeof(float);
        OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault,
            nullptr, 0, bytes, 0, &block);
        if (status == noErr) status = CMBlockBufferReplaceDataBytes(m_scratch.data(), block, 0, bytes);
        CMSampleTimingInfo timing {CMTimeMake(1, m_rate), CMTimeMake(m_nextFrame, m_rate), kCMTimeInvalid};
        const size_t frameBytes = m_channels * sizeof(float);
        if (status == noErr) status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, m_description, frames,
            1, &timing, 1, &frameBytes, &sample);
        if (status == noErr) {
            [m_renderer enqueueSampleBuffer:sample];
            m_nextFrame += frames;
            m_maximumBufferedFrames = std::max(m_maximumBufferedFrames, m_nextFrame - (m_starved ? m_resumeFrame : clockFrame()));
            for (int sample = 0; sample < frames * m_channels; ++sample) {
                m_sourcePeak = std::max(m_sourcePeak, std::abs(static_cast<double>(m_scratch[sample])));
            }
        } else {
            m_failure = QStringLiteral("提交系统音频缓冲失败（%1）。").arg(status);
            m_running = false;
        }
        if (sample) CFRelease(sample);
        if (block) CFRelease(block);
    }
    mutable std::mutex m_mutex;
    AVSampleBufferAudioRenderer* m_renderer = nil;
    AVSampleBufferRenderSynchronizer* m_sync = nil;
    CMAudioFormatDescriptionRef m_description = nullptr;
    Pull m_pull;
    int m_rate = 0;
    int m_channels = 0;
    qint64 m_nextFrame = 0;
    qint64 m_resumeFrame = 0;
    bool m_clockStarted = false;
    bool m_starved = false;
    bool m_endOfInput = false;
    bool m_prerollReached = false;
    qint64 m_maximumBufferedFrames = 0;
    double m_sourcePeak = 0;
    std::atomic<bool> m_running {false};
    std::thread m_thread;
    std::vector<float> m_scratch;
    QString m_failure;
};

class MacAudioOutput final : public AudioOutput {
public:
    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        close();
        if (m_requested && !m_failed) {
            m_active = createSpatialAudioOutput();
            if (m_active->open(source, false, opened, &m_reason)) {
                m_native = true;
                m_active->setPull(m_pull);
                return true;
            }
            m_active->close();
            m_failed = true;
        }
        m_active = createCoreAudioOutput();
        m_native = false;
        m_active->setPull(m_pull);
        return m_active->open(source, m_requested ? false : exclusive, opened, error);
    }
    void close() override { if (m_active) m_active->close(); m_active.reset(); m_native = false; }
    void setPull(Pull pull) override { m_pull = std::move(pull); if (m_active) m_active->setPull(m_pull); }
    void start() override { if (m_active) m_active->start(); }
    void pause() override { if (m_active) m_active->pause(); }
    void flush() override { if (m_active) m_active->flush(); }
    void drain() override { if (m_active) m_active->drain(); }
    OutputDiagnostics diagnostics() const override { return m_active ? m_active->diagnostics() : OutputDiagnostics {}; }
    void finishInput() override { if (m_active) m_active->finishInput(); }
    void setVolume(float volume) override { if (m_active) m_active->setVolume(volume); }
    bool queuedPlayback() const override { return m_active && m_active->queuedPlayback(); }
    qint64 pendingFrames() const override { return m_active ? m_active->pendingFrames() : 0; }
    bool isOpen() const override { return m_active && m_active->isOpen(); }
    void setSpatialEnabled(bool enabled) override
    {
        if (m_requested != enabled) { m_failed = false; m_reason.clear(); }
        m_requested = enabled;
    }
    QString spatialStatus() const override
    {
        if (!m_requested) return QStringLiteral("空间音频已关闭。");
        if (m_failed) return QStringLiteral("空间音频不可用，已回退普通输出：%1").arg(m_reason);
        if (m_native) return m_active->spatialStatus();
        return QStringLiteral("已启用。开始播放后，在控制中心的声音面板选择固定或头部跟踪；需要支持的 AirPods 和 Mac。");
    }
    QString takeError() override
    {
        const QString error = m_active ? m_active->takeError() : QString();
        if (!error.isEmpty() && m_native) { m_failed = true; m_reason = error; }
        return error;
    }
private:
    std::unique_ptr<AudioOutput> m_active;
    Pull m_pull;
    bool m_requested = false;
    bool m_native = false;
    bool m_failed = false;
    QString m_reason;
};

} // namespace
std::unique_ptr<AudioOutput> createSpatialAudioOutput() { return std::make_unique<SpatialAudioOutput>(); }
std::unique_ptr<AudioOutput> createMacAudioOutput() { return std::make_unique<MacAudioOutput>(); }
} // namespace echo
