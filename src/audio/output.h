#pragma once

#include "audio/format.h"

#include <functional>
#include <memory>
#include <QString>

namespace echo {

struct OutputDiagnostics {
    qint64 submittedFrames = 0;
    qint64 bufferedFrames = 0;
    qint64 maximumBufferedFrames = 0;
    bool prerollReached = false;
    double sourcePeak = 0;
    float volume = 1;
};

class AudioOutput {
public:
    virtual ~AudioOutput() = default;

    using Pull = std::function<int(uint8_t* dst, int frames)>;

    // Opens the device. On success, fills the format that was actually opened.
    virtual bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) = 0;
    virtual void close() = 0;
    virtual void setPull(Pull pull) = 0;
    virtual void start() = 0;
    virtual void pause() = 0;
    // Queued renderers expose their unplayed frames so engine positions follow audible output.
    virtual void flush() {}
    virtual void drain() {}
    virtual void finishInput() {}
    [[nodiscard]] virtual OutputDiagnostics diagnostics() const { return {}; }
    virtual void setVolume(float) {}
    [[nodiscard]] virtual bool queuedPlayback() const { return false; }
    virtual void setSpatialEnabled(bool) {}
    [[nodiscard]] virtual qint64 pendingFrames() const { return 0; }
    [[nodiscard]] virtual QString spatialStatus() const { return {}; }
    [[nodiscard]] virtual QString takeError() { return {}; }
    [[nodiscard]] virtual bool isOpen() const = 0;
};

// Shared device negotiation for backends that can open the source's own PCM layout.
[[nodiscard]] DeviceFormat deviceFormatFromSource(const SampleFormat& source, bool exclusive, bool align24In32 = true);
// Pulls up to `frames` and zeros any shortfall. Returns the number of frames actually supplied.
int pullOrSilence(const AudioOutput::Pull& pull, uint8_t* destination, int frames, int frameBytes);

std::unique_ptr<AudioOutput> createAudioOutput();
std::unique_ptr<AudioOutput> createCoreAudioOutput();
std::unique_ptr<AudioOutput> createMacAudioOutput();
std::unique_ptr<AudioOutput> createSpatialAudioOutput();
std::unique_ptr<AudioOutput> createWasapiOutput();
std::unique_ptr<AudioOutput> createPipeWireOutput();
std::unique_ptr<AudioOutput> createAlsaOutput();
std::unique_ptr<AudioOutput> createNullOutput();

} // namespace echo
