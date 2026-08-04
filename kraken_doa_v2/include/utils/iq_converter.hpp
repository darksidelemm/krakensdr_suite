#pragma once

#include <vector>
#include <complex>
#include <cstdint>
#include <cstddef>

// Optimized IQ data converter
// Uses simple scalar implementation which is 8.5% faster than hand-optimized SIMD
// and beats all library implementations (tested 45 variants).
// See opt_iqconv/COMPREHENSIVE_RESULTS.md for detailed benchmarking results.
class IQConverter {
public:
    // Convert uint8 IQ data to complex float
    // Input: interleaved I/Q bytes (I0, Q0, I1, Q1, ...)
    // Output: complex float samples normalized to [-1.0, 1.0]
    // Formula: normalized = (uint8_value - 127.5) / 127.5
    // DC correction: If enabled, calculates mean I/Q and subtracts to remove center spike
    static void convert_uint8_to_complex_float(
        const uint8_t* input,
        std::complex<float>* output,
        size_t num_samples,
        bool dc_correction = true
    );

    // Batch convert for multiple channels with optimized memory access
    // Processes each channel sequentially (single-threaded is fastest per channel)
    static void convert_multi_channel(
        const uint8_t* input,
        std::vector<std::vector<std::complex<float>>>& output,
        size_t num_channels,
        size_t samples_per_channel
    );

    // Batch convert with pre-allocated output buffers
    // Avoids allocation overhead when buffers are reused
    static void convert_multi_channel_preallocated(
        const uint8_t* input,
        std::complex<float>** output_channels,
        size_t num_channels,
        size_t samples_per_channel
    );
};
