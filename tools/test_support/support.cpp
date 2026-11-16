#include "test_support/support.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cmath>
#include <vector>

namespace echo::test {
namespace {
int g_checks = 0;
}

void check(bool ok, const char* message)
{
    if (!ok) {
        qFatal("FAIL %s", message);
    }
    ++g_checks;
}

int passedChecks()
{
    return g_checks;
}

bool waitFor(const std::function<bool()>& predicate, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    do {
        QCoreApplication::processEvents();
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    } while (clock.elapsed() < timeoutMs);
    return false;
}

void writePcm16Wave(const QString& path, int milliseconds)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "wave fixture opens");
    constexpr int rate = 48000;
    const int frames = rate * std::max(1, milliseconds) / 1000;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames * 2);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(frames * 2);
    for (int i = 0; i < frames; ++i) {
        out << qint16(std::sin(i * 2 * 3.141592653589793 * 440 / rate) * 5000);
    }
}

SilentOutput::~SilentOutput()
{
    close();
}

bool SilentOutput::open(const SampleFormat& source, bool, DeviceFormat* opened, QString*)
{
    m_rate = source.sampleRate;
    m_channels = source.channels;
    *opened = { m_rate, m_channels, 32, 4, true };
    m_open = true;
    return true;
}

void SilentOutput::close()
{
    pause();
    m_open = false;
}

void SilentOutput::setPull(Pull pull)
{
    m_pull = std::move(pull);
}

void SilentOutput::start()
{
    pause();
    m_running = true;
    m_worker = std::thread([this] {
        std::vector<float> samples(static_cast<size_t>(m_rate / 200 * m_channels));
        while (m_running) {
            const int frames = m_rate / 200;
            const int got = m_pull(reinterpret_cast<uint8_t*>(samples.data()), frames);
            if (got < frames) {
                ++underruns;
            }
            if (got) {
                ++blocks;
                for (int i = 0; i < got * m_channels; ++i) {
                    if (std::abs(samples[i]) > .01f) {
                        ++nonzero;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
}

void SilentOutput::pause()
{
    m_running = false;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool SilentOutput::isOpen() const
{
    return m_open;
}

} // namespace echo::test
