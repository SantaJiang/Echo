#include "audio/output.h"

#include <atomic>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

namespace echo {
namespace {

using Microsoft::WRL::ComPtr;

class WasapiOutput final : public AudioOutput {
public:
    ~WasapiOutput() override { close(); }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        close();
        const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_comInitialized = SUCCEEDED(init);
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            if (error) {
                *error = QStringLiteral("无法枚举音频设备");
            }
            return false;
        }
        if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device))) {
            if (error) {
                *error = QStringLiteral("找不到输出设备");
            }
            return false;
        }
        if (FAILED(m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, IID_PPV_ARGS(&m_client)))) {
            if (error) {
                *error = QStringLiteral("无法打开 WASAPI");
            }
            return false;
        }
        WAVEFORMATEX* mix = nullptr;
        m_client->GetMixFormat(&mix);
        DeviceFormat format = deviceFormatFor(source, exclusive);
        WAVEFORMATEXTENSIBLE desired = toWave(format);
        bool openedExclusive = false;
        HRESULT supported = exclusive ? m_client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
                                                                     reinterpret_cast<WAVEFORMATEX*>(&desired), nullptr)
                                       : E_FAIL;
        if (exclusive && SUCCEEDED(supported)) {
            openedExclusive = true;
        } else if (mix) {
            format.sampleRate = static_cast<int>(mix->nSamplesPerSec);
            format.channels = mix->nChannels;
            format.isFloat = true;
            format.bitsPerSample = 32;
            format.containerBytes = 4;
            format.exclusive = false;
            format.highAligned = false;
            desired = toWave(format);
            if (mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE)) {
                desired = *reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix);
                if (mix->wFormatTag != WAVE_FORMAT_EXTENSIBLE) {
                    desired = toWave(format);
                }
            }
        }
        const AUDCLNT_SHAREMODE mode = openedExclusive ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
        const REFERENCE_TIME duration = openedExclusive ? 1000000 : 0;
        const HRESULT started = m_client->Initialize(mode, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, duration,
                                                      openedExclusive ? duration : 0, reinterpret_cast<WAVEFORMATEX*>(&desired),
                                                      nullptr);
        if (mix) {
            CoTaskMemFree(mix);
        }
        if (FAILED(started)) {
            if (error) {
                *error = QStringLiteral("设备被占用或不接受该格式");
            }
            close();
            return false;
        }
        m_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_client->SetEventHandle(m_event);
        m_client->GetBufferSize(&m_bufferFrames);
        m_client->GetService(IID_PPV_ARGS(&m_render));
        format.exclusive = openedExclusive;
        format.latencyFrames = static_cast<int>(m_bufferFrames);
        m_opened = format;
        if (opened) {
            *opened = format;
        }
        return true;
    }

    void close() override
    {
        m_running = false;
        if (m_event) {
            SetEvent(m_event);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_client) {
            m_client->Stop();
        }
        m_render.Reset();
        m_client.Reset();
        m_device.Reset();
        if (m_event) {
            CloseHandle(m_event);
            m_event = nullptr;
        }
        if (m_comInitialized) {
            CoUninitialize();
            m_comInitialized = false;
        }
        m_opened = {};
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (!m_client || m_running) {
            return;
        }
        m_running = true;
        m_client->Start();
        m_thread = std::thread([this] { renderLoop(); });
    }

    void pause() override
    {
        m_running = false;
        if (m_event) {
            SetEvent(m_event);
        }
        if (m_thread.joinable()) {
            m_thread.join();
        }
        if (m_client) {
            m_client->Stop();
            m_client->Reset();
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_client != nullptr; }

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
            format.containerBytes = 4;
            format.highAligned = true;
        } else {
            format.bitsPerSample = 32;
            format.containerBytes = 4;
        }
        return format;
    }

    static WAVEFORMATEXTENSIBLE toWave(const DeviceFormat& format)
    {
        WAVEFORMATEXTENSIBLE wave {};
        wave.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wave.Format.nChannels = static_cast<WORD>(format.channels);
        wave.Format.nSamplesPerSec = static_cast<DWORD>(format.sampleRate);
        wave.Format.wBitsPerSample = static_cast<WORD>(format.containerBytes * 8);
        wave.Format.nBlockAlign = static_cast<WORD>(format.bytesPerFrame());
        wave.Format.nAvgBytesPerSec = wave.Format.nSamplesPerSec * wave.Format.nBlockAlign;
        wave.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        wave.Samples.wValidBitsPerSample = static_cast<WORD>(format.bitsPerSample);
        wave.dwChannelMask = format.channels == 1 ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
        wave.SubFormat = format.isFloat ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
        return wave;
    }

    void renderLoop()
    {
        while (m_running) {
            if (WaitForSingleObject(m_event, 500) != WAIT_OBJECT_0) {
                continue;
            }
            if (!m_running || !m_render) {
                break;
            }
            UINT32 padding = 0;
            if (!m_opened.exclusive) {
                m_client->GetCurrentPadding(&padding);
            }
            const UINT32 frames = m_bufferFrames - padding;
            if (frames == 0) {
                continue;
            }
            BYTE* data = nullptr;
            if (FAILED(m_render->GetBuffer(frames, &data)) || !data) {
                continue;
            }
            const int frameBytes = m_opened.bytesPerFrame();
            int received = 0;
            if (m_pull) {
                received = m_pull(data, static_cast<int>(frames));
            }
            if (received < static_cast<int>(frames)) {
                std::memset(data + std::max(received, 0) * frameBytes, 0,
                            static_cast<size_t>(frames - std::max(received, 0)) * static_cast<size_t>(frameBytes));
            }
            m_render->ReleaseBuffer(frames, received == 0 ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
        }
    }

    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioClient> m_client;
    ComPtr<IAudioRenderClient> m_render;
    HANDLE m_event = nullptr;
    UINT32 m_bufferFrames = 0;
    DeviceFormat m_opened;
    Pull m_pull;
    std::atomic<bool> m_running {false};
    std::thread m_thread;
    bool m_comInitialized = false;
};

} // namespace

std::unique_ptr<AudioOutput> createWasapiOutput()
{
    return std::make_unique<WasapiOutput>();
}

} // namespace echo
