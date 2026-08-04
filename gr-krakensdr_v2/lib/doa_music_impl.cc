/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "doa_music_impl.h"
#include <gnuradio/io_signature.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace gr {
namespace krakensdr_v2 {

namespace {

constexpr double SPEED_OF_LIGHT = 299792458.0;

// UCA element ordering: the antenna ring is expected to be wired CLOCKWISE
// (ANT0 on +x, ANT1 clockwise from it) on both standard KrakenSDR and
// Wideband hardware. Mirrors the element angle about +x so DoA OUTPUT stays
// unit-circle CCW. Must match uca_angle_sign() in
// kraken_doa_v2/include/globals.hpp - flip both together.
constexpr double UCA_ANGLE_SIGN = -1.0;

// Parameter validation helpers usable inside the base-class initializer
// (io_signature construction happens before the ctor body runs).
int validated_vec_len(int vec_len)
{
    if (vec_len < 1) {
        throw std::invalid_argument("doa_music: vec_len must be >= 1");
    }
    return vec_len;
}

int validated_num_elements(int num_elements)
{
    if (num_elements < 2) {
        throw std::invalid_argument("doa_music: num_elements must be >= 2");
    }
    return num_elements;
}

bool parse_array_type(const std::string& array_type)
{
    std::string t(array_type);
    std::transform(
        t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::toupper(c); });
    if (t == "ULA") {
        return true;
    }
    if (t == "UCA") {
        return false;
    }
    throw std::invalid_argument("doa_music: array_type must be \"UCA\" or \"ULA\", got "
                                "\"" +
                                array_type + "\"");
}

} // namespace

doa_music::sptr doa_music::make(int vec_len,
                                double freq_mhz,
                                double array_dist_m,
                                int num_elements,
                                const std::string& array_type,
                                int signal_dimension)
{
    return gnuradio::make_block_sptr<doa_music_impl>(
        vec_len, freq_mhz, array_dist_m, num_elements, array_type, signal_dimension);
}

doa_music_impl::doa_music_impl(int vec_len,
                               double freq_mhz,
                               double array_dist_m,
                               int num_elements,
                               const std::string& array_type,
                               int signal_dimension)
    : gr::sync_block(
          "doa_music",
          gr::io_signature::make(validated_num_elements(num_elements),
                                 num_elements,
                                 sizeof(gr_complex) *
                                     (size_t)validated_vec_len(vec_len)),
          gr::io_signature::makev(3,
                                  3,
                                  std::vector<int>{ (int)(sizeof(float) * NUM_ANGLES),
                                                    (int)sizeof(float),
                                                    (int)sizeof(float) })),
      d_vec_len(vec_len),
      d_num_elements(num_elements),
      d_signal_dim(std::clamp(signal_dimension, 1, num_elements - 1)),
      d_noise_dim(num_elements - d_signal_dim),
      d_is_ula(parse_array_type(array_type)),
      d_array_dist_m(array_dist_m),
      d_freq_mhz(freq_mhz),
      d_xpos(num_elements, 0.0),
      d_ypos(num_elements, 0.0),
      d_X(num_elements, vec_len),
      d_R(num_elements, num_elements),
      d_S(num_elements, NUM_ANGLES),
      d_EHS(num_elements - d_signal_dim, NUM_ANGLES),
      d_solver(num_elements),
      d_last_warn(std::chrono::steady_clock::now() - std::chrono::hours(1))
{
    if (!(freq_mhz > 0.0)) {
        throw std::invalid_argument("doa_music: freq_mhz must be > 0");
    }
    if (!(array_dist_m > 0.0)) {
        throw std::invalid_argument("doa_music: array_dist_m must be > 0");
    }
    if (d_signal_dim != signal_dimension) {
        d_logger->warn("signal_dimension {} clamped to {} (valid range 1..{})",
                       signal_dimension,
                       d_signal_dim,
                       num_elements - 1);
    }

    generate_steering(freq_mhz); // no lock needed: no work thread yet
}

doa_music_impl::~doa_music_impl() {}

void doa_music_impl::generate_steering(double freq_mhz)
{
    const double lambda = SPEED_OF_LIGHT / (freq_mhz * 1e6);
    const double r = d_array_dist_m / lambda; // radius / spacing in wavelengths
    const int M = d_num_elements;

    // Element positions (kraken_doa_v2 conventions):
    //  - UCA: element k at angle -2*pi*k/M (clockwise ring), ANT0 on +x
    //  - ULA: elements along +x, x_k = k * spacing
    for (int k = 0; k < M; k++) {
        if (d_is_ula) {
            d_xpos[k] = (double)k * r;
            d_ypos[k] = 0.0;
        } else {
            const double a = UCA_ANGLE_SIGN * 2.0 * M_PI * (double)k / (double)M;
            d_xpos[k] = r * std::cos(a);
            d_ypos[k] = r * std::sin(a);
        }
    }

    // Steering (kraken_doa_v2 conventions), unit-norm columns:
    //  - UCA/generic: exp(+j * 2*pi * (x*cos(theta) + y*sin(theta)))
    //  - ULA: exp(+j * 2*pi * x*sin(theta)) — the v2 ULA frame: 0 deg is
    //    broadside, ambiguity theta <-> 180-theta. Using the generic x/y form
    //    here would rotate ULA bearings by 90 deg vs kraken_doa_v2.
    for (int b = 0; b < NUM_ANGLES; b++) {
        const double theta = (double)b * M_PI / 180.0;
        const double ct = std::cos(theta);
        const double st = std::sin(theta);
        for (int k = 0; k < M; k++) {
            const double phase =
                d_is_ula ? 2.0 * M_PI * d_xpos[k] * st
                         : 2.0 * M_PI * (d_xpos[k] * ct + d_ypos[k] * st);
            d_S(k, b) = std::complex<double>(std::cos(phase), std::sin(phase));
        }
        d_S.col(b).normalize();
    }

    d_freq_mhz = freq_mhz;
}

void doa_music_impl::set_freq(double freq_mhz)
{
    if (!(freq_mhz > 0.0)) {
        d_logger->warn("set_freq: ignoring non-positive frequency {} MHz", freq_mhz);
        return;
    }
    std::lock_guard<std::mutex> lock(d_mutex);
    generate_steering(freq_mhz);
}

void doa_music_impl::set_array_dist(double array_dist_m)
{
    if (!(array_dist_m > 0.0)) {
        d_logger->warn("set_array_dist: ignoring non-positive distance {} m",
                       array_dist_m);
        return;
    }
    std::lock_guard<std::mutex> lock(d_mutex);
    d_array_dist_m = array_dist_m;
    generate_steering(d_freq_mhz);
}

void doa_music_impl::set_array_type(const std::string& array_type)
{
    bool is_ula;
    try {
        is_ula = parse_array_type(array_type);
    } catch (const std::invalid_argument&) {
        d_logger->warn("set_array_type: ignoring unknown array type \"{}\" "
                       "(want \"UCA\" or \"ULA\")",
                       array_type);
        return;
    }
    std::lock_guard<std::mutex> lock(d_mutex);
    if (is_ula == d_is_ula) {
        return;
    }
    d_is_ula = is_ula;
    generate_steering(d_freq_mhz);
}

int doa_music_impl::work(int noutput_items,
                         gr_vector_const_void_star& input_items,
                         gr_vector_void_star& output_items)
{
    const int M = d_num_elements;
    const int N = d_vec_len;

    float* out_psd = (float*)output_items[0];
    float* out_angle = (float*)output_items[1];
    float* out_conf = (float*)output_items[2];

    for (int item = 0; item < noutput_items; item++) {
        // 1. Stack the input snapshot block into X (M x N), upcast to double
        for (int m = 0; m < M; m++) {
            const gr_complex* in = ((const gr_complex*)input_items[m]) +
                                   (size_t)item * (size_t)N;
            for (int n = 0; n < N; n++) {
                d_X(m, n) = std::complex<double>(in[n].real(), in[n].imag());
            }
        }

        // 2. R = X * X^H / N (rank update on lower triangle, then mirror)
        d_R.setZero();
        d_R.selfadjointView<Eigen::Lower>().rankUpdate(d_X, 1.0 / (double)N);
        d_R.triangularView<Eigen::StrictlyUpper>() =
            d_R.triangularView<Eigen::StrictlyLower>().adjoint();

        const double trace_r = d_R.trace().real();
        if (trace_r > 1e-15) {
            d_R *= (1.0 / trace_r);
        } else {
            const auto now = std::chrono::steady_clock::now();
            if (now - d_last_warn >= std::chrono::seconds(5)) {
                d_last_warn = now;
                d_logger->warn(
                    "covariance trace ~0 (all-zero input?); DoA output degenerate");
            }
        }
        d_R.diagonal().array() += 1e-12;

        // 3. Eigendecomposition (SelfAdjointEigenSolver: ascending eigenvalues,
        //    noise subspace = first M-K eigenvectors)
        d_solver.compute(d_R);

        // 4./5. Pseudospectrum P(theta) = 1 / max(||E^H s||^2, 1e-15).
        //    Steering matrix shared with set_freq() -> compute under the mutex.
        {
            std::lock_guard<std::mutex> lock(d_mutex);
            d_EHS.noalias() =
                d_solver.eigenvectors().leftCols(d_noise_dim).adjoint() * d_S;
        }

        double p_max = 0.0;
        double p_sum = 0.0;
        int max_idx = 0;
        for (int b = 0; b < NUM_ANGLES; b++) {
            double denom = d_EHS.col(b).squaredNorm();
            if (denom < 1e-15) {
                denom = 1e-15;
            }
            const double p = 1.0 / denom;
            d_psd[b] = p;
            p_sum += p;
            if (p > p_max) {
                p_max = p;
                max_idx = b;
            }
        }

        // 7. out0: 10*log10(P/Pmax), floored at -100 dB
        float* o = out_psd + (size_t)item * NUM_ANGLES;
        for (int b = 0; b < NUM_ANGLES; b++) {
            double v = 10.0 * std::log10(d_psd[b] / p_max);
            if (v < -100.0) {
                v = -100.0;
            }
            o[b] = (float)v;
        }

        // 6. Peak: 3-point parabolic vertex on ln(P), circular indexing,
        //    delta clamped to +-0.5 bin
        const double y1 = std::log(d_psd[(max_idx + NUM_ANGLES - 1) % NUM_ANGLES]);
        const double y2 = std::log(d_psd[max_idx]);
        const double y3 = std::log(d_psd[(max_idx + 1) % NUM_ANGLES]);
        const double denom = y1 - 2.0 * y2 + y3;
        double delta = 0.0;
        if (denom < -1e-12) {
            delta = std::clamp(0.5 * (y1 - y3) / denom, -0.5, 0.5);
        }
        double angle = (double)max_idx + delta; // 1 deg per bin
        if (angle < 0.0) {
            angle += 360.0;
        } else if (angle >= 360.0) {
            angle -= 360.0;
        }
        out_angle[item] = (float)angle;

        // Confidence: peak-to-mean of the linear pseudospectrum
        const double p_mean = p_sum / (double)NUM_ANGLES;
        double conf = (p_mean > 0.0) ? (p_max / p_mean - 1.0) / 3.0 : 0.0;
        conf = std::clamp(conf, 0.0, 1.0);
        out_conf[item] = (float)conf;
    }

    return noutput_items;
}

} // namespace krakensdr_v2
} // namespace gr
