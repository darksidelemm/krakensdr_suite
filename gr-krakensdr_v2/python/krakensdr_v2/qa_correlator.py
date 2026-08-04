#!/usr/bin/env python3
#
# Copyright 2026 KrakenRF Inc.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Synthetic self-test for the krakensdr_v2 correlator block: known phase
# rotations and integer delays must come out at the documented sign
# conventions (positive lag = input 1 delayed relative to input 0; phase
# output = arg(sum x* y), so y = x*exp(j*phi) reads +phi).

from gnuradio import gr, gr_unittest
from gnuradio import blocks
import numpy as np

try:
    from gnuradio import krakensdr_v2
except ImportError:
    import os
    import sys
    dirname, filename = os.path.split(os.path.abspath(__file__))
    sys.path.append(os.path.join(dirname, "bindings"))
    from gnuradio import krakensdr_v2

VEC_LEN = 4096
FFT_CUT = 256


class qa_correlator(gr_unittest.TestCase):
    def setUp(self):
        self.tb = gr.top_block()
        rng = np.random.default_rng(1234)
        self.x = (rng.standard_normal(VEC_LEN) +
                  1j * rng.standard_normal(VEC_LEN)).astype(np.complex64)

    def tearDown(self):
        self.tb = None

    def _run(self, x, y):
        src0 = blocks.vector_source_c(x.tolist(), False, VEC_LEN)
        src1 = blocks.vector_source_c(y.tolist(), False, VEC_LEN)
        corr = krakensdr_v2.correlator(VEC_LEN, FFT_CUT)
        snk_xc = blocks.vector_sink_f(FFT_CUT)
        snk_ph = blocks.vector_sink_f(1)
        snk_lag = blocks.vector_sink_f(1)
        self.tb.connect(src0, (corr, 0))
        self.tb.connect(src1, (corr, 1))
        self.tb.connect((corr, 0), snk_xc)
        self.tb.connect((corr, 1), snk_ph)
        self.tb.connect((corr, 2), snk_lag)
        self.tb.run()
        return (np.array(snk_xc.data()), snk_ph.data()[0], snk_lag.data()[0])

    def test_001_phase_rotation(self):
        # y = x * exp(+j*30deg): zero lag, phase +30 deg, peak at center bin
        y = (self.x * np.exp(1j * np.deg2rad(30.0))).astype(np.complex64)
        xc, phase, lag = self._run(self.x, y)
        self.assertAlmostEqual(lag, 0.0, delta=0.1)
        self.assertAlmostEqual(phase, 30.0, delta=0.5)
        peak_bin = int(np.argmax(xc))
        self.assertEqual(peak_bin, FFT_CUT // 2)
        self.assertAlmostEqual(xc[peak_bin], 0.0, delta=0.01)  # normalized peak

    def test_002_integer_delay(self):
        # y[n] = x[n-7] (input 1 delayed by 7 samples) -> lag = +7
        y = np.zeros(VEC_LEN, dtype=np.complex64)
        y[7:] = self.x[:-7]
        xc, phase, lag = self._run(self.x, y)
        self.assertAlmostEqual(lag, 7.0, delta=0.1)
        peak_bin = int(np.argmax(xc))
        self.assertEqual(peak_bin, FFT_CUT // 2 + 7)

    def test_003_negative_delay(self):
        # y[n] = x[n+5] (input 1 ahead by 5 samples) -> lag = -5
        y = np.zeros(VEC_LEN, dtype=np.complex64)
        y[:-5] = self.x[5:]
        xc, phase, lag = self._run(self.x, y)
        self.assertAlmostEqual(lag, -5.0, delta=0.1)

    def test_004_uncorrelated_floor(self):
        # independent noise: peak/median contrast far below the correlated case
        rng = np.random.default_rng(999)
        y = (rng.standard_normal(VEC_LEN) +
             1j * rng.standard_normal(VEC_LEN)).astype(np.complex64)
        xc, _, _ = self._run(self.x, y)
        # correlated pair peaks ~40 dB above the floor; uncorrelated ~ <15 dB
        self.assertLess(-np.median(xc), 15.0)


if __name__ == '__main__':
    gr_unittest.run(qa_correlator)
