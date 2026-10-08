#include "audio/output.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

namespace echo {
namespace {

AudioDeviceID defaultOutputDevice()
{
    AudioDeviceID device = kAudioObjectUnknown;
    AudioObjectPropertyAddress address {kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr) {
        return kAudioObjectUnknown;
    }
    return device;
}

bool setHog(AudioDeviceID device, bool take)
{
    pid_t owner = take ? getpid() : static_cast<pid_t>(-1);
    AudioObjectPropertyAddress address {kAudioDevicePropertyHogMode, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    return AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(owner), &owner) == noErr;
}

double nominalRate(AudioDeviceID device)
{
    Float64 rate = 0;
    AudioObjectPropertyAddress address {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(rate);
    AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate);
    return rate;
}

bool setNominalRate(AudioDeviceID device, double rate)
{
    AudioObjectPropertyAddress address {kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    const Float64 value = rate;
    if (AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(value), &value) != noErr) {
        return false;
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (std::fabs(nominalRate(device) - rate) < 1.0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return std::fabs(nominalRate(device) - rate) < 1.0;
}

AudioStreamID outputStream(AudioDeviceID device)
{
    AudioObjectPropertyAddress address {kAudioDevicePropertyStreams, kAudioDevicePropertyScopeOutput,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr || size < sizeof(AudioStreamID)) {
        return kAudioObjectUnknown;
    }
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, streams.data()) != noErr || streams.empty()) {
        return kAudioObjectUnknown;
    }
    return streams.front();
}

bool readPhysicalFormat(AudioStreamID stream, AudioStreamBasicDescription* format)
{
    AudioObjectPropertyAddress address {kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(*format);
    return AudioObjectGetPropertyData(stream, &address, 0, nullptr, &size, format) == noErr;
}

bool writePhysicalFormat(AudioStreamID stream, const AudioStreamBasicDescription& format)
{
    AudioObjectPropertyAddress address {kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    return AudioObjectSetPropertyData(stream, &address, 0, nullptr, sizeof(format), &format) == noErr;
}

int scoreFormat(const AudioStreamBasicDescription& format, const AudioValueRange& range, const SampleFormat& source)
{
    if (format.mFormatID != kAudioFormatLinearPCM || format.mChannelsPerFrame == 0) {
        return -1;
    }
    if ((format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0) {
        return -1;
    }
    const double rate = format.mSampleRate > 0 ? format.mSampleRate : source.sampleRate;
    const bool rateFits = std::fabs(rate - source.sampleRate) < 1.0
        || (range.mMinimum <= source.sampleRate && source.sampleRate <= range.mMaximum);
    if (!rateFits) {
        return -1;
    }
    const bool isFloat = (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    int score = 0;
    if (format.mChannelsPerFrame == static_cast<UInt32>(source.channels)) {
        score += 1000;
    } else if (format.mChannelsPerFrame == 2 && source.channels >= 2) {
        score += 100;
    } else {
        return -1;
    }
    score += isFloat == source.isFloat ? 200 : 0;
    score -= std::abs(static_cast<int>(format.mBitsPerChannel) - source.bitsPerSample);
    if ((format.mFormatFlags & kAudioFormatFlagIsPacked) != 0) {
        score += 30;
    }
    return score;
}

bool choosePhysicalFormat(AudioStreamID stream, const SampleFormat& source, AudioStreamBasicDescription* chosen)
{
    AudioObjectPropertyAddress address {kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(stream, &address, 0, nullptr, &size) != noErr || size == 0) {
        return false;
    }
    std::vector<AudioStreamRangedDescription> formats(size / sizeof(AudioStreamRangedDescription));
    if (AudioObjectGetPropertyData(stream, &address, 0, nullptr, &size, formats.data()) != noErr) {
        return false;
    }
    int best = -1;
    AudioStreamBasicDescription match {};
    for (const auto& entry : formats) {
        const int score = scoreFormat(entry.mFormat, entry.mSampleRateRange, source);
        if (score > best) {
            best = score;
            match = entry.mFormat;
            match.mSampleRate = source.sampleRate;
        }
    }
    if (best < 0) {
        return false;
    }
    *chosen = match;
    return true;
}

UInt32 clampBufferFrames(AudioDeviceID device)
{
    AudioObjectPropertyAddress address {kAudioDevicePropertyBufferFrameSizeRange, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    AudioValueRange range {128, 4096};
    UInt32 size = sizeof(range);
    AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &range);
    UInt32 frames = 512;
    if (frames < range.mMinimum) {
        frames = static_cast<UInt32>(range.mMinimum);
    }
    if (frames > range.mMaximum) {
        frames = static_cast<UInt32>(range.mMaximum);
    }
    AudioObjectPropertyAddress setAddress {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                            kAudioObjectPropertyElementMain};
    AudioObjectSetPropertyData(device, &setAddress, 0, nullptr, sizeof(frames), &frames);
    UInt32 actual = frames;
    size = sizeof(actual);
    AudioObjectGetPropertyData(device, &setAddress, 0, nullptr, &size, &actual);
    return actual > 0 ? actual : frames;
}

DeviceFormat deviceFormatFromStream(const AudioStreamBasicDescription& stream, bool exclusive)
{
    DeviceFormat format;
    format.sampleRate = static_cast<int>(std::llround(stream.mSampleRate));
    format.channels = static_cast<int>(stream.mChannelsPerFrame);
    format.exclusive = exclusive;
    format.isFloat = (stream.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    format.bitsPerSample = static_cast<int>(stream.mBitsPerChannel);
    format.containerBytes = stream.mChannelsPerFrame > 0 ? static_cast<int>(stream.mBytesPerFrame / stream.mChannelsPerFrame) : 0;
    format.highAligned = (stream.mFormatFlags & kAudioFormatFlagIsAlignedHigh) != 0;
    if (format.bitsPerSample <= 0 && format.containerBytes > 0) {
        format.bitsPerSample = format.containerBytes * 8;
    }
    return format;
}

class CoreAudioOutput final : public AudioOutput {
public:
    ~CoreAudioOutput() override { close(); }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        close();
        m_device = defaultOutputDevice();
        if (m_device == kAudioObjectUnknown) {
            if (error) {
                *error = QStringLiteral("找不到输出设备");
            }
            return false;
        }
        m_savedRate = nominalRate(m_device);
        m_stream = outputStream(m_device);
        m_haveSavedFormat = m_stream != kAudioObjectUnknown && readPhysicalFormat(m_stream, &m_savedFormat);
        if (exclusive && openExclusive(source, error)) {
            if (opened) {
                *opened = m_opened;
            }
            return true;
        }
        if (exclusive && m_hogged) {
            setHog(m_device, false);
            m_hogged = false;
        }
        if (m_haveSavedFormat && m_stream != kAudioObjectUnknown) {
            writePhysicalFormat(m_stream, m_savedFormat);
        }
        if (std::fabs(nominalRate(m_device) - m_savedRate) > 1.0) {
            setNominalRate(m_device, m_savedRate);
        }
        DeviceFormat shared;
        shared.sampleRate = static_cast<int>(std::llround(nominalRate(m_device)));
        shared.channels = std::clamp(source.channels, 1, 8);
        shared.isFloat = true;
        shared.bitsPerSample = 32;
        shared.containerBytes = 4;
        shared.exclusive = false;
        QString failure;
        if (!openUnit(shared, &failure)) {
            if (error) {
                *error = failure.isEmpty() ? QStringLiteral("无法打开 Core Audio") : failure;
            }
            close();
            return false;
        }
        m_opened = shared;
        m_opened.latencyFrames = static_cast<int>(clampBufferFrames(m_device));
        m_scratch.assign(static_cast<size_t>(std::max(m_opened.latencyFrames, 4096)) * static_cast<size_t>(m_opened.bytesPerFrame()), 0);
        if (opened) {
            *opened = m_opened;
        }
        return true;
    }

    void close() override
    {
        pause();
        if (m_unit) {
            AudioUnitUninitialize(m_unit);
            AudioComponentInstanceDispose(m_unit);
            m_unit = nullptr;
        }
        if (m_ioproc) {
            AudioDeviceDestroyIOProcID(m_device, m_ioproc);
            m_ioproc = nullptr;
        }
        if (m_device != kAudioObjectUnknown) {
            if (m_haveSavedFormat && m_stream != kAudioObjectUnknown) {
                writePhysicalFormat(m_stream, m_savedFormat);
            }
            if (m_hogged) {
                setHog(m_device, false);
                m_hogged = false;
            }
            if (m_savedRate > 0 && std::fabs(nominalRate(m_device) - m_savedRate) > 1.0) {
                setNominalRate(m_device, m_savedRate);
            }
        }
        m_device = kAudioObjectUnknown;
        m_stream = kAudioObjectUnknown;
        m_savedRate = 0;
        m_haveSavedFormat = false;
        m_opened = {};
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (m_ioproc) {
            AudioDeviceStart(m_device, m_ioproc);
            return;
        }
        if (m_unit) {
            AudioOutputUnitStart(m_unit);
        }
    }

    void pause() override
    {
        if (m_ioproc && m_device != kAudioObjectUnknown) {
            AudioDeviceStop(m_device, m_ioproc);
        }
        if (m_unit) {
            AudioOutputUnitStop(m_unit);
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_unit != nullptr || m_ioproc != nullptr; }

private:
    bool openExclusive(const SampleFormat& source, QString* error)
    {
        m_hogged = setHog(m_device, true);
        if (!m_hogged || m_stream == kAudioObjectUnknown) {
            return false;
        }
        if (std::fabs(m_savedRate - source.sampleRate) > 1.0 && !setNominalRate(m_device, source.sampleRate)) {
            return false;
        }
        AudioStreamBasicDescription wanted {};
        if (!choosePhysicalFormat(m_stream, source, &wanted) || !writePhysicalFormat(m_stream, wanted)) {
            return false;
        }
        AudioStreamBasicDescription actual {};
        if (!readPhysicalFormat(m_stream, &actual)) {
            return false;
        }
        m_opened = deviceFormatFromStream(actual, true);
        if (!m_opened.valid()) {
            return false;
        }
        m_opened.latencyFrames = static_cast<int>(clampBufferFrames(m_device));
        m_opened.exclusive = true;
        const int scratchFrames = std::max(m_opened.latencyFrames, 4096);
        m_scratch.assign(static_cast<size_t>(scratchFrames) * static_cast<size_t>(std::max(m_opened.bytesPerFrame(), 1)), 0);
        if (AudioDeviceCreateIOProcID(m_device, &CoreAudioOutput::ioProc, this, &m_ioproc) != noErr) {
            m_ioproc = nullptr;
            if (error) {
                *error = QStringLiteral("无法创建 Core Audio 回调");
            }
            return false;
        }
        return true;
    }

    bool openUnit(const DeviceFormat& format, QString* error)
    {
        AudioComponentDescription description {};
        description.componentType = kAudioUnitType_Output;
        description.componentSubType = kAudioUnitSubType_HALOutput;
        description.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &description);
        if (!component || AudioComponentInstanceNew(component, &m_unit) != noErr) {
            if (error) {
                *error = QStringLiteral("无法创建音频单元");
            }
            return false;
        }
        UInt32 enable = 1;
        UInt32 disable = 0;
        AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enable, sizeof(enable));
        AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &disable, sizeof(disable));
        if (AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &m_device, sizeof(m_device))
            != noErr) {
            if (error) {
                *error = QStringLiteral("无法绑定输出设备");
            }
            return false;
        }
        AudioStreamBasicDescription stream {};
        stream.mSampleRate = format.sampleRate;
        stream.mFormatID = kAudioFormatLinearPCM;
        stream.mFramesPerPacket = 1;
        stream.mChannelsPerFrame = static_cast<UInt32>(format.channels);
        stream.mBitsPerChannel = static_cast<UInt32>(format.containerBytes * 8);
        stream.mBytesPerFrame = static_cast<UInt32>(format.bytesPerFrame());
        stream.mBytesPerPacket = stream.mBytesPerFrame;
        stream.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        if (AudioUnitSetProperty(m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &stream, sizeof(stream)) != noErr) {
            if (error) {
                *error = QStringLiteral("设备不接受该音频格式");
            }
            return false;
        }
        AURenderCallbackStruct callback {};
        callback.inputProc = &CoreAudioOutput::render;
        callback.inputProcRefCon = this;
        AudioUnitSetProperty(m_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof(callback));
        if (AudioUnitInitialize(m_unit) != noErr) {
            if (error) {
                *error = QStringLiteral("音频单元初始化失败");
            }
            return false;
        }
        return true;
    }

    void renderBuffers(AudioBufferList* data, UInt32 frames)
    {
        if (!data || frames == 0) {
            return;
        }
        const int frameBytes = std::max(1, m_opened.bytesPerFrame());
        const int requested = static_cast<int>(frames);
        if (data->mNumberBuffers <= 1 && data->mBuffers[0].mData) {
            auto* buffer = static_cast<uint8_t*>(data->mBuffers[0].mData);
            int received = 0;
            if (m_pull) {
                received = m_pull(buffer, requested);
            }
            if (received < requested) {
                std::memset(buffer + std::max(received, 0) * frameBytes, 0,
                            static_cast<size_t>(requested - std::max(received, 0)) * static_cast<size_t>(frameBytes));
            }
            data->mBuffers[0].mDataByteSize = frames * static_cast<UInt32>(frameBytes);
            return;
        }
        if (m_scratch.size() < static_cast<size_t>(requested) * static_cast<size_t>(frameBytes)) {
            for (UInt32 bufferIndex = 0; bufferIndex < data->mNumberBuffers; ++bufferIndex) {
                if (data->mBuffers[bufferIndex].mData) {
                    std::memset(data->mBuffers[bufferIndex].mData, 0, data->mBuffers[bufferIndex].mDataByteSize);
                }
            }
            return;
        }
        int received = 0;
        if (m_pull) {
            received = m_pull(m_scratch.data(), requested);
        }
        if (received < requested) {
            std::memset(m_scratch.data() + std::max(received, 0) * frameBytes, 0,
                        static_cast<size_t>(requested - std::max(received, 0)) * static_cast<size_t>(frameBytes));
        }
        const int channels = std::max(1, m_opened.channels);
        const int container = std::max(1, m_opened.containerBytes);
        for (UInt32 bufferIndex = 0; bufferIndex < data->mNumberBuffers; ++bufferIndex) {
            auto* destination = static_cast<uint8_t*>(data->mBuffers[bufferIndex].mData);
            if (!destination) {
                continue;
            }
            for (int frame = 0; frame < requested; ++frame) {
                const int channel = static_cast<int>(bufferIndex) < channels ? static_cast<int>(bufferIndex) : 0;
                std::memcpy(destination + static_cast<size_t>(frame) * static_cast<size_t>(container),
                            m_scratch.data() + static_cast<size_t>(frame * frameBytes + channel * container),
                            static_cast<size_t>(container));
            }
            data->mBuffers[bufferIndex].mDataByteSize = frames * static_cast<UInt32>(container);
        }
    }

    static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*, const AudioBufferList*, const AudioTimeStamp*,
                           AudioBufferList* output, const AudioTimeStamp*, void* client)
    {
        auto* self = static_cast<CoreAudioOutput*>(client);
        UInt32 frames = 0;
        if (output && output->mNumberBuffers > 0 && self->m_opened.containerBytes > 0) {
            const int stride = output->mNumberBuffers == 1 ? self->m_opened.bytesPerFrame() : self->m_opened.containerBytes;
            if (stride > 0 && output->mBuffers[0].mDataByteSize > 0) {
                frames = output->mBuffers[0].mDataByteSize / static_cast<UInt32>(stride);
            }
        }
        if (frames == 0) {
            frames = static_cast<UInt32>(std::max(self->m_opened.latencyFrames, 0));
        }
        self->renderBuffers(output, frames);
        return noErr;
    }

    static OSStatus render(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                           AudioBufferList* data)
    {
        static_cast<CoreAudioOutput*>(refCon)->renderBuffers(data, frames);
        return noErr;
    }

    AudioUnit m_unit = nullptr;
    AudioDeviceIOProcID m_ioproc = nullptr;
    AudioDeviceID m_device = kAudioObjectUnknown;
    AudioStreamID m_stream = kAudioObjectUnknown;
    AudioStreamBasicDescription m_savedFormat {};
    double m_savedRate = 0;
    bool m_hogged = false;
    bool m_haveSavedFormat = false;
    DeviceFormat m_opened;
    Pull m_pull;
    std::vector<uint8_t> m_scratch;
};

} // namespace

std::unique_ptr<AudioOutput> createCoreAudioOutput()
{
    return std::make_unique<CoreAudioOutput>();
}

} // namespace echo
