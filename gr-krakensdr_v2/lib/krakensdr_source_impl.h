/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_IMPL_H
#define INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_IMPL_H

#include <gnuradio/krakensdr_v2/krakensdr_source.h>
#include <pmt/pmt.h>

#include <netinet/in.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gr {
namespace krakensdr_v2 {

class krakensdr_source_impl : public krakensdr_source
{
private:
    // ---- heimdall_v2 data protocol constants (port 8091) ----
    static constexpr uint32_t TCP_MAGIC = 0x4D434851; // 'MCHQ', big-endian on wire
    static constexpr size_t HEADER_FIXED = 32;        // fixed header before per-ch meta
    static constexpr size_t RX_BUF_SIZE = 4 * 1024 * 1024; // accumulation buffer
    static constexpr uint32_t MAX_WIRE_CHANNELS = 16;       // header sanity gate
    static constexpr uint32_t MAX_WIRE_SAMPLES = 65536;     // header sanity gate
    static constexpr size_t QUEUE_CAP = 16;                 // bounded frame queue
    static constexpr size_t FREELIST_CAP = QUEUE_CAP + 4;   // recycled frame cap

    //! One decoded packet: per-channel converted samples + metadata.
    struct frame_t {
        std::vector<std::vector<gr_complex>> ch; // [num_channels][nsamples]
        size_t nsamples = 0;
        double freq_hz = 0.0; // from ch0 packet metadata
        float gain_db = 0.0f; // from ch0 packet metadata
        uint32_t phase_state = 0;
        bool noise_source = false;
        bool retuning = false;
    };

    // ---- configuration (fixed at construction) ----
    const std::string d_ip;
    const int d_data_port;
    const int d_ctrl_port;
    const int d_num_channels;
    const bool d_coherent_only;
    const bool d_dc_correction;
    const bool d_debug;

    // ---- lifecycle ----
    std::atomic<bool> d_running{ false };
    std::thread d_reader_thread;
    std::thread d_ctrl_thread;

    // Socket fds. Owning thread opens/closes its slot; stop() only shutdown()s
    // to unblock recv(). d_fd_mutex serializes slot access so shutdown never
    // races a close.
    std::mutex d_fd_mutex;
    int d_data_fd = -1;
    int d_ctrl_fd = -1;

    // Self-pipe: wakes the control thread's poll() on enqueue/stop.
    int d_wake_pipe[2] = { -1, -1 };

    // Hostname resolution cache: getaddrinfo blocks uninterruptibly (stop()
    // cannot cancel it), so a non-numeric address is resolved at most once
    // per block lifetime instead of on every reconnect attempt.
    std::atomic<bool> d_addr_cached{ false };
    struct in_addr d_cached_addr {};

    // ---- frame queue (reader -> work) ----
    std::mutex d_queue_mutex;
    std::condition_variable d_queue_cv;
    std::deque<std::unique_ptr<frame_t>> d_queue;

    // ---- frame freelist (recycled buffers, no steady-state allocation) ----
    std::mutex d_freelist_mutex;
    std::vector<std::unique_ptr<frame_t>> d_freelist;

    // ---- work() streaming state (work thread only) ----
    std::unique_ptr<frame_t> d_cur_frame;
    size_t d_cur_pos = 0;

    // ---- tag state (work thread only) ----
    bool d_have_tagged = false;
    double d_tag_freq = 0.0;
    uint32_t d_tag_phase = 0;
    bool d_tag_noise = false;
    pmt::pmt_t d_key_rx_freq;
    pmt::pmt_t d_key_phase_state;
    pmt::pmt_t d_key_noise_source;
    pmt::pmt_t d_srcid;

    // ---- control command queue + current settings ----
    std::mutex d_cmd_mutex;
    std::deque<std::string> d_cmd_queue;
    double d_freq_mhz;
    double d_gain_db;

    // ---- status atomics (reader writes, getters read) ----
    std::atomic<uint32_t> d_phase_state{ 0 };
    std::atomic<bool> d_noise{ false };
    std::atomic<double> d_center_freq{ 0.0 };

    // ---- counters + log throttles (5 s) ----
    uint64_t d_frames_rx = 0;      // reader thread only
    uint64_t d_frames_gated = 0;   // dropped: noise/retune (coherent_only)
    uint64_t d_frames_chdrop = 0;  // dropped: packet channels < block channels
    uint64_t d_queue_drops = 0;    // oldest-frame drops on full queue
    std::chrono::steady_clock::time_point d_t_chdrop{};
    std::chrono::steady_clock::time_point d_t_qdrop{};
    std::chrono::steady_clock::time_point d_t_debug{};
    std::chrono::steady_clock::time_point d_t_bcast{};
    std::chrono::steady_clock::time_point d_t_ctrl_err{};

    // ---- threads ----
    void reader_loop();
    void control_loop();
    void process_packet(const uint8_t* pkt,
                        uint32_t nch,
                        uint32_t nsamp,
                        size_t header_size);

    // ---- helpers ----
    int try_connect(int port, int timeout_ms); // -1 on failure; interruptible
    bool send_all(int fd, const std::string& s);
    void sleep_while_running(int ms);
    void wake_control();
    void handle_ctrl_line(const std::string& line);
    bool throttle_ok(std::chrono::steady_clock::time_point& last);

    std::unique_ptr<frame_t> acquire_frame();
    void release_frame(std::unique_ptr<frame_t> f);
    void push_frame(std::unique_ptr<frame_t> f);

    static std::string format_freq_cmd(double freq_mhz);
    static std::string format_gain_cmd(double gain_db);

public:
    krakensdr_source_impl(const std::string& ip_addr,
                          int data_port,
                          int ctrl_port,
                          int num_channels,
                          double freq_mhz,
                          double gain_db,
                          bool coherent_only,
                          bool dc_correction,
                          bool debug);
    ~krakensdr_source_impl() override;

    bool start() override;
    bool stop() override;

    void set_freq(double freq_mhz) override;
    void set_gain(double gain_db) override;

    uint32_t phase_state() const override;
    bool noise_source_active() const override;
    double center_freq_hz() const override;

    int work(int noutput_items,
             gr_vector_const_void_star& input_items,
             gr_vector_void_star& output_items) override;
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_IMPL_H */
