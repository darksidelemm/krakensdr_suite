/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_DOA_MUSIC_IMPL_H
#define INCLUDED_KRAKENSDR_V2_DOA_MUSIC_IMPL_H

#include <gnuradio/krakensdr_v2/doa_music.h>

#include <Eigen/Dense>

#include <chrono>
#include <mutex>
#include <vector>

namespace gr {
namespace krakensdr_v2 {

class doa_music_impl : public doa_music
{
private:
    static constexpr int NUM_ANGLES = 360; // 1 deg grid, fixed (kraken_doa_v2 parity)

    const int d_vec_len;      // N, snapshots per input vector item
    const int d_num_elements; // M
    const int d_signal_dim;   // K, clamped to [1, M-1]
    const int d_noise_dim;    // M - K
    bool d_is_ula;         // guarded by d_mutex
    double d_array_dist_m; // guarded by d_mutex
    double d_freq_mhz;     // guarded by d_mutex

    // Guards d_S (plus d_freq_mhz / d_array_dist_m / d_is_ula) against
    // setters from another thread while work() computes the spectrum.
    // Coarse-grained by design (see DESIGN.md).
    std::mutex d_mutex;

    // Element positions in wavelengths, regenerated with the steering matrix
    std::vector<double> d_xpos;
    std::vector<double> d_ypos;

    // Pre-allocated work objects — no allocation in work()
    Eigen::MatrixXcd d_X;   // M x N snapshot matrix (upcast to double)
    Eigen::MatrixXcd d_R;   // M x M spatial covariance
    Eigen::MatrixXcd d_S;   // M x 360 steering matrix (unit-norm columns)
    Eigen::MatrixXcd d_EHS; // (M-K) x 360 workspace: E^H * S
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXcd> d_solver;
    double d_psd[NUM_ANGLES]; // linear pseudospectrum P(theta)

    std::chrono::steady_clock::time_point d_last_warn; // >= 5 s log throttle

    //! Regenerate d_S for freq_mhz. Caller must hold d_mutex (except in ctor).
    void generate_steering(double freq_mhz);

public:
    doa_music_impl(int vec_len,
                   double freq_mhz,
                   double array_dist_m,
                   int num_elements,
                   const std::string& array_type,
                   int signal_dimension);
    ~doa_music_impl() override;

    void set_freq(double freq_mhz) override;
    void set_array_dist(double array_dist_m) override;
    void set_array_type(const std::string& array_type) override;

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_DOA_MUSIC_IMPL_H */
