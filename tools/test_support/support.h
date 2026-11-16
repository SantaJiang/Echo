#pragma once

#include "audio/output.h"

#include <QString>

#include <atomic>
#include <functional>
#include <thread>

namespace echo::test {

// Shared assertions for the hand-written test executables. A failure aborts
// the process so CI stops on the first broken check.
void check(bool ok, const char* message);
[[nodiscard]] int passedChecks();

// Pumps the Qt event loop until the predicate holds or the timeout elapses.
[[nodiscard]] bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 8000);

// Writes a 48 kHz mono 16-bit sine so library and playback tests share one fixture.
void writePcm16Wave(const QString& path, int milliseconds);

// Pulls PCM on a background thread and discards it. Counters let tests assert
// that decoded audio is nonzero and that the device is not starved.
class SilentOutput final : public AudioOutput {
public:
    ~SilentOutput() override;
    bool open(const SampleFormat& source, bool exclusive, DeviceFormat* opened, QString* error) override;
    void close() override;
    void setPull(Pull pull) override;
    void start() override;
    void pause() override;
    [[nodiscard]] bool isOpen() const override;

    std::atomic<int> blocks { 0 };
    std::atomic<int> nonzero { 0 };
    std::atomic<int> underruns { 0 };

private:
    Pull m_pull;
    int m_rate = 48000;
    int m_channels = 1;
    bool m_open = false;
    std::atomic<bool> m_running { false };
    std::thread m_worker;
};

} // namespace echo::test
