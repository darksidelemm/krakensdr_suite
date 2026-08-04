#pragma once

#include <complex>
#include <atomic>
#include <array>

// Lock-free atomic complex number for hot-path phase compensation
// Uses two separate atomics to avoid locking during reads
struct AtomicComplex {
    std::atomic<float> real;
    std::atomic<float> imag;

    AtomicComplex() : real(1.0f), imag(0.0f) {}

    AtomicComplex(float r, float i) : real(r), imag(i) {}

    // Fast lock-free read (hot path - called millions of times/sec)
    inline std::complex<float> load_relaxed() const {
        return std::complex<float>(
            real.load(std::memory_order_relaxed),
            imag.load(std::memory_order_relaxed)
        );
    }

    // Thread-safe write (cold path - called during calibration only)
    inline void store_release(const std::complex<float>& value) {
        real.store(value.real(), std::memory_order_release);
        imag.store(value.imag(), std::memory_order_release);
    }

    // Prevent copying (atomics aren't copyable)
    AtomicComplex(const AtomicComplex&) = delete;
    AtomicComplex& operator=(const AtomicComplex&) = delete;

    // Allow move construction for initialization
    AtomicComplex(AtomicComplex&& other)
        : real(other.real.load(std::memory_order_relaxed))
        , imag(other.imag.load(std::memory_order_relaxed)) {}
};

// Lock-free phase compensation vector
// Aligned for cache efficiency
template<size_t N>
struct alignas(64) AtomicCompensationVector {
    std::array<AtomicComplex, N> data;

    AtomicCompensationVector() {
        // Initialize all to (1.0, 0.0) - identity
        for (size_t i = 0; i < N; ++i) {
            data[i].real.store(1.0f, std::memory_order_relaxed);
            data[i].imag.store(0.0f, std::memory_order_relaxed);
        }
    }

    // Fast read for hot path (no locks!)
    inline std::complex<float> load(size_t index) const {
        return data[index].load_relaxed();
    }

    // Thread-safe write for calibration (cold path)
    inline void store(size_t index, const std::complex<float>& value) {
        data[index].store_release(value);
    }
};
