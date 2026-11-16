#include "audio/output.h"

#include <alsa/asoundlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
        char hardware[32] = "hw:0,0";
        int card = -1;
        if (snd_card_next(&card) >= 0 && card >= 0) {
            std::snprintf(hardware, sizeof(hardware), "hw:%d,0", card);
        }
        const bool hardwareOpen = exclusive && snd_pcm_open(&m_pcm, hardware, SND_PCM_STREAM_PLAYBACK, 0) >= 0;
        if (!hardwareOpen && snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
            if (error) {
                *error = QStringLiteral("无法打开 ALSA");
            }
            return false;
        }
        m_opened = describe(source);
        m_opened.exclusive = hardwareOpen;
        if (!configure(m_pcm, &m_opened, hardwareOpen)) {
            snd_pcm_close(m_pcm);
            m_pcm = nullptr;
            if (hardwareOpen && snd_pcm_open(&m_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
                m_opened = describe(source);
                m_opened.exclusive = false;
                m_opened.isFloat = true;
                m_opened.bitsPerSample = 32;
                m_opened.containerBytes = 4;
            }
            if (!m_pcm || !configure(m_pcm, &m_opened, false)) {
                if (error) {
                    *error = QStringLiteral("ALSA 不接受该格式");
                }
                close();
                return false;
            }
        }
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
            int received = pullOrSilence(m_pull, buffer.data(), frames, m_opened.bytesPerFrame());
            if (received <= 0) {
                received = frames;
            }
            int offset = 0;
            while (offset < received && m_running) {
                const int written = static_cast<int>(snd_pcm_writei(m_pcm, buffer.data() + offset * m_opened.bytesPerFrame(),
                                                                     static_cast<snd_pcm_uframes_t>(received - offset)));
                if (written < 0) {
                    if (snd_pcm_recover(m_pcm, written, 1) < 0) {
                        return;
                    }
                    continue;
                }
                offset += written;
            }
        }
    }

    static DeviceFormat describe(const SampleFormat& source)
    {
        return deviceFormatFromSource(source, false, true);
    }

    static bool configure(snd_pcm_t* pcm, DeviceFormat* format, bool exclusive)
    {
        snd_pcm_hw_params_t* hardware = nullptr;
        snd_pcm_hw_params_alloca(&hardware);
        if (snd_pcm_hw_params_any(pcm, hardware) < 0) {
            return false;
        }
        snd_pcm_hw_params_set_rate_resample(pcm, hardware, exclusive ? 0 : 1);
        if (snd_pcm_hw_params_set_access(pcm, hardware, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
            return false;
        }
        const snd_pcm_format_t preferred = pcmFormat(*format);
        const snd_pcm_format_t order[] = {preferred, SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_LE, SND_PCM_FORMAT_S16_LE,
                                           SND_PCM_FORMAT_FLOAT_LE};
        bool formatSet = false;
        for (const snd_pcm_format_t candidate : order) {
            if (snd_pcm_hw_params_set_format(pcm, hardware, candidate) == 0) {
                format->isFloat = candidate == SND_PCM_FORMAT_FLOAT_LE;
                if (candidate == SND_PCM_FORMAT_S16_LE) {
                    format->bitsPerSample = 16;
                    format->containerBytes = 2;
                    format->highAligned = false;
                } else if (candidate == SND_PCM_FORMAT_S24_LE) {
                    format->bitsPerSample = 24;
                    format->containerBytes = 4;
                    format->highAligned = false;
                } else if (candidate == SND_PCM_FORMAT_S32_LE) {
                    format->bitsPerSample = format->bitsPerSample == 24 ? 24 : 32;
                    format->containerBytes = 4;
                    format->highAligned = format->bitsPerSample == 24;
                } else {
                    format->bitsPerSample = 32;
                    format->containerBytes = 4;
                    format->highAligned = false;
                }
                formatSet = true;
                break;
            }
        }
        if (!formatSet) {
            return false;
        }
        unsigned int channels = static_cast<unsigned int>(std::max(format->channels, 1));
        if (snd_pcm_hw_params_set_channels(pcm, hardware, channels) < 0) {
            channels = 2;
            if (snd_pcm_hw_params_set_channels(pcm, hardware, channels) < 0) {
                return false;
            }
            format->channels = 2;
        }
        unsigned int rate = static_cast<unsigned int>(std::max(format->sampleRate, 1));
        int direction = 0;
        if (exclusive) {
            if (snd_pcm_hw_params_set_rate(pcm, hardware, rate, 0) < 0) {
                return false;
            }
        } else if (snd_pcm_hw_params_set_rate_near(pcm, hardware, &rate, &direction) < 0) {
            return false;
        }
        format->sampleRate = static_cast<int>(rate);
        snd_pcm_uframes_t period = std::max<snd_pcm_uframes_t>(256, rate / 100);
        snd_pcm_hw_params_set_period_size_near(pcm, hardware, &period, &direction);
        unsigned int periods = 4;
        snd_pcm_hw_params_set_periods_near(pcm, hardware, &periods, &direction);
        if (snd_pcm_hw_params(pcm, hardware) < 0) {
            return false;
        }
        format->latencyFrames = static_cast<int>(period);
        snd_pcm_sw_params_t* software = nullptr;
        snd_pcm_sw_params_alloca(&software);
        if (snd_pcm_sw_params_current(pcm, software) == 0) {
            snd_pcm_sw_params_set_start_threshold(pcm, software, period);
            snd_pcm_sw_params_set_avail_min(pcm, software, period);
            snd_pcm_sw_params(pcm, software);
        }
        return true;
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
