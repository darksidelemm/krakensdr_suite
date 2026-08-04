/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_CORRELATOR_H
#define INCLUDED_KRAKENSDR_V2_CORRELATOR_H

#include <gnuradio/krakensdr_v2/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
namespace krakensdr_v2 {

/*!
 * \brief FFT-based cross-correlator for coherence checking.
 * \ingroup krakensdr_v2
 *
 * Computes the full linear cross-correlation of two complex vectors of
 * length vec_len via zero-padded FFTs of length 2*vec_len.
 *
 * Outputs per input item:
 *  - out0 "xcorr": float[fft_cut], correlation magnitude in dB normalized to
 *                  peak = 0 dB, centered on zero lag (bin fft_cut/2 = lag 0)
 *  - out1 "phase": float, phase at the zero-lag bin, degrees
 *  - out2 "lag":   float, lag of the correlation peak in samples (sub-sample
 *                  parabolic interpolation; positive = input 1 lags input 0)
 *
 * On a calibrated KrakenSDR the peak sits at lag 0 with a stable phase.
 */
class KRAKENSDR_V2_API correlator : virtual public gr::sync_block
{
public:
    typedef std::shared_ptr<correlator> sptr;

    /*!
     * \param vec_len Input vector length (samples per correlation)
     * \param fft_cut Number of lag bins to output, centered on zero lag
     */
    static sptr make(int vec_len, int fft_cut = 2048);
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_CORRELATOR_H */
