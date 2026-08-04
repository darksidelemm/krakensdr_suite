/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * kraken_music_doa — MUSIC direction finding with Heimdall v2.
 *
 * Decimates all channels to 100 kHz with identical low-pass filters (cross-
 * channel phase preserved), runs MUSIC and prints, once per second, the
 * estimated bearing, confidence and a coarse ASCII pseudospectrum.
 *
 * Angle convention: unit circle — 0 deg = antenna 0 direction (+x), counter-
 * clockwise positive (same as kraken_doa_v2). Requires converged calibration
 * (phase_state 4) and the antenna array connected.
 *
 *   kraken_music_doa [--ip 127.0.0.1] [--freq 100.0] [--gain 40.2]
 *                    [--radius 0.25] [--array UCA|ULA] [--seconds 0]
 */

#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/fft/window.h>
#include <gnuradio/filter/fir_filter_blk.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/krakensdr_v2/doa_music.h>
#include <gnuradio/krakensdr_v2/krakensdr_source.h>
#include <gnuradio/sync_block.h>
#include <gnuradio/top_block.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

constexpr int NUM_CH = 5;
constexpr int DECIM = 24; // 2.4 MSPS -> 100 kHz
constexpr int VEC_LEN = 2048;
constexpr int NUM_ANGLES = 360;
constexpr double SAMP_RATE = 2.4e6;

std::atomic<bool> g_stop{ false };
void sigint_handler(int) { g_stop.store(true); }

// Console sink: doa spectrum[360], angle, confidence -> 1 Hz report.
class doa_console_sink : public gr::sync_block
{
public:
    explicit doa_console_sink(gr::krakensdr_v2::krakensdr_source::sptr src)
        : gr::sync_block("doa_console_sink",
                         gr::io_signature::makev(
                             3,
                             3,
                             std::vector<int>{ (int)(sizeof(float) * NUM_ANGLES),
                                               (int)sizeof(float),
                                               (int)sizeof(float) }),
                         gr::io_signature::make(0, 0, 0)),
          d_src(std::move(src)),
          d_last(std::chrono::steady_clock::now())
    {
    }

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star&) override
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - d_last >= std::chrono::seconds(1)) {
            d_last = now;
            const int item = noutput_items - 1;
            const float* spec =
                ((const float*)input_items[0]) + (size_t)item * NUM_ANGLES;
            const float angle = ((const float*)input_items[1])[item];
            const float conf = ((const float*)input_items[2])[item];

            // 36-char ASCII pseudospectrum: max-projection of 10-deg chunks,
            // dB mapped -30..0 onto ' .:-=+*#%@'
            static const char* ramp = " .:-=+*#%@";
            char bar[NUM_ANGLES / 10 + 1];
            for (int b = 0; b < NUM_ANGLES / 10; b++) {
                float mx = -100.0f;
                for (int k = 0; k < 10; k++)
                    mx = std::max(mx, spec[b * 10 + k]);
                int idx = (int)((mx + 30.0f) / 30.0f * 9.0f + 0.5f);
                idx = std::min(9, std::max(0, idx));
                bar[b] = ramp[idx];
            }
            bar[NUM_ANGLES / 10] = '\0';

            const uint32_t ps = d_src->phase_state();
            printf("DoA %6.1f deg  conf %4.2f  |%s|  0..350 deg%s\n",
                   angle,
                   conf,
                   bar,
                   ps == 4 ? "" : "  [WARNING: phase_state != 4, not calibrated]");
            fflush(stdout);
        }
        return noutput_items;
    }

private:
    gr::krakensdr_v2::krakensdr_source::sptr d_src;
    std::chrono::steady_clock::time_point d_last;
};

void usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [--ip ADDR] [--freq MHZ] [--gain DB] [--radius M] "
            "[--array UCA|ULA] [--seconds N]\n"
            "  --ip       heimdall_v2 server address (default 127.0.0.1)\n"
            "  --freq     center frequency in MHz (default 100.0)\n"
            "  --gain     tuner gain in dB, negative = AGC (default 40.2)\n"
            "  --radius   UCA radius / ULA spacing in meters (default 0.25)\n"
            "  --array    UCA or ULA (default UCA)\n"
            "  --seconds  run time, 0 = until Ctrl-C (default 0)\n",
            argv0);
}

} // namespace

int main(int argc, char** argv)
{
    std::string ip = "127.0.0.1";
    std::string array = "UCA";
    double freq = 100.0, gain = 40.2, radius = 0.25;
    int seconds = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ip") && i + 1 < argc)
            ip = argv[++i];
        else if (!strcmp(argv[i], "--freq") && i + 1 < argc)
            freq = atof(argv[++i]);
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
            gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc)
            radius = atof(argv[++i]);
        else if (!strcmp(argv[i], "--array") && i + 1 < argc)
            array = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    printf("kraken_music_doa: heimdall %s, %.3f MHz, gain %.1f dB, %s radius/"
           "spacing %.3f m, decim %d -> %.0f kHz, %d-sample blocks\n"
           "Angle convention: 0 deg = ANT0 = +x, CCW positive (unit circle).\n"
           "DoA needs converged calibration (phase_state 4) and antennas "
           "connected.\n",
           ip.c_str(),
           freq,
           gain,
           array.c_str(),
           radius,
           DECIM,
           SAMP_RATE / DECIM / 1e3,
           VEC_LEN);

    auto tb = gr::make_top_block("kraken_music_doa");
    auto src = gr::krakensdr_v2::krakensdr_source::make(
        ip, 8091, 8092, NUM_CH, freq, gain, true, true, false);
    auto music =
        gr::krakensdr_v2::doa_music::make(VEC_LEN, freq, radius, NUM_CH, array, 1);
    auto sink = gnuradio::make_block_sptr<doa_console_sink>(src);

    // Identical low-pass for every channel (identical group delay preserves
    // the cross-channel phase relationships MUSIC depends on).
    const std::vector<float> taps = gr::filter::firdes::low_pass(
        1.0, SAMP_RATE, 40e3, 20e3, gr::fft::window::WIN_HAMMING);
    printf("Low-pass: %zu taps, cutoff 40 kHz, transition 20 kHz\n", taps.size());

    for (int c = 0; c < NUM_CH; c++) {
        auto fir = gr::filter::fir_filter_ccf::make(DECIM, taps);
        auto s2v = gr::blocks::stream_to_vector::make(sizeof(gr_complex), VEC_LEN);
        tb->connect(src, c, fir, 0);
        tb->connect(fir, 0, s2v, 0);
        tb->connect(s2v, 0, music, c);
    }
    tb->connect(music, 0, sink, 0);
    tb->connect(music, 1, sink, 1);
    tb->connect(music, 2, sink, 2);

    signal(SIGINT, sigint_handler);
    tb->start();

    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (seconds > 0 &&
            std::chrono::steady_clock::now() - t0 >= std::chrono::seconds(seconds))
            break;
    }

    tb->stop();
    tb->wait();
    printf("kraken_music_doa: done\n");
    return 0;
}
