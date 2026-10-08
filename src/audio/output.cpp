#include "audio/output.h"

#include <vector>

namespace echo {
namespace {

class FallbackOutput final : public AudioOutput {
public:
    explicit FallbackOutput(std::vector<std::unique_ptr<AudioOutput>> backends)
        : m_backends(std::move(backends))
    {
    }

    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override
    {
        QString lastError;
        for (const auto& backend : m_backends) {
            QString attempt;
            if (backend->open(source, exclusive, opened, &attempt)) {
                m_active = backend.get();
                return true;
            }
            lastError = attempt;
            backend->close();
        }
        if (error) {
            *error = lastError.isEmpty() ? QStringLiteral("没有可用的音频输出") : lastError;
        }
        return false;
    }

    void close() override
    {
        if (m_active) {
            m_active->close();
            m_active = nullptr;
        }
    }

    void setPull(Pull pull) override
    {
        m_pull = std::move(pull);
        for (const auto& backend : m_backends) {
            backend->setPull(m_pull);
        }
    }

    void start() override
    {
        if (m_active) {
            m_active->start();
        }
    }

    void pause() override
    {
        if (m_active) {
            m_active->pause();
        }
    }

    [[nodiscard]] bool isOpen() const override { return m_active && m_active->isOpen(); }

private:
    std::vector<std::unique_ptr<AudioOutput>> m_backends;
    AudioOutput* m_active = nullptr;
    Pull m_pull;
};

} // namespace

std::unique_ptr<AudioOutput> createAudioOutput()
{
#if defined(Q_OS_MACOS)
    return createCoreAudioOutput();
#elif defined(Q_OS_WIN)
    return createWasapiOutput();
#else
    std::vector<std::unique_ptr<AudioOutput>> backends;
#if defined(ECHO_HAS_PIPEWIRE)
    backends.push_back(createPipeWireOutput());
#endif
#if defined(ECHO_HAS_ALSA)
    backends.push_back(createAlsaOutput());
#endif
    backends.push_back(createNullOutput());
    return std::make_unique<FallbackOutput>(std::move(backends));
#endif
}

} // namespace echo
