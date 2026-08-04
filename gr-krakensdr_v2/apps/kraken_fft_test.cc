/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * kraken_fft_test — FFT test for the KrakenSDR / Heimdall v2 GNU Radio source.
 *
 * Connects to a running heimdall_v2 server, FFTs all channels and prints, once
 * per second, per-channel total power and the three strongest spectral peaks.
 * Console equivalent of the kraken_fft_display.grc GUI example.
 *
 *   kraken_fft_test [--ip 127.0.0.1] [--freq 100.0] [--gain 40.2] [--seconds 0]
 */

#include <gnuradio/blocks/stream_to_vector.h>
#include <gnuradio/fft/fft_v.h>
#include <gnuradio/fft/window.h>
#include <gnuradio/io_signature.h>
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

constexpr int FFT_SIZE = 16384;
constexpr int NUM_CH = 5;
constexpr double SAMP_RATE = 2.4e6;

std::atomic<bool> g_stop{ false };
void sigint_handler(int) { g_stop.store(true); }

// Console sink: NUM_CH complex-vector inputs (post-FFT, fftshifted), prints a
// per-channel report once per second.
class fft_console_sink : public gr::sync_block
{
public:
    fft_console_sink(gr::krakensdr_v2::krakensdr_source::sptr src, double freq_mhz)
        : gr::sync_block(
              "fft_console_sink",
              gr::io_signature::make(NUM_CH, NUM_CH, sizeof(gr_complex) * FFT_SIZE),
              gr::io_signature::make(0, 0, 0)),
          d_src(std::move(src)),
          d_freq_mhz(freq_mhz),
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
            const double cf_mhz = d_src->center_freq_hz() > 0.0
                                      ? d_src->center_freq_hz() / 1e6
                                      : d_freq_mhz;
            printf("--- center %.3f MHz  phase_state=%u%s  noise=%s ---\n",
                   cf_mhz,
                   d_src->phase_state(),
                   d_src->phase_state() == 4 ? " (CONVERGED)" : "",
                   d_src->noise_source_active() ? "ON" : "off");
            // report on the most recent item
            const int item = noutput_items - 1;
            for (int c = 0; c < NUM_CH; c++) {
                const gr_complex* in =
                    ((const gr_complex*)input_items[c]) + (size_t)item * FFT_SIZE;
                report_channel(c, in, cf_mhz);
            }
            fflush(stdout);
        }
        return noutput_items; // consume everything
    }

private:
    void report_channel(int c, const gr_complex* in, double cf_mhz)
    {
        // power spectrum in dB (relative), fftshifted input: bin k ->
        // offset (k - FFT_SIZE/2) * fs / FFT_SIZE
        static thread_local std::vector<float> db(FFT_SIZE);
        double total = 0.0;
        for (int k = 0; k < FFT_SIZE; k++) {
            const float m2 = in[k].real() * in[k].real() + in[k].imag() * in[k].imag();
            total += m2;
            db[k] = 10.0f * log10f(m2 + 1e-20f);
        }
        const double total_db = 10.0 * log10(total / FFT_SIZE + 1e-20);

        // top-3 peaks with >=50-bin (~7.3 kHz) separation
        int peaks[3] = { -1, -1, -1 };
        for (int p = 0; p < 3; p++) {
            float best = -1e30f;
            int best_k = -1;
            for (int k = 0; k < FFT_SIZE; k++) {
                bool near = false;
                for (int q = 0; q < p; q++)
                    if (std::abs(k - peaks[q]) < 50)
                        near = true;
                if (!near && db[k] > best) {
                    best = db[k];
                    best_k = k;
                }
            }
            peaks[p] = best_k;
        }

        printf("CH%d  pwr %6.1f dB  peaks:", c, total_db);
        for (int p = 0; p < 3; p++) {
            const int k = peaks[p];
            const double off_hz = ((double)k - FFT_SIZE / 2) * SAMP_RATE / FFT_SIZE;
            printf("  %9.4f MHz (%5.1f dB)", cf_mhz + off_hz / 1e6, db[k]);
        }
        printf("\n");
    }

    gr::krakensdr_v2::krakensdr_source::sptr d_src;
    double d_freq_mhz;
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

    printf("kraken_fft_test: heimdall %s, %.3f MHz, gain %.1f dB, %d channels, "
           "FFT %d\n",
           ip.c_str(),
           freq,
           gain,
           NUM_CH,
           FFT_SIZE);

    auto tb = gr::make_top_block("kraken_fft_test");
    auto src = gr::krakensdr_v2::krakensdr_source::make(
        ip, 8091, 8092, NUM_CH, freq, gain, true, true, false);
    auto sink = gnuradio::make_block_sptr<fft_console_sink>(src, freq);

    const auto window = gr::fft::window::blackman_harris(FFT_SIZE);
    for (int c = 0; c < NUM_CH; c++) {
        auto s2v = gr::blocks::stream_to_vector::make(sizeof(gr_complex), FFT_SIZE);
        auto fft = gr::fft::fft_v<gr_complex, true>::make(FFT_SIZE, window, true);
        tb->connect(src, c, s2v, 0);
        tb->connect(s2v, 0, fft, 0);
        tb->connect(fft, 0, sink, c);
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
    printf("kraken_fft_test: done\n");
    return 0;
}
