#pragma once

#include "../core/types.hpp"
#include <string>
#include <vector>

// Initialize correlation result data structures (domain-specific setup)
void initialize_correlation_result(CorrelationResult& result);

// Gaussian interpolation for refined peak detection (more accurate than parabolic)
ParabolicPeak gaussian_interpolation(const std::complex<float>* data, int peak_idx, int data_size);

// Main correlation processing function
void process_correlations(CorrelationResult& correlation_result, FFTProcessingControl& fft_control);

// Build correlation data message for web interface
std::string build_correlation_message(const CorrelationResult& correlation_result, 
                                    const FFTProcessingControl& fft_control);

// Correlation processor thread function
void correlation_processor(CorrelationResult& correlation_result, FFTProcessingControl& fft_control);