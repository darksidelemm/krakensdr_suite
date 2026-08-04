/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_CORRELATOR_IMPL_H
#define INCLUDED_KRAKENSDR_V2_CORRELATOR_IMPL_H

#include <gnuradio/krakensdr_v2/correlator.h>

#include <fftw3.h>

namespace gr {
namespace krakensdr_v2 {

class correlator_impl : public correlator
{
private:
    const int d_vec_len; // N: samples per input vector
    const int d_fft_len; // L = 2N: zero-padded FFT length
    const int d_fft_cut; // output window, centered on zero lag (index N)

    // FFTW3f working buffers (fftwf_malloc-aligned)
    fftwf_complex* d_buf_a; // [x, zeros] -> FFT(A) in place
    fftwf_complex* d_buf_b; // [zeros, y] -> FFT(B) in place
    fftwf_complex* d_buf_c; // conj(A)*B -> IFFT in place = cross-correlation

    fftwf_plan d_plan_a; // forward, in-place on d_buf_a
    fftwf_plan d_plan_b; // forward, in-place on d_buf_b
    fftwf_plan d_plan_c; // inverse, in-place on d_buf_c

public:
    correlator_impl(int vec_len, int fft_cut);
    ~correlator_impl() override;

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_CORRELATOR_IMPL_H */
