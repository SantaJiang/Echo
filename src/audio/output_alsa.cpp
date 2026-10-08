#include "audio/output.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace echo {
namespace {

snd_pcm_format_t pcmFormat(const DeviceFormat& format)
{
    if (format.isFloat) {
        return SND_PCM_FORMAT_FLOAT_LE;
    }
    if (format.containerBytes == 2) {
        return SND_PCM_FORMAT_S16_LE;
    }
    if (format.containerBytes == 3) {
        return SND_PCM_FORMAT_S24_3LE;
    }
    return SND_PCM_FORMAT_S32_LE;
}

class AlsaOutput final : public AudioOutput {
public:
    ~AlsaOutput() override { close(); }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        close();
        const char* device = exclusive ? "hw:0,0" : "default";
        if (snd_pcm_open(&m_pcm, device, SND_PCM_STREAM_PLAYBACK, 0) < 0 && exclusive) {
            if (snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
                if (error) {
                    *error = QStringLiteral("无法打开 ALSA");
                }
                return false;
            }
            exclusive = false;
        } else if (!m_pcm) {
            if (error) {
                *error = QStringLiteral("无法打开 ALSA");
            }
            return false;
        }
        m_opened = DeviceFormat {};
        m_opened.sampleRate = source.sampleRate;
        m_opened.channels = source.channels;
        m_opened.exclusive = exclusive;
        if (source.isFloat) {
            m_opened.isFloat = true;
            m_opened.bitsPerSample = 32;
            m_opened.containerBytes = 4;
        } else if (source.bitsPerSample <= 16) {
            m_opened.bitsPerSample = 16;
            m_opened.containerBytes = 2;
        } else if (source.bitsPerSample <= 24) {
            m_opened.bitsPerSample = 24;
            m_opened.containerBytes = 3;
        } else {
            m_opened.bitsPerSample = 32;
            m_opened.containerBytes = 4;
        }
        const int result = snd_pcm_set_params(m_pcm, pcmFormat(m_opened), SND_PCM_ACCESS_RW_INTERLEAVED,
                                               static_cast<unsigned>(m_opened.channels),
                                               static_cast<unsigned>(m_opened.sampleRate), 1, 100000);
        if (result < 0) {
            m_opened.isFloat = true;
            m_opened.bitsPerSample = 32;
            m_opened.containerBytes = 4;
            m_opened.exclusive = false;
            if (snd_pcm_set_params(m_pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                   static_cast<unsigned>(m_opened.channels), static_cast<unsigned>(m_opened.sampleRate), 1,
                                   100000)
                < 0) {
                if (error) {
                    *error = QStringLiteral("ALSA 不接受该格式");
                }
                close();
                return false;
            }
        }
        snd_pcm_uframes_t bufferSize = 0;
        snd_pcm_uframes_t period = 0;
        snd_pcm_get_params(m_pcm, &bufferSize, &period);
        m_opened.latencyFrames = static_cast<int>(period > 0 ? period : 1024);
        if (opened) {
            *opened = m_opened;
        }
        return true;
    }

    void close() override
    {
        pause();
        if (m_pcm) {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
        }
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (!m_pcm || m_running) {
            return;
        }
        m_running = true;
        m_thread = std::thread([this] { renderLoop(); });
    }

    void pause() override
    {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_pcm != nullptr; }

private:
    void renderLoop()
    {
        const int frames = std::max(256, m_opened.latencyFrames);
        std::vector<uint8_t> buffer(static_cast<size_t>(frames) * static_cast<size_t>(m_opened.bytesPerFrame()));
        while (m_running) {
            int received = 0;
            if (m_pull) {
                received = m_pull(buffer.data(), frames);
            }
            if (received <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                continue;
            }
            int offset = 0;
            while (offset < received && m_running) {
                const int written = static_cast<int>(snd_pcm_writei(m_pcm, buffer.data() + offset * m_opened.bytesPerFrame(),
                                                                     static_cast<snd_pcm_uframes_t>(received - offset)));
                if (written == -EPIPE) {
                    snd_pcm_prepare(m_pcm);
                    continue;
                }
                if (written < 0) {
                    return;
                }
                offset += written;
            }
        }
    }

    snd_pcm_t* m_pcm = nullptr;
    DeviceFormat m_opened;
    Pull m_pull;
    std::atomic<bool> m_running {false};
    std::thread m_thread;
};

} // namespace

std::unique_ptr<AudioOutput> createAlsaOutput()
{
    return std::make_unique<AlsaOutput>();
}

} // namespace echo
