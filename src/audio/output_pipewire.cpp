#include "audio/output.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace echo {
namespace {

spa_audio_format spaFormat(const DeviceFormat& format)
{
    if (format.isFloat) {
        return SPA_AUDIO_FORMAT_F32_LE;
    }
    if (format.containerBytes == 2) {
        return SPA_AUDIO_FORMAT_S16_LE;
    }
    if (format.bitsPerSample == 24 && format.containerBytes == 4) {
        return SPA_AUDIO_FORMAT_S24_32_LE;
    }
    if (format.containerBytes == 3) {
        return SPA_AUDIO_FORMAT_S24_LE;
    }
    return SPA_AUDIO_FORMAT_S32_LE;
}

class PipeWireOutput final : public AudioOutput {
public:
    PipeWireOutput()
    {
        pw_init(nullptr, nullptr);
        m_loop = pw_thread_loop_new("echo-audio", nullptr);
    }

    ~PipeWireOutput() override
    {
        close();
        if (m_loop) {
            pw_thread_loop_destroy(m_loop);
        }
        pw_deinit();
    }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        close();
        m_opened.sampleRate = source.sampleRate;
        m_opened.channels = std::clamp(source.channels, 1, 8);
        m_opened.exclusive = false;
        if (!source.isFloat && source.bitsPerSample <= 16) {
            m_opened.bitsPerSample = 16;
            m_opened.containerBytes = 2;
        } else if (!source.isFloat && source.bitsPerSample <= 24) {
            m_opened.bitsPerSample = 24;
            m_opened.containerBytes = 4;
            m_opened.highAligned = false;
        } else if (!source.isFloat) {
            m_opened.bitsPerSample = 32;
            m_opened.containerBytes = 4;
        } else {
            m_opened.isFloat = true;
            m_opened.bitsPerSample = 32;
            m_opened.containerBytes = 4;
        }
        char latency[32];
        std::snprintf(latency, sizeof(latency), "%d/%d", 1024, std::max(m_opened.sampleRate, 1));
        pw_properties* properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                                      PW_KEY_MEDIA_ROLE, "Music", PW_KEY_NODE_LATENCY, latency, nullptr);
        if (exclusive) {
            pw_properties_set(properties, PW_KEY_NODE_EXCLUSIVE, "true");
            m_opened.exclusive = true;
        }
        m_stream = pw_stream_new_simple(pw_thread_loop_get_loop(m_loop), "Echo", properties, &PipeWireOutput::events, this);
        if (!m_stream) {
            if (error) {
                *error = QStringLiteral("无法创建 PipeWire 流");
            }
            return false;
        }
        uint8_t buffer[1024];
        spa_audio_info_raw info {};
        info.format = spaFormat(m_opened);
        info.channels = static_cast<uint32_t>(m_opened.channels);
        info.rate = static_cast<uint32_t>(m_opened.sampleRate);
        if (m_opened.channels == 1) {
            info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        } else {
            info.position[0] = SPA_AUDIO_CHANNEL_FL;
            info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        int connected = pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                                 static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                                                              | PW_STREAM_FLAG_RT_PROCESS),
                                                 params, 1);
        if (connected < 0 && exclusive) {
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
            m_opened.exclusive = false;
            properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
                                           PW_KEY_NODE_LATENCY, latency, nullptr);
            m_stream = pw_stream_new_simple(pw_thread_loop_get_loop(m_loop), "Echo", properties, &PipeWireOutput::events, this);
            connected = m_stream ? pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                                                      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                                                                   | PW_STREAM_FLAG_RT_PROCESS),
                                                      params, 1)
                                 : -1;
        }
        if (connected < 0) {
            if (error) {
                *error = QStringLiteral("无法连接 PipeWire");
            }
            close();
            return false;
        }
        m_opened.latencyFrames = 1024;
        if (opened) {
            *opened = m_opened;
        }
        return true;
    }

    void close() override
    {
        pause();
        if (m_stream) {
            pw_stream_destroy(m_stream);
            m_stream = nullptr;
        }
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (m_loop && !m_started) {
            pw_thread_loop_start(m_loop);
            m_started = true;
        }
    }

    void pause() override
    {
        if (m_loop && m_started) {
            pw_thread_loop_stop(m_loop);
            m_started = false;
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_stream != nullptr; }

private:
    static void onProcess(void* userdata)
    {
        auto* self = static_cast<PipeWireOutput*>(userdata);
        pw_buffer* buffer = pw_stream_dequeue_buffer(self->m_stream);
        if (!buffer) {
            return;
        }
        spa_data& data = buffer->buffer->datas[0];
        const int stride = self->m_opened.bytesPerFrame();
        const int frames = stride > 0 ? static_cast<int>(data.maxsize) / stride : 0;
        int received = 0;
        if (self->m_pull && data.data && frames > 0) {
            received = self->m_pull(static_cast<uint8_t*>(data.data), frames);
        }
        if (received < frames && data.data) {
            std::memset(static_cast<uint8_t*>(data.data) + received * stride, 0,
                        static_cast<size_t>(frames - received) * static_cast<size_t>(stride));
        }
        if (data.chunk) {
            data.chunk->offset = 0;
            data.chunk->stride = stride;
            data.chunk->size = static_cast<uint32_t>(frames * stride);
        }
        pw_stream_queue_buffer(self->m_stream, buffer);
    }

    static const pw_stream_events events;

    pw_thread_loop* m_loop = nullptr;
    pw_stream* m_stream = nullptr;
    DeviceFormat m_opened;
    Pull m_pull;
    bool m_started = false;
};

} // namespace

const pw_stream_events PipeWireOutput::events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = PipeWireOutput::onProcess,
};

std::unique_ptr<AudioOutput> createPipeWireOutput()
{
    return std::make_unique<PipeWireOutput>();
}

} // namespace echo
