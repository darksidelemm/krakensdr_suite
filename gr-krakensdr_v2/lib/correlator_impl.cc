/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "correlator_impl.h"
#include <gnuradio/io_signature.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace gr {
namespace krakensdr_v2 {

namespace {

// The FFTW planner is not thread-safe; serialize plan create/destroy.
std::mutex& fftw_planner_mutex()
{
    static std::mutex m;
    return m;
}

int validated_vec_len(int vec_len)
{
    if (vec_len < 2) {
        throw std::invalid_argument("correlator: vec_len must be >= 2");
    }
    return vec_len;
}

int validated_fft_cut(int fft_cut, int vec_len)
{
    if (fft_cut < 2 || fft_cut > 2 * vec_len) {
        throw std::invalid_argument(
            "correlator: fft_cut must be in [2, 2*vec_len]");
    }
    return fft_cut;
}

} // namespace

correlator::sptr correlator::make(int vec_len, int fft_cut)
{
    return gnuradio::make_block_sptr<correlator_impl>(vec_len, fft_cut);
}

correlator_impl::correlator_impl(int vec_len, int fft_cut)
    : gr::sync_block(
          "correlator",
          gr::io_signature::make(
              2, 2, sizeof(gr_complex) * (size_t)validated_vec_len(vec_len)),
          gr::io_signature::makev(
              3,
              3,
              std::vector<int>{ (int)(sizeof(float) *
                                      (size_t)validated_fft_cut(fft_cut, vec_len)),
                                (int)sizeof(float),
                                (int)sizeof(float) })),
      d_vec_len(vec_len),
      d_fft_len(2 * vec_len),
      d_fft_cut(fft_cut)
{
    d_buf_a = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * d_fft_len);
    d_buf_b = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * d_fft_len);
    d_buf_c = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * d_fft_len);
    if (!d_buf_a || !d_buf_b || !d_buf_c) {
        fftwf_free(d_buf_a);
        fftwf_free(d_buf_b);
        fftwf_free(d_buf_c);
        throw std::runtime_error("correlator: fftwf_malloc failed");
    }

    {
        std::lock_guard<std::mutex> lk(fftw_planner_mutex());
        d_plan_a =
            fftwf_plan_dft_1d(d_fft_len, d_buf_a, d_buf_a, FFTW_FORWARD, FFTW_ESTIMATE);
        d_plan_b =
            fftwf_plan_dft_1d(d_fft_len, d_buf_b, d_buf_b, FFTW_FORWARD, FFTW_ESTIMATE);
        d_plan_c =
            fftwf_plan_dft_1d(d_fft_len, d_buf_c, d_buf_c, FFTW_BACKWARD, FFTW_ESTIMATE);
    }
    if (!d_plan_a || !d_plan_b || !d_plan_c) {
        throw std::runtime_error("correlator: FFTW plan creation failed");
    }
}

correlator_impl::~correlator_impl()
{
    {
        std::lock_guard<std::mutex> lk(fftw_planner_mutex());
        if (d_plan_a)
            fftwf_destroy_plan(d_plan_a);
        if (d_plan_b)
            fftwf_destroy_plan(d_plan_b);
        if (d_plan_c)
            fftwf_destroy_plan(d_plan_c);
    }
    fftwf_free(d_buf_a);
    fftwf_free(d_buf_b);
    fftwf_free(d_buf_c);
}

int correlator_impl::work(int noutput_items,
                          gr_vector_const_void_star& input_items,
                          gr_vector_void_star& output_items)
{
    const int N = d_vec_len;
    const int L = d_fft_len;
    const float inv_l = 1.0f / (float)L;

    const gr_complex* in0 = (const gr_complex*)input_items[0];
    const gr_complex* in1 = (const gr_complex*)input_items[1];
    float* out_xc = (float*)output_items[0];
    float* out_phase = (float*)output_items[1];
    float* out_lag = (float*)output_items[2];

    for (int item = 0; item < noutput_items; item++) {
        const gr_complex* x = in0 + (size_t)item * N;
        const gr_complex* y = in1 + (size_t)item * N;

        // A = [x, zeros], B = [zeros, y]  (zero lag lands at index N)
        memcpy(d_buf_a, x, sizeof(fftwf_complex) * N);
        memset(d_buf_a + N, 0, sizeof(fftwf_complex) * N);
        memset(d_buf_b, 0, sizeof(fftwf_complex) * N);
        memcpy(d_buf_b + N, y, sizeof(fftwf_complex) * N);

        fftwf_execute(d_plan_a);
        fftwf_execute(d_plan_b);

        // C = conj(A) * B, then IFFT (scaled 1/L) = linear cross-correlation
        for (int k = 0; k < L; k++) {
            const float ar = d_buf_a[k][0], ai = d_buf_a[k][1];
            const float br = d_buf_b[k][0], bi = d_buf_b[k][1];
            d_buf_c[k][0] = ar * br + ai * bi;  // Re{conj(a)*b}
            d_buf_c[k][1] = ar * bi - ai * br;  // Im{conj(a)*b}
        }
        fftwf_execute(d_plan_c);

        // Global peak over the full correlation (for the lag output) and
        // window stats — single pass over |xc|^2.
        int max_idx = 0;
        float max_m2 = 0.0f;
        for (int k = 0; k < L; k++) {
            const float re = d_buf_c[k][0] * inv_l;
            const float im = d_buf_c[k][1] * inv_l;
            const float m2 = re * re + im * im;
            d_buf_c[k][0] = re; // store the scaled value back
            d_buf_c[k][1] = im;
            if (m2 > max_m2) {
                max_m2 = m2;
                max_idx = k;
            }
        }

        // out0: dB magnitude of the centered fft_cut window, window max = 0 dB
        float* o = out_xc + (size_t)item * d_fft_cut;
        const int start = N - d_fft_cut / 2;
        float win_max_m2 = 0.0f;
        for (int k = 0; k < d_fft_cut; k++) {
            const int idx = start + k; // in [0, L) because fft_cut <= L
            const float re = d_buf_c[idx][0];
            const float im = d_buf_c[idx][1];
            const float m2 = re * re + im * im;
            o[k] = m2;
            if (m2 > win_max_m2)
                win_max_m2 = m2;
        }
        const float norm = (win_max_m2 > 0.0f) ? 1.0f / win_max_m2 : 1.0f;
        for (int k = 0; k < d_fft_cut; k++) {
            // 10*log10(|xc|/max) == 5*log10(|xc|^2/max^2)
            o[k] = 5.0f * log10f(o[k] * norm + 1e-20f);
        }

        // out1: phase at the zero-lag bin, degrees
        out_phase[item] =
            atan2f(d_buf_c[N][1], d_buf_c[N][0]) * (180.0f / (float)M_PI);

        // out2: peak lag in samples, sub-sample parabolic refinement on the
        // magnitude. Positive = input 1 delayed relative to input 0.
        const auto mag_at = [&](int k) {
            k = (k % L + L) % L;
            return sqrtf(d_buf_c[k][0] * d_buf_c[k][0] + d_buf_c[k][1] * d_buf_c[k][1]);
        };
        const float y1 = mag_at(max_idx - 1);
        const float y2 = mag_at(max_idx);
        const float y3 = mag_at(max_idx + 1);
        const float denom = y1 - 2.0f * y2 + y3;
        float delta = 0.0f;
        if (denom < -1e-12f) {
            delta = std::clamp(0.5f * (y1 - y3) / denom, -0.5f, 0.5f);
        }
        out_lag[item] = (float)(max_idx - N) + delta;
    }

    return noutput_items;
}

} // namespace krakensdr_v2
} // namespace gr
