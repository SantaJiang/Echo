#include "audio/output.h"

#include <atomic>
#include <cmath>
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
    return AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(value), &value) == noErr;
}

UInt32 bufferFrameSize(AudioDeviceID device)
{
    UInt32 frames = 512;
    AudioObjectPropertyAddress address {kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
    UInt32 size = sizeof(frames);
    AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &frames);
    return frames > 0 ? frames : 512;
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
        DeviceFormat wanted = deviceFormatFor(source, exclusive);
        if (exclusive) {
            m_hogged = setHog(m_device, true);
            if (!m_hogged) {
                exclusive = false;
                wanted.exclusive = false;
            } else if (std::fabs(m_savedRate - source.sampleRate) > 1.0) {
                if (!setNominalRate(m_device, source.sampleRate)) {
                    exclusive = false;
                    wanted.exclusive = false;
                    wanted.sampleRate = static_cast<int>(std::llround(m_savedRate));
                }
            }
        }
        QString failure;
        if (!openUnit(wanted, &failure)) {
            if (exclusive || m_hogged) {
                if (m_hogged) {
                    setHog(m_device, false);
                    m_hogged = false;
                }
                if (std::fabs(nominalRate(m_device) - m_savedRate) > 1.0) {
                    setNominalRate(m_device, m_savedRate);
                }
                wanted = deviceFormatFor(source, false);
                wanted.sampleRate = static_cast<int>(std::llround(nominalRate(m_device)));
                wanted.isFloat = true;
                wanted.bitsPerSample = 32;
                wanted.containerBytes = 4;
                wanted.exclusive = false;
                if (!openUnit(wanted, &failure)) {
                    if (error) {
                        *error = failure.isEmpty() ? QStringLiteral("无法打开 Core Audio") : failure;
                    }
                    close();
                    return false;
                }
            } else {
                if (error) {
                    *error = failure.isEmpty() ? QStringLiteral("无法打开 Core Audio") : failure;
                }
                close();
                return false;
            }
        }
        m_opened = wanted;
        m_opened.latencyFrames = static_cast<int>(bufferFrameSize(m_device));
        m_opened.exclusive = m_hogged;
        if (opened) {
            *opened = m_opened;
        }
        return true;
    }

    void close() override
    {
        if (m_unit) {
            AudioOutputUnitStop(m_unit);
            AudioUnitUninitialize(m_unit);
            AudioComponentInstanceDispose(m_unit);
            m_unit = nullptr;
        }
        if (m_device != kAudioObjectUnknown) {
            if (m_hogged) {
                setHog(m_device, false);
                m_hogged = false;
            }
            if (m_savedRate > 0 && std::fabs(nominalRate(m_device) - m_savedRate) > 1.0) {
                setNominalRate(m_device, m_savedRate);
            }
        }
        m_device = kAudioObjectUnknown;
        m_savedRate = 0;
        m_opened = {};
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (m_unit) {
            AudioOutputUnitStart(m_unit);
        }
    }

    void pause() override
    {
        if (m_unit) {
            AudioOutputUnitStop(m_unit);
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_unit != nullptr; }

private:
    static DeviceFormat deviceFormatFor(const SampleFormat& source, bool exclusive)
    {
        DeviceFormat format;
        format.sampleRate = source.sampleRate;
        format.channels = source.channels;
        format.exclusive = exclusive;
        if (source.isFloat) {
            format.isFloat = true;
            format.bitsPerSample = 32;
            format.containerBytes = 4;
        } else if (source.bitsPerSample <= 16) {
            format.bitsPerSample = 16;
            format.containerBytes = 2;
        } else if (source.bitsPerSample <= 24) {
            format.bitsPerSample = 24;
            format.containerBytes = 3;
        } else {
            format.bitsPerSample = 32;
            format.containerBytes = 4;
        }
        return format;
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
        if (AudioUnitSetProperty(m_unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &m_device,
                                 sizeof(m_device))
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
        stream.mFormatFlags = format.isFloat ? (kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked)
                                              : (kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked);
        if (AudioUnitSetProperty(m_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &stream, sizeof(stream))
            != noErr) {
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

    static OSStatus render(void* refCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                           AudioBufferList* data)
    {
        auto* self = static_cast<CoreAudioOutput*>(refCon);
        if (!data || data->mNumberBuffers < 1 || !data->mBuffers[0].mData) {
            return noErr;
        }
        auto* buffer = static_cast<uint8_t*>(data->mBuffers[0].mData);
        const int frameBytes = self->m_opened.bytesPerFrame();
        int received = 0;
        if (self->m_pull && frameBytes > 0) {
            received = self->m_pull(buffer, static_cast<int>(frames));
        }
        if (received < static_cast<int>(frames)) {
            const int start = std::max(received, 0) * frameBytes;
            std::memset(buffer + start, 0, static_cast<size_t>(frames) * static_cast<size_t>(frameBytes) - static_cast<size_t>(start));
        }
        data->mBuffers[0].mDataByteSize = frames * static_cast<UInt32>(std::max(frameBytes, 0));
        return noErr;
    }

    AudioUnit m_unit = nullptr;
    AudioDeviceID m_device = kAudioObjectUnknown;
    double m_savedRate = 0;
    bool m_hogged = false;
    DeviceFormat m_opened;
    Pull m_pull;
};

} // namespace

std::unique_ptr<AudioOutput> createCoreAudioOutput()
{
    return std::make_unique<CoreAudioOutput>();
}

} // namespace echo
