#include "audio/output.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace echo {
namespace {

class NullOutput final : public AudioOutput {
public:
    ~NullOutput() override { close(); }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString*) override
    {
        close();
        m_opened.sampleRate = source.sampleRate;
        m_opened.channels = source.channels;
        m_opened.isFloat = source.isFloat;
        m_opened.bitsPerSample = source.isFloat ? 32 : source.bitsPerSample;
        m_opened.containerBytes = source.bytesPerSample;
        m_opened.exclusive = exclusive;
        m_opened.latencyFrames = source.sampleRate / 50;
        if (opened) {
            *opened = m_opened;
        }
        m_open = true;
        return m_opened.valid();
    }

    void close() override
    {
        pause();
        m_open = false;
    }

    void setPull(Pull pull) override { m_pull = std::move(pull); }

    void start() override
    {
        if (!m_open || m_running) {
            return;
        }
        m_running = true;
        m_thread = std::thread([this] {
            const int frames = std::max(64, m_opened.sampleRate / 100);
            std::vector<uint8_t> buffer(static_cast<size_t>(frames * m_opened.bytesPerFrame()));
            while (m_running) {
                if (m_pull) {
                    m_pull(buffer.data(), frames);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    void pause() override
    {
        m_running = false;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_open; }

private:
    DeviceFormat m_opened;
    Pull m_pull;
    std::atomic<bool> m_running {false};
    std::thread m_thread;
    bool m_open = false;
};

} // namespace

std::unique_ptr<AudioOutput> createNullOutput()
{
    return std::make_unique<NullOutput>();
}

} // namespace echo
