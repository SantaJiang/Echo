#include "audio/output.h"

#include <cstring>
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
    void flush() override { if (m_active) m_active->flush(); }
    void drain() override { if (m_active) m_active->drain(); }
    void finishInput() override { if (m_active) m_active->finishInput(); }
    [[nodiscard]] OutputDiagnostics diagnostics() const override { return m_active ? m_active->diagnostics() : OutputDiagnostics {}; }
    void setVolume(float value) override
    {
        if (m_active) m_active->setVolume(value);
        else for (const auto& backend : m_backends) backend->setVolume(value);
    }
    [[nodiscard]] bool queuedPlayback() const override { return m_active && m_active->queuedPlayback(); }
    void setSpatialEnabled(bool enabled) override
    {
        if (m_active) m_active->setSpatialEnabled(enabled);
        else for (const auto& backend : m_backends) backend->setSpatialEnabled(enabled);
    }
    [[nodiscard]] qint64 pendingFrames() const override { return m_active ? m_active->pendingFrames() : 0; }
    [[nodiscard]] QString spatialStatus() const override { return m_active ? m_active->spatialStatus() : QString {}; }
    [[nodiscard]] QString takeError() override { return m_active ? m_active->takeError() : QString {}; }

private:
    std::vector<std::unique_ptr<AudioOutput>> m_backends;
    AudioOutput* m_active = nullptr;
    Pull m_pull;
};

} // namespace

DeviceFormat deviceFormatFromSource(const SampleFormat& source, bool exclusive, bool align24In32)
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
        format.highAligned = align24In32;
    } else {
        format.bitsPerSample = 32;
        format.containerBytes = 4;
    }
    return format;
}

int pullOrSilence(const AudioOutput::Pull& pull, uint8_t* destination, int frames, int frameBytes)
{
    if (!destination || frames <= 0 || frameBytes <= 0) {
        return 0;
    }
    int received = pull ? pull(destination, frames) : 0;
    if (received < 0) {
        received = 0;
    }
    if (received > frames) {
        received = frames;
    }
    if (received < frames) {
        std::memset(destination + static_cast<size_t>(received) * static_cast<size_t>(frameBytes), 0,
            static_cast<size_t>(frames - received) * static_cast<size_t>(frameBytes));
    }
    return received;
}

std::unique_ptr<AudioOutput> createAudioOutput()
{
#if defined(Q_OS_MACOS)
    return createMacAudioOutput();
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
