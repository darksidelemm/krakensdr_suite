/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_DOA_MUSIC_H
#define INCLUDED_KRAKENSDR_V2_DOA_MUSIC_H

#include <gnuradio/krakensdr_v2/api.h>
#include <gnuradio/sync_block.h>
#include <string>

namespace gr {
namespace krakensdr_v2 {

/*!
 * \brief MUSIC direction-of-arrival estimator (uniform circular/linear array).
 * \ingroup krakensdr_v2
 *
 * Inputs: num_elements complex vector streams (vlen = vec_len), one snapshot
 * block per item, all elements sample-aligned (as produced by
 * krakensdr_source, optionally decimated by identical per-channel filters).
 *
 * Outputs per input item:
 *  - out0 "doa":   float[360] pseudospectrum, normalized to peak = 0 dB,
 *                  10*log10, floored at -100 dB (1 deg per bin)
 *  - out1 "angle": float, interpolated peak angle in degrees [0,360).
 *                  Convention: unit circle, 0 deg = +x = ANT0 direction,
 *                  counter-clockwise positive (matches kraken_doa_v2).
 *  - out2 "conf":  float, confidence 0..1 (peak-to-mean of the linear
 *                  pseudospectrum, min(1, (p/m - 1)/3)).
 *
 * Geometry follows kraken_doa_v2: the UCA ring is wired CLOCKWISE, so element
 * k sits at angle -2*pi*k/M with ANT0 on +x (the mirror is what keeps the
 * reported azimuth CCW), on a ring of radius array_dist_m, steering
 * exp(+j*2*pi/lambda *
 * (x*cos(theta) + y*sin(theta))); ULA elements along +x with spacing
 * array_dist_m, steering exp(+j*2*pi/lambda * x*sin(theta)) — the v2 ULA
 * frame where 0 deg is broadside and theta is ambiguous with 180-theta.
 * All steering columns are unit-norm.
 */
class KRAKENSDR_V2_API doa_music : virtual public gr::sync_block
{
public:
    typedef std::shared_ptr<doa_music> sptr;

    /*!
     * \param vec_len Samples per snapshot block (input vector length)
     * \param freq_mhz Signal frequency in MHz (sets the wavelength)
     * \param array_dist_m UCA radius / ULA inter-element spacing, meters
     * \param num_elements Number of antenna elements (input ports)
     * \param array_type "UCA" or "ULA"
     * \param signal_dimension Assumed number of signal sources (1..M-1)
     */
    static sptr make(int vec_len,
                     double freq_mhz,
                     double array_dist_m = 0.25,
                     int num_elements = 5,
                     const std::string& array_type = "UCA",
                     int signal_dimension = 1);

    //! Update the frequency used for the wavelength (MHz); regenerates steering vectors.
    virtual void set_freq(double freq_mhz) = 0;

    //! Update the UCA radius / ULA spacing (meters); regenerates steering vectors.
    virtual void set_array_dist(double array_dist_m) = 0;

    //! Switch between "UCA" and "ULA" geometry; regenerates steering vectors.
    virtual void set_array_type(const std::string& array_type) = 0;
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_DOA_MUSIC_H */
