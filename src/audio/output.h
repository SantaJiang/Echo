#pragma once

#include "audio/format.h"

#include <functional>
#include <memory>
#include <QString>

namespace echo {

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
    [[nodiscard]] virtual bool isOpen() const = 0;
};

std::unique_ptr<AudioOutput> createAudioOutput();
std::unique_ptr<AudioOutput> createCoreAudioOutput();
std::unique_ptr<AudioOutput> createWasapiOutput();
std::unique_ptr<AudioOutput> createPipeWireOutput();
std::unique_ptr<AudioOutput> createAlsaOutput();
std::unique_ptr<AudioOutput> createNullOutput();

} // namespace echo
