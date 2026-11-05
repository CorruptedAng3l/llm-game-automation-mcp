// Per-VK observation counters. Reads prove API delivery, never that the application acted on it.
#pragma once
#include <atomic>
#include <cstdint>

struct ChannelTrace {
    std::atomic<long long> reads{0};
    std::atomic<long long> down_reads{0};
    std::atomic<int> last{0};
    std::atomic<unsigned> writing{0};
    std::atomic<std::uint64_t> serial{0};

    void record(int value, bool down) noexcept {
        writing.fetch_add(1);
        reads.fetch_add(1, std::memory_order_relaxed);
        if (down) down_reads.fetch_add(1, std::memory_order_relaxed);
        last.store(value, std::memory_order_relaxed);
        serial.fetch_add(1);writing.fetch_sub(1);
    }

    // Bounded observation: never spin waiting for a hot application reader.
    bool snapshot(long long& count,long long& high) const noexcept {
        for(unsigned attempt=0;attempt<4;++attempt){const auto before=serial.load();if(writing.load())continue;
            const auto r=reads.load(),d=down_reads.load();
            if(!writing.load()&&serial.load()==before){count=r;high=d;return true;}}
        return false;
    }

    // Win32 SHORT states use bit 15; BYTE keyboard/DirectInput states use bit 7.
    // For raw MAKE/BREAK flags use the explicit overload: raw MAKE is zero.
    void record(int value) noexcept {
        record(value, (static_cast<unsigned int>(value) & 0x8080u) != 0);
    }
};

struct KeyTrace {
    std::atomic<long long> injected_down{0}, injected_up{0};
    std::atomic<long long> delivered_down{0}, delivered_up{0};
    std::atomic<long long> post_attempt_down{0}, post_attempt_up{0};
    std::atomic<long long> post_accepted_down{0}, post_accepted_up{0};
    std::atomic<long long> post_failed_down{0}, post_failed_up{0};
    std::atomic<std::uint32_t> last_post_error{0}; // last failed PostMessage error; zero until a failure
    std::atomic<std::uint64_t> last_injected_down_ms{0}, last_injected_up_ms{0};
    ChannelTrace gaks, gks, gkbs, di_state, di_buffer, raw_data, raw_buffer;

    void injection(bool down, std::uint64_t tick_ms) noexcept {
        if (down) {
            injected_down.fetch_add(1, std::memory_order_relaxed);
            last_injected_down_ms.store(tick_ms, std::memory_order_relaxed);
        } else {
            injected_up.fetch_add(1, std::memory_order_relaxed);
            last_injected_up_ms.store(tick_ms, std::memory_order_relaxed);
        }
    }

    void delivery(bool down) noexcept {
        if (down) delivered_down.fetch_add(1, std::memory_order_relaxed);
        else delivered_up.fetch_add(1, std::memory_order_relaxed);
    }
    void posting(bool down) noexcept {
        (down ? post_attempt_down : post_attempt_up).fetch_add(1, std::memory_order_relaxed);
    }
    void post_result(bool down, bool accepted, std::uint32_t error) noexcept {
        if (accepted) (down ? post_accepted_down : post_accepted_up).fetch_add(1, std::memory_order_relaxed);
        else { (down ? post_failed_down : post_failed_up).fetch_add(1, std::memory_order_relaxed); last_post_error.store(error, std::memory_order_relaxed); }
    }
};
