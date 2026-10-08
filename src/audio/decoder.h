#pragma once

#include "audio/format.h"

#include <QString>

namespace echo {

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();
    [[nodiscard]] bool isOpen() const;

    // Audible timeline. endFrame < 0 plays until the file ends.
    void setRange(qint64 startFrame, qint64 endFrame);

    [[nodiscard]] const SampleFormat& format() const { return m_format; }
    [[nodiscard]] const ReplayGain& replayGain() const { return m_gain; }
    [[nodiscard]] qint64 encoderDelay() const { return m_delay; }
    [[nodiscard]] qint64 encoderPadding() const { return m_padding; }
    [[nodiscard]] qint64 durationMs() const;

    bool seekAudibleFrame(qint64 frame);
    // Interleaved source PCM. Returns frames, 0 at end of range, -1 on error.
    int read(uint8_t* dst, int maxFrames);

private:
    struct Impl;
    Impl* m_impl = nullptr;
    SampleFormat m_format;
    ReplayGain m_gain;
    qint64 m_delay = 0;
    qint64 m_padding = 0;
    qint64 m_rangeStart = 0;
    qint64 m_rangeEnd = -1;
};

} // namespace echo
