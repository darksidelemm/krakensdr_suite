/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * kraken_corr_test — cross-correlation coherence check for Heimdall v2.
 *
 * Cross-correlates channel 0 against channels 1..4 and prints, once per
 * second, the correlation peak lag, zero-lag phase and peak quality for each
 * pair. On a calibrated KrakenSDR the lag is ~0 samples with a stable phase.
 *
 * The source is configured with coherent_only=false so calibration-noise
 * frames also flow — heimdall's noise source produces the strongest
 * correlation peaks (it can be forced on from the heimdall web UI on port
 * 8070 with the noise-source/bias-tee toggle).
 *
 *   kraken_corr_test [--ip 127.0.0.1] [--freq 100.0] [--gain 40.2] [--seconds 0]
 */

#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/io_signature.h>
#include <gnuradio/krakensdr_v2/correlator.h>
#include <gnuradio/krakensdr_v2/krakensdr_source.h>
#include <gnuradio/sync_block.h>
#include <gnuradio/top_block.h>

#include <algorithm>
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

constexpr int VEC_LEN = 16384;
constexpr int FFT_CUT = 2048;
constexpr int NUM_CH = 5;
constexpr int NUM_PAIRS = NUM_CH - 1;

std::atomic<bool> g_stop{ false };
void sigint_handler(int) { g_stop.store(true); }

// Console sink: NUM_PAIRS groups of (xcorr[FFT_CUT], phase, lag) inputs.
// Port layout: 3*p = xcorr, 3*p+1 = phase, 3*p+2 = lag for pair p (ch0 x ch{p+1}).
class corr_console_sink : public gr::sync_block
{
public:
    explicit corr_console_sink(gr::krakensdr_v2::krakensdr_source::sptr src)
        : gr::sync_block("corr_console_sink",
                         gr::io_signature::makev(3 * NUM_PAIRS, 3 * NUM_PAIRS, sizes()),
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
            const int item = noutput_items - 1; // most recent
            printf("--- phase_state=%u%s  noise=%s  (lag ~0 + stable phase = "
                   "coherent) ---\n",
                   d_src->phase_state(),
                   d_src->phase_state() == 4 ? " (CONVERGED)" : "",
                   d_src->noise_source_active() ? "ON" : "off");
            for (int p = 0; p < NUM_PAIRS; p++) {
                const float* xc =
                    ((const float*)input_items[3 * p]) + (size_t)item * FFT_CUT;
                const float phase = ((const float*)input_items[3 * p + 1])[item];
                const float lag = ((const float*)input_items[3 * p + 2])[item];

                // peak quality: 0 dB peak vs the window median
                static thread_local std::vector<float> tmp(FFT_CUT);
                memcpy(tmp.data(), xc, sizeof(float) * FFT_CUT);
                std::nth_element(tmp.begin(), tmp.begin() + FFT_CUT / 2, tmp.end());
                const float median = tmp[FFT_CUT / 2];

                printf("CH0xCH%d  lag %8.2f samp   phase %8.2f deg   peak/median "
                       "%5.1f dB\n",
                       p + 1,
                       lag,
                       phase,
                       -median);
            }
            fflush(stdout);
        }
        return noutput_items;
    }

private:
    static std::vector<int> sizes()
    {
        std::vector<int> s;
        for (int p = 0; p < NUM_PAIRS; p++) {
            s.push_back(sizeof(float) * FFT_CUT);
            s.push_back(sizeof(float));
            s.push_back(sizeof(float));
        }
        return s;
    }

    gr::krakensdr_v2::krakensdr_source::sptr d_src;
    std::chrono::steady_clock::time_point d_last;
};

void usage(const char* argv0)
{
    fprintf(stderr,
            "Usage: %s [--ip ADDR] [--freq MHZ] [--gain DB] [--seconds N]\n"
            "  --ip       heimdall_v2 server address (default 127.0.0.1)\n"
            "  --freq     center frequency in MHz (default 100.0)\n"
            "  --gain     tuner gain in dB, negative = AGC (default 40.2)\n"
            "  --seconds  run time, 0 = until Ctrl-C (default 0)\n",
            argv0);
}

} // namespace

int main(int argc, char** argv)
{
    std::string ip = "127.0.0.1";
    double freq = 100.0, gain = 40.2;
    int seconds = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--ip") && i + 1 < argc)
            ip = argv[++i];
        else if (!strcmp(argv[i], "--freq") && i + 1 < argc)
            freq = atof(argv[++i]);
        else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
            gain = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return 1;
        }
    }

    printf("kraken_corr_test: heimdall %s, %.3f MHz, gain %.1f dB, %d-sample "
           "correlations (includes calibration-noise frames)\n",
           ip.c_str(),
           freq,
           gain,
           VEC_LEN);

    auto tb = gr::make_top_block("kraken_corr_test");
    auto src = gr::krakensdr_v2::krakensdr_source::make(
        ip, 8091, 8092, NUM_CH, freq, gain, /*coherent_only=*/false, true, false);
    auto sink = gnuradio::make_block_sptr<corr_console_sink>(src);

    std::vector<gr::blocks::stream_to_vector::sptr> s2v(NUM_CH);
    for (int c = 0; c < NUM_CH; c++) {
        s2v[c] = gr::blocks::stream_to_vector::make(sizeof(gr_complex), VEC_LEN);
        tb->connect(src, c, s2v[c], 0);
    }
    for (int p = 0; p < NUM_PAIRS; p++) {
        auto corr = gr::krakensdr_v2::correlator::make(VEC_LEN, FFT_CUT);
        tb->connect(s2v[0], 0, corr, 0);
        tb->connect(s2v[p + 1], 0, corr, 1);
        tb->connect(corr, 0, sink, 3 * p);
        tb->connect(corr, 1, sink, 3 * p + 1);
        tb->connect(corr, 2, sink, 3 * p + 2);
    }

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
    printf("kraken_corr_test: done\n");
    return 0;
}
