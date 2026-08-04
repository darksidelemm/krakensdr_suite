#!/usr/bin/env python3
#
# Copyright 2026 KrakenRF Inc.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Synthetic self-test for the krakensdr_v2 doa_music block: a plane wave
# synthesized with the kraken_doa_v2 geometry conventions (UCA ring wired
# CLOCKWISE, ANT0 at +x, steering exp(+j*2*pi/lambda*(x*cos(az)+y*sin(az))))
# must peak at the true azimuth - the reported azimuth stays unit-circle CCW
# regardless of the ring chirality, which is the invariant under test.

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

C = 299792458.0
VEC_LEN = 1024
M = 5


def synth_plane_wave(az_deg, freq_mhz, radius_m, ula=False, seed=42):
    """M x VEC_LEN samples of a plane wave from azimuth az_deg (+ noise).

    Uses the kraken_doa_v2 steering conventions the block must match:
    UCA exp(+j*2pi*(x*cos(az)+y*sin(az))) with CLOCKWISE elements; ULA
    exp(+j*2pi*x*sin(az)) (0 deg = broadside, az ambiguous with 180-az).
    """
    lam = C / (freq_mhz * 1e6)
    az = np.deg2rad(az_deg)
    if ula:
        x = np.arange(M) * radius_m / lam
        steer = np.exp(1j * 2.0 * np.pi * x * np.sin(az))
    else:
        # Clockwise ring (UCA_ANGLE_SIGN = -1 in doa_music_impl.cc), ANT0 on +x
        ang = -2.0 * np.pi * np.arange(M) / M
        x = radius_m / lam * np.cos(ang)
        y = radius_m / lam * np.sin(ang)
        steer = np.exp(1j * 2.0 * np.pi * (x * np.cos(az) + y * np.sin(az)))
    rng = np.random.default_rng(seed)
    tone = np.exp(1j * 2.0 * np.pi * 0.01 * np.arange(VEC_LEN))
    sig = np.outer(steer, tone)
    sig += 1e-3 * (rng.standard_normal(sig.shape) +
                   1j * rng.standard_normal(sig.shape))
    return sig.astype(np.complex64)


class qa_doa_music(gr_unittest.TestCase):
    def setUp(self):
        self.tb = gr.top_block()

    def tearDown(self):
        self.tb = None

    def _run(self, sig, freq_mhz, radius_m, array_type):
        music = krakensdr_v2.doa_music(VEC_LEN, freq_mhz, radius_m, M,
                                       array_type, 1)
        snk_spec = blocks.vector_sink_f(360)
        snk_angle = blocks.vector_sink_f(1)
        snk_conf = blocks.vector_sink_f(1)
        for m in range(M):
            src = blocks.vector_source_c(sig[m].tolist(), False, VEC_LEN)
            self.tb.connect(src, (music, m))
        self.tb.connect((music, 0), snk_spec)
        self.tb.connect((music, 1), snk_angle)
        self.tb.connect((music, 2), snk_conf)
        self.tb.run()
        return (np.array(snk_spec.data()), snk_angle.data()[0],
                snk_conf.data()[0])

    def test_001_uca_azimuth(self):
        for az in (0.0, 77.0, 191.0, 305.0):
            tb = gr.top_block()
            self.tb = tb
            sig = synth_plane_wave(az, 433.0, 0.25)
            spec, angle, conf = self._run(sig, 433.0, 0.25, "UCA")
            err = (angle - az + 180.0) % 360.0 - 180.0
            self.assertLess(abs(err), 2.0,
                            f"UCA az {az}: got {angle:.1f} (err {err:.1f})")
            self.assertGreater(conf, 0.5)
            # spectrum peak bin agrees with the angle output
            self.assertLess(abs(((np.argmax(spec) - az + 180) % 360) - 180), 2.5)

    def test_002_ula_azimuth(self):
        # v2 ULA frame (steering ~ sin(theta)): 0 deg is broadside and a
        # source at az aliases with 180-az (sin equality). A source at 30 deg
        # must read ~30 or ~150 — NOT ~60/120 (which the generic x*cos(theta)
        # form would produce, rotated 90 deg from kraken_doa_v2).
        az = 30.0
        sig = synth_plane_wave(az, 433.0, 0.10, ula=True)
        spec, angle, conf = self._run(sig, 433.0, 0.10, "ULA")
        err1 = abs((angle - 30.0 + 180.0) % 360.0 - 180.0)
        err2 = abs((angle - 150.0 + 180.0) % 360.0 - 180.0)
        self.assertLess(min(err1, err2), 2.0,
                        f"ULA: got {angle:.1f}, expected ~30 or ~150")

    def test_003_normalized_output(self):
        sig = synth_plane_wave(120.0, 433.0, 0.25)
        spec, _, _ = self._run(sig, 433.0, 0.25, "UCA")
        self.assertAlmostEqual(float(np.max(spec)), 0.0, delta=1e-3)
        self.assertGreaterEqual(float(np.min(spec)), -100.0)


if __name__ == '__main__':
    gr_unittest.run(qa_doa_music)
