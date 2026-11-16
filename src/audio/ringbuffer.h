#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace echo {

// Single-producer, single-consumer byte ring. Indices are free-running.
class RingBuffer {
public:
    explicit RingBuffer(int capacityBytes = 8 * 1024 * 1024)
    {
        int cap = 1;
        while (cap < capacityBytes) {
            cap <<= 1;
        }
        m_buffer.resize(static_cast<size_t>(cap));
        m_mask = static_cast<uint64_t>(cap - 1);
    }

    [[nodiscard]] int availableRead() const
    {
        const auto w = m_write.load(std::memory_order_acquire);
        const auto r = m_read.load(std::memory_order_relaxed);
        return static_cast<int>(w - r);
    }

    [[nodiscard]] int availableWrite() const
    {
        return static_cast<int>(m_mask) - availableRead();
    }

    int write(const uint8_t* src, int bytes)
    {
        const int space = availableWrite();
        const int n = std::min(bytes, space);
        const auto w = m_write.load(std::memory_order_relaxed);
        const int cap = static_cast<int>(m_mask + 1);
        const int first = std::min(n, cap - static_cast<int>(w & m_mask));
        std::memcpy(m_buffer.data() + (w & m_mask), src, static_cast<size_t>(first));
        if (n > first) {
            std::memcpy(m_buffer.data(), src + first, static_cast<size_t>(n - first));
        }
        m_write.store(w + static_cast<uint64_t>(n), std::memory_order_release);
        return n;
    }

    int read(uint8_t* dst, int bytes)
    {
        const int have = availableRead();
        const int n = std::min(bytes, have);
        const auto r = m_read.load(std::memory_order_relaxed);
        const int cap = static_cast<int>(m_mask + 1);
        const int first = std::min(n, cap - static_cast<int>(r & m_mask));
        std::memcpy(dst, m_buffer.data() + (r & m_mask), static_cast<size_t>(first));
        if (n > first) {
            std::memcpy(dst + first, m_buffer.data(), static_cast<size_t>(n - first));
        }
        m_read.store(r + static_cast<uint64_t>(n), std::memory_order_release);
        return n;
    }

    void clear()
    {
        const auto w = m_write.load(std::memory_order_relaxed);
        m_read.store(w, std::memory_order_release);
    }

private:
    std::vector<uint8_t> m_buffer;
    uint64_t m_mask = 0;
    std::atomic<uint64_t> m_write {0};
    std::atomic<uint64_t> m_read {0};
};

} // namespace echo
