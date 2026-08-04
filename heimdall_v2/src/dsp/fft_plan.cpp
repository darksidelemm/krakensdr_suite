#include "fft_plan.hpp"
#include "../core/config.hpp"
#include <algorithm>
#include <cstring>

FftPlan::FftPlan() {
    input = fftwf_alloc_complex(CORRELATION_SIZE);
    output = fftwf_alloc_complex(CORRELATION_SIZE);
    correlation_freq = fftwf_alloc_complex(CORRELATION_SIZE);
    correlation_time = fftwf_alloc_complex(CORRELATION_SIZE);
    
    forward_plan = fftwf_plan_dft_1d(CORRELATION_SIZE, input, output, FFTW_FORWARD, FFTW_ESTIMATE);
    inverse_plan = fftwf_plan_dft_1d(CORRELATION_SIZE, correlation_freq, correlation_time, FFTW_BACKWARD, FFTW_ESTIMATE);
}

FftPlan::~FftPlan() {
    if (forward_plan) fftwf_destroy_plan(forward_plan);
    if (inverse_plan) fftwf_destroy_plan(inverse_plan);
    if (input) fftwf_free(input);
    if (output) fftwf_free(output);
    if (correlation_freq) fftwf_free(correlation_freq);
    if (correlation_time) fftwf_free(correlation_time);
}

void FftPlan::forward(const ComplexBuffer& data, int offset) {
    memset(input, 0, CORRELATION_SIZE * sizeof(fftwf_complex));
    int samples_to_copy = std::min(static_cast<int>(data.size()), NUM_SAMPLES);
    memcpy(input + offset, data.data(), samples_to_copy * sizeof(std::complex<float>));
    fftwf_execute(forward_plan);
}

std::complex<float>* FftPlan::get_output() { 
    return reinterpret_cast<std::complex<float>*>(output); 
}

std::complex<float>* FftPlan::get_correlation_freq() { 
    return reinterpret_cast<std::complex<float>*>(correlation_freq); 
}

std::complex<float>* FftPlan::get_correlation_time() { 
    return reinterpret_cast<std::complex<float>*>(correlation_time); 
}

void FftPlan::cross_correlate(const std::complex<float>* ref_fft, const std::complex<float>* sig_fft) {
    auto* freq = get_correlation_freq();
    for (int i = 0; i < CORRELATION_SIZE; i++) {
        freq[i] = std::conj(ref_fft[i]) * sig_fft[i];
    }
    fftwf_execute(inverse_plan);
}

std::complex<float>* FftPlan::inverse_transform(const std::complex<float>* freq_in) {
    auto* freq = get_correlation_freq();
    std::memcpy(freq, freq_in, CORRELATION_SIZE * sizeof(std::complex<float>));
    fftwf_execute(inverse_plan);
    return get_correlation_time();
}

FftPool::FftPool(int size) {
    for (int i = 0; i < size; i++) {
        auto plan = std::make_unique<FftPlan>();
        available.enqueue(plan.get());
        plans.push_back(std::move(plan));
    }
}

FftPlan* FftPool::acquire() {
    FftPlan* plan;
    available.wait_dequeue(plan);
    return plan;
}

void FftPool::release(FftPlan* plan) {
    available.enqueue(plan);
}

// Global FFT pool instance
FftPool fft_pool;
