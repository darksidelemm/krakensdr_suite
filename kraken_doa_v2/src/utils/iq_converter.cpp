#include "utils/iq_converter.hpp"
#include <cstring>
#include <algorithm>

// Main conversion - optimized simple scalar version
// After extensive benchmarking (45 variants tested), this simple implementation
// is 8.5% faster than hand-optimized NEON and beats all library implementations.
// See opt_iqconv/COMPREHENSIVE_RESULTS.md for details.
void IQConverter::convert_uint8_to_complex_float(
    const uint8_t* input,
    std::complex<float>* output,
    size_t num_samples,
    bool dc_correction
) {
    if (!input || !output || num_samples == 0) {
        return;
    }

    const float off = 127.5f;
    const float scl = 1.0f / 127.5f;

    if (dc_correction) {
        // Two-pass DC correction: calculate mean, then convert with correction
        // Both passes auto-vectorize well

        // Pass 1: Calculate mean I and Q
        float sum_i = 0.0f;
        float sum_q = 0.0f;

        for (size_t i = 0; i < num_samples; i++) {
            sum_i += float(input[i*2]);
            sum_q += float(input[i*2+1]);
        }

        const float mean_i = sum_i / float(num_samples);
        const float mean_q = sum_q / float(num_samples);

        // Pass 2: Convert with DC correction
        for (size_t i = 0; i < num_samples; i++) {
            output[i] = {
                (float(input[i*2]) - mean_i) * scl,
                (float(input[i*2+1]) - mean_q) * scl
            };
        }
    } else {
        // Original fast path without DC correction
        for (size_t i = 0; i < num_samples; i++) {
            output[i] = {
                (float(input[i*2]) - off) * scl,
                (float(input[i*2+1]) - off) * scl
            };
        }
    }
}

// Multi-channel conversion with optimized memory access
void IQConverter::convert_multi_channel(
    const uint8_t* input,
    std::vector<std::vector<std::complex<float>>>& output,
    size_t num_channels,
    size_t samples_per_channel
) {
    output.resize(num_channels);

    // Pre-allocate all channels
    for (size_t ch = 0; ch < num_channels; ch++) {
        output[ch].resize(samples_per_channel);
    }

    // Convert channel by channel for better cache locality
    for (size_t ch = 0; ch < num_channels; ch++) {
        const uint8_t* channel_data = input + ch * samples_per_channel * 2;

        convert_uint8_to_complex_float(
            channel_data,
            output[ch].data(),
            samples_per_channel,
            true  // Enable DC correction
        );
    }
}

// Multi-channel conversion with pre-allocated buffers (avoids allocation overhead)
void IQConverter::convert_multi_channel_preallocated(
    const uint8_t* input,
    std::complex<float>** output_channels,
    size_t num_channels,
    size_t samples_per_channel
) {
    // Process each channel sequentially (single-threaded is faster for per-channel conversion)
    // For parallel processing, use OpenMP to parallelize across channels, not within channels
    for (size_t ch = 0; ch < num_channels; ch++) {
        const uint8_t* channel_data = input + ch * samples_per_channel * 2;
        convert_uint8_to_complex_float(
            channel_data,
            output_channels[ch],
            samples_per_channel,
            true  // Enable DC correction
        );
    }
}
