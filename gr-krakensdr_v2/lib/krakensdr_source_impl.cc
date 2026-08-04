/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "krakensdr_source_impl.h"
#include <gnuradio/io_signature.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gr {
namespace krakensdr_v2 {

// ---------------------------------------------------------------------------
// local helpers
// ---------------------------------------------------------------------------

static inline uint32_t read_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static inline float read_le_float(const uint8_t* p)
{
    float f;
    memcpy(&f, p, sizeof(float)); // wire is LE float32; host (aarch64/x86) is LE
    return f;
}

static int clamp_channels(int n) { return n < 1 ? 1 : (n > 16 ? 16 : n); }

//! uint8 interleaved I,Q -> gr_complex, optional per-block DC mean removal.
static void convert_channel(const uint8_t* src, gr_complex* dst, size_t n, bool dc)
{
    const float scl = 1.0f / 127.5f;
    if (dc) {
        // pass 1: per-channel per-frame mean of the raw uint8 values
        // (max 65536 * 255 = 16711680, fits uint32_t)
        uint32_t si = 0, sq = 0;
        for (size_t i = 0; i < n; i++) {
            si += src[2 * i];
            sq += src[2 * i + 1];
        }
        const float mi = (float)si / (float)n;
        const float mq = (float)sq / (float)n;
        for (size_t i = 0; i < n; i++) {
            dst[i] = gr_complex(((float)src[2 * i] - mi) * scl,
                                ((float)src[2 * i + 1] - mq) * scl);
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            dst[i] = gr_complex(((float)src[2 * i] - 127.5f) * scl,
                                ((float)src[2 * i + 1] - 127.5f) * scl);
        }
    }
}

// ---------------------------------------------------------------------------
// make / ctor / dtor
// ---------------------------------------------------------------------------

krakensdr_source::sptr krakensdr_source::make(const std::string& ip_addr,
                                              int data_port,
                                              int ctrl_port,
                                              int num_channels,
                                              double freq_mhz,
                                              double gain_db,
                                              bool coherent_only,
                                              bool dc_correction,
                                              bool debug)
{
    return gnuradio::make_block_sptr<krakensdr_source_impl>(ip_addr,
                                                            data_port,
                                                            ctrl_port,
                                                            num_channels,
                                                            freq_mhz,
                                                            gain_db,
                                                            coherent_only,
                                                            dc_correction,
                                                            debug);
}

krakensdr_source_impl::krakensdr_source_impl(const std::string& ip_addr,
                                             int data_port,
                                             int ctrl_port,
                                             int num_channels,
                                             double freq_mhz,
                                             double gain_db,
                                             bool coherent_only,
                                             bool dc_correction,
                                             bool debug)
    : gr::sync_block("krakensdr_source",
                     gr::io_signature::make(0, 0, 0),
                     gr::io_signature::make(clamp_channels(num_channels),
                                            clamp_channels(num_channels),
                                            sizeof(gr_complex))),
      d_ip(ip_addr),
      d_data_port(data_port),
      d_ctrl_port(ctrl_port),
      d_num_channels(clamp_channels(num_channels)),
      d_coherent_only(coherent_only),
      d_dc_correction(dc_correction),
      d_debug(debug),
      d_freq_mhz(freq_mhz),
      d_gain_db(gain_db)
{
    if (num_channels != d_num_channels) {
        d_logger->warn("num_channels {} out of range, clamped to {}",
                       num_channels,
                       d_num_channels);
    }

    d_key_rx_freq = pmt::string_to_symbol("rx_freq");
    d_key_phase_state = pmt::string_to_symbol("phase_state");
    d_key_noise_source = pmt::string_to_symbol("noise_source");
    d_srcid = pmt::string_to_symbol(name());
}

krakensdr_source_impl::~krakensdr_source_impl()
{
    // Safe if start() never ran and idempotent if stop() already did.
    krakensdr_source_impl::stop();
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool krakensdr_source_impl::start()
{
    if (d_reader_thread.joinable() || d_ctrl_thread.joinable())
        return true; // already started

    if (::pipe2(d_wake_pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        d_wake_pipe[0] = -1;
        d_wake_pipe[1] = -1;
        d_logger->warn("pipe2 failed ({}); control wakeups fall back to poll timeout",
                       strerror(errno));
    }

    {
        std::lock_guard<std::mutex> lk(d_queue_mutex);
        d_queue.clear();
    }
    d_cur_frame.reset();
    d_cur_pos = 0;
    d_have_tagged = false;

    d_running.store(true);
    d_reader_thread = std::thread(&krakensdr_source_impl::reader_loop, this);
    d_ctrl_thread = std::thread(&krakensdr_source_impl::control_loop, this);
    return true;
}

bool krakensdr_source_impl::stop()
{
    d_running.store(false);

    // Wake anything that might be blocked.
    d_queue_cv.notify_all();
    wake_control();
    {
        std::lock_guard<std::mutex> lk(d_fd_mutex);
        if (d_data_fd >= 0)
            ::shutdown(d_data_fd, SHUT_RDWR);
        if (d_ctrl_fd >= 0)
            ::shutdown(d_ctrl_fd, SHUT_RDWR);
    }

    if (d_reader_thread.joinable())
        d_reader_thread.join();
    if (d_ctrl_thread.joinable())
        d_ctrl_thread.join();

    for (int i = 0; i < 2; i++) {
        if (d_wake_pipe[i] >= 0) {
            ::close(d_wake_pipe[i]);
            d_wake_pipe[i] = -1;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

void krakensdr_source_impl::set_freq(double freq_mhz)
{
    {
        std::lock_guard<std::mutex> lk(d_cmd_mutex);
        d_freq_mhz = freq_mhz;
        d_cmd_queue.push_back(format_freq_cmd(freq_mhz));
    }
    wake_control(); // never blocks the caller
}

void krakensdr_source_impl::set_gain(double gain_db)
{
    {
        std::lock_guard<std::mutex> lk(d_cmd_mutex);
        d_gain_db = gain_db;
        d_cmd_queue.push_back(format_gain_cmd(gain_db));
    }
    wake_control();
}

uint32_t krakensdr_source_impl::phase_state() const { return d_phase_state.load(); }

bool krakensdr_source_impl::noise_source_active() const { return d_noise.load(); }

double krakensdr_source_impl::center_freq_hz() const { return d_center_freq.load(); }

// ---------------------------------------------------------------------------
// work
// ---------------------------------------------------------------------------

int krakensdr_source_impl::work(int noutput_items,
                                gr_vector_const_void_star& input_items,
                                gr_vector_void_star& output_items)
{
    (void)input_items;

    if (!d_cur_frame) {
        std::unique_lock<std::mutex> lk(d_queue_mutex);
        d_queue_cv.wait_for(lk, std::chrono::milliseconds(250), [this] {
            return !d_queue.empty() || !d_running.load(std::memory_order_relaxed);
        });
        if (d_queue.empty())
            return 0; // scheduler will call again
        d_cur_frame = std::move(d_queue.front());
        d_queue.pop_front();
        lk.unlock();
        d_cur_pos = 0;

        // Tag the first output item of a frame whose metadata changed.
        const frame_t& f = *d_cur_frame;
        if (!d_have_tagged || f.freq_hz != d_tag_freq || f.phase_state != d_tag_phase ||
            f.noise_source != d_tag_noise) {
            const pmt::pmt_t v_freq = pmt::from_double(f.freq_hz);
            const pmt::pmt_t v_ps = pmt::from_long((long)f.phase_state);
            const pmt::pmt_t v_ns = pmt::from_bool(f.noise_source);
            for (size_t o = 0; o < output_items.size(); o++) {
                const uint64_t abs = nitems_written((unsigned)o);
                add_item_tag((unsigned)o, abs, d_key_rx_freq, v_freq, d_srcid);
                add_item_tag((unsigned)o, abs, d_key_phase_state, v_ps, d_srcid);
                add_item_tag((unsigned)o, abs, d_key_noise_source, v_ns, d_srcid);
            }
            d_have_tagged = true;
            d_tag_freq = f.freq_hz;
            d_tag_phase = f.phase_state;
            d_tag_noise = f.noise_source;
        }
    }

    const size_t remaining = d_cur_frame->nsamples - d_cur_pos;
    const size_t n = std::min((size_t)noutput_items, remaining);
    const size_t nports = output_items.size();
    for (size_t o = 0; o < nports; o++) {
        gr_complex* out = (gr_complex*)output_items[o];
        memcpy(out, d_cur_frame->ch[o].data() + d_cur_pos, n * sizeof(gr_complex));
    }
    d_cur_pos += n;
    if (d_cur_pos >= d_cur_frame->nsamples) {
        release_frame(std::move(d_cur_frame));
        d_cur_pos = 0;
    }
    return (int)n;
}

// ---------------------------------------------------------------------------
// frame pool / queue
// ---------------------------------------------------------------------------

std::unique_ptr<krakensdr_source_impl::frame_t> krakensdr_source_impl::acquire_frame()
{
    {
        std::lock_guard<std::mutex> lk(d_freelist_mutex);
        if (!d_freelist.empty()) {
            auto f = std::move(d_freelist.back());
            d_freelist.pop_back();
            return f;
        }
    }
    return std::make_unique<frame_t>();
}

void krakensdr_source_impl::release_frame(std::unique_ptr<frame_t> f)
{
    if (!f)
        return;
    std::lock_guard<std::mutex> lk(d_freelist_mutex);
    if (d_freelist.size() < FREELIST_CAP)
        d_freelist.push_back(std::move(f)); // keeps vector capacity for reuse
    // else: let it free — bounds worst-case memory
}

void krakensdr_source_impl::push_frame(std::unique_ptr<frame_t> f)
{
    std::unique_ptr<frame_t> dropped;
    {
        std::lock_guard<std::mutex> lk(d_queue_mutex);
        if (d_queue.size() >= QUEUE_CAP) {
            dropped = std::move(d_queue.front()); // drop the OLDEST: freshness wins
            d_queue.pop_front();
        }
        d_queue.push_back(std::move(f));
    }
    d_queue_cv.notify_one();
    if (dropped) {
        d_queue_drops++;
        release_frame(std::move(dropped));
        if (throttle_ok(d_t_qdrop)) {
            d_logger->warn("frame queue full, dropping oldest (downstream too slow; "
                           "{} drops total)",
                           d_queue_drops);
        }
    }
}

// ---------------------------------------------------------------------------
// reader thread
// ---------------------------------------------------------------------------

void krakensdr_source_impl::reader_loop()
{
    std::vector<uint8_t> buf(RX_BUF_SIZE);
    bool was_connected = false;

    while (d_running.load()) {
        int fd = try_connect(d_data_port, 2000);
        if (fd < 0) {
            sleep_while_running(1000); // 1 s retry backoff
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(d_fd_mutex);
            d_data_fd = fd;
        }
        d_logger->info("data: connected to {}:{}", d_ip, d_data_port);
        was_connected = true;
        size_t bytes = 0; // reset parse buffer on each new connection

        while (d_running.load()) {
            ssize_t r = ::recv(fd, buf.data() + bytes, buf.size() - bytes, 0);
            if (r < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    continue; // SO_RCVTIMEO tick: no data yet, poll d_running
                break;        // real error -> reconnect
            }
            if (r == 0)
                break; // EOF -> reconnect
            bytes += (size_t)r;

            // Parse every complete packet in the buffer; resync on magic.
            size_t off = 0;
            while (off + HEADER_FIXED <= bytes) {
                if (read_be32(&buf[off]) != TCP_MAGIC) {
                    off++; // byte-wise resync
                    continue;
                }
                const uint32_t nch = read_be32(&buf[off + 4]);
                const uint32_t nsamp = read_be32(&buf[off + 8]);
                if (nch < 1 || nch > MAX_WIRE_CHANNELS || nsamp < 1 ||
                    nsamp > MAX_WIRE_SAMPLES) {
                    off++; // false magic match
                    continue;
                }
                const size_t header_size = HEADER_FIXED + (size_t)nch * 8;
                const size_t packet_size = header_size + (size_t)nch * nsamp * 2;
                if (off + packet_size > bytes)
                    break; // wait for the rest of the packet
                process_packet(&buf[off], nch, nsamp, header_size);
                off += packet_size;
            }
            if (off > 0) {
                memmove(buf.data(), buf.data() + off, bytes - off);
                bytes -= off;
            }

            if (d_debug && throttle_ok(d_t_debug)) {
                d_logger->info("data: {} frames rx, {} gated (noise/retune), "
                               "{} ch-mismatch drops, {} queue drops",
                               d_frames_rx,
                               d_frames_gated,
                               d_frames_chdrop,
                               d_queue_drops);
            }
        }

        {
            std::lock_guard<std::mutex> lk(d_fd_mutex);
            d_data_fd = -1;
        }
        ::close(fd);
        if (d_running.load() && was_connected)
            d_logger->warn("data: connection lost, reconnecting");
    }
}

void krakensdr_source_impl::process_packet(const uint8_t* pkt,
                                           uint32_t nch,
                                           uint32_t nsamp,
                                           size_t header_size)
{
    // Low byte is the phase-state enum; high bits carry KerberosSDR support
    // flags (bit 8 = kerberos mode, bit 9 = calibration stale) - mask them so
    // phase_state comparisons (e.g. == 4 CONVERGED) keep working.
    const uint32_t phase_state = read_be32(pkt + 12) & 0xFFu;
    const uint32_t noise_source = read_be32(pkt + 16);
    const uint32_t retuning = read_be32(pkt + 28);
    const float freq_hz = read_le_float(pkt + 32);     // channel 0 metadata
    const float gain_db = read_le_float(pkt + 36);

    // "Last seen header value" getters — updated for every valid packet.
    d_phase_state.store(phase_state);
    d_noise.store(noise_source != 0);
    d_center_freq.store((double)freq_hz);

    d_frames_rx++;

    // coherent_only gating: drop calibration-noise and mid-retune frames.
    // State-5 cooldown frames are KEPT (continuous, uncalibrated antenna data).
    if (d_coherent_only && (noise_source != 0 || retuning != 0)) {
        d_frames_gated++;
        return;
    }

    if ((int)nch < d_num_channels) {
        d_frames_chdrop++;
        if (throttle_ok(d_t_chdrop)) {
            d_logger->error("packet has {} channels but block needs {} — dropping "
                            "({} drops total)",
                            nch,
                            d_num_channels,
                            d_frames_chdrop);
        }
        return;
    }
    // nch > d_num_channels: use the first d_num_channels.

    std::unique_ptr<frame_t> f = acquire_frame();
    f->nsamples = nsamp;
    f->freq_hz = (double)freq_hz;
    f->gain_db = gain_db;
    f->phase_state = phase_state;
    f->noise_source = (noise_source != 0);
    f->retuning = (retuning != 0);
    if ((int)f->ch.size() != d_num_channels)
        f->ch.resize(d_num_channels);

    const uint8_t* payload = pkt + header_size; // channel-major, interleaved I,Q
    for (int c = 0; c < d_num_channels; c++) {
        f->ch[c].resize(nsamp); // no-op after first reuse (capacity kept)
        convert_channel(
            payload + (size_t)c * nsamp * 2, f->ch[c].data(), nsamp, d_dc_correction);
    }

    push_frame(std::move(f));
}

// ---------------------------------------------------------------------------
// control thread
// ---------------------------------------------------------------------------

std::string krakensdr_source_impl::format_freq_cmd(double freq_mhz)
{
    char buf[96];
    snprintf(buf,
             sizeof(buf),
             "{\"command\":\"set_frequency\",\"frequency\":%lld}\n",
             (long long)llround(freq_mhz * 1e6));
    return std::string(buf);
}

std::string krakensdr_source_impl::format_gain_cmd(double gain_db)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"command\":\"set_gain\",\"gain\":%.2f}\n", gain_db);
    return std::string(buf);
}

void krakensdr_source_impl::wake_control()
{
    if (d_wake_pipe[1] >= 0) {
        const char b = 1;
        ssize_t r = ::write(d_wake_pipe[1], &b, 1); // non-blocking; full pipe is fine
        (void)r;
    }
}

void krakensdr_source_impl::handle_ctrl_line(const std::string& line)
{
    if (line.empty())
        return;
    if (line.find("\"status\":\"error\"") != std::string::npos) {
        d_logger->warn("control: server error: {}",
                       line.size() > 300 ? line.substr(0, 300) + "..." : line);
        return;
    }
    // Unsolicited 2 Hz status broadcast: top-level "settings", no "status".
    if (d_debug && line.find("\"settings\"") != std::string::npos &&
        throttle_ok(d_t_bcast)) {
        double cf = 0.0, gn = 0.0;
        size_t p = line.find("\"center_freq\":");
        if (p != std::string::npos)
            cf = strtod(line.c_str() + p + 14, nullptr);
        p = line.find("\"gain\":");
        if (p != std::string::npos)
            gn = strtod(line.c_str() + p + 7, nullptr);
        d_logger->info("control: heimdall status center_freq={:.0f} Hz gain={:.1f} dB",
                       cf,
                       gn);
    }
}

void krakensdr_source_impl::control_loop()
{
    std::string linebuf;
    int fd = -1;

    auto disconnect = [&]() {
        if (fd >= 0) {
            {
                std::lock_guard<std::mutex> lk(d_fd_mutex);
                d_ctrl_fd = -1;
            }
            ::close(fd);
            fd = -1;
        }
    };

    while (d_running.load()) {
        if (fd < 0) {
            fd = try_connect(d_ctrl_port, 2000);
            if (fd < 0) {
                sleep_while_running(5000); // 5 s retry
                continue;
            }
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            {
                std::lock_guard<std::mutex> lk(d_fd_mutex);
                d_ctrl_fd = fd;
            }
            linebuf.clear();

            // Discard stale queued commands (superseded by the stored values)
            // and push the CURRENT settings to the server.
            std::string cmd_freq, cmd_gain;
            {
                std::lock_guard<std::mutex> lk(d_cmd_mutex);
                d_cmd_queue.clear();
                cmd_freq = format_freq_cmd(d_freq_mhz);
                cmd_gain = format_gain_cmd(d_gain_db);
            }
            if (!send_all(fd, cmd_freq) || !send_all(fd, cmd_gain)) {
                disconnect();
                sleep_while_running(5000);
                continue;
            }
            d_logger->info("control: connected to {}:{}, sent set_frequency + set_gain",
                           d_ip,
                           d_ctrl_port);
        }

        struct pollfd pfds[2];
        pfds[0].fd = fd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd = d_wake_pipe[0];
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;
        const nfds_t nfds = (d_wake_pipe[0] >= 0) ? 2 : 1;
        ::poll(pfds, nfds, 200);
        if (!d_running.load())
            break;

        if (nfds == 2 && (pfds[1].revents & POLLIN)) {
            char tmp[64];
            while (::read(d_wake_pipe[0], tmp, sizeof(tmp)) > 0) {
            } // drain wakeups
        }

        if (pfds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            // Drain the socket — the server may drop clients that don't read.
            bool dead = false;
            char rbuf[4096];
            for (;;) {
                ssize_t r = ::recv(fd, rbuf, sizeof(rbuf), MSG_DONTWAIT);
                if (r > 0) {
                    linebuf.append(rbuf, (size_t)r);
                    if (r < (ssize_t)sizeof(rbuf))
                        break;
                    continue;
                }
                if (r == 0) {
                    dead = true;
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                    break;
                dead = true;
                break;
            }

            // Line-split ('\n'-terminated JSON) and handle.
            size_t pos;
            while ((pos = linebuf.find('\n')) != std::string::npos) {
                handle_ctrl_line(linebuf.substr(0, pos));
                linebuf.erase(0, pos + 1);
            }
            if (linebuf.size() > 65536)
                linebuf.clear(); // runaway line without newline

            if (dead) {
                disconnect();
                if (d_running.load() && throttle_ok(d_t_ctrl_err))
                    d_logger->warn("control: connection lost, reconnecting");
                continue;
            }
        }

        // Send queued commands (from set_freq/set_gain callbacks).
        std::deque<std::string> cmds;
        {
            std::lock_guard<std::mutex> lk(d_cmd_mutex);
            cmds.swap(d_cmd_queue);
        }
        for (const std::string& c : cmds) {
            if (!send_all(fd, c)) {
                // Treat send failure as disconnect; the reconnect path re-sends
                // the current stored freq/gain, so lost commands are recovered.
                disconnect();
                if (d_running.load() && throttle_ok(d_t_ctrl_err))
                    d_logger->warn("control: send failed, reconnecting");
                break;
            }
            if (d_debug)
                d_logger->info("control: sent {}",
                               c.substr(0, c.size() - 1)); // strip '\n'
        }
    }

    disconnect();
}

// ---------------------------------------------------------------------------
// socket helpers
// ---------------------------------------------------------------------------

int krakensdr_source_impl::try_connect(int port, int timeout_ms)
{
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, d_ip.c_str(), &sa.sin_addr) != 1) {
        // Hostname. getaddrinfo blocks with no way for stop() to cancel it,
        // so resolve at most ONCE per block lifetime; the reconnect loops
        // reuse the cached address. (A stop() during this first resolution
        // can still wait out the resolver timeout — bounded and rare.)
        if (!d_addr_cached.load()) {
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* res = nullptr;
            if (getaddrinfo(d_ip.c_str(), nullptr, &hints, &res) != 0 || !res)
                return -1;
            d_cached_addr = ((struct sockaddr_in*)res->ai_addr)->sin_addr;
            freeaddrinfo(res);
            d_addr_cached.store(true);
        }
        sa.sin_addr = d_cached_addr;
    }

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;

    // Non-blocking connect so stop() stays responsive (polled in 100 ms slices).
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = ::connect(fd, (struct sockaddr*)&sa, sizeof(sa));
    if (rc < 0) {
        if (errno != EINPROGRESS) {
            ::close(fd);
            return -1;
        }
        bool ok = false;
        int waited = 0;
        while (d_running.load() && waited < timeout_ms) {
            struct pollfd p;
            p.fd = fd;
            p.events = POLLOUT;
            p.revents = 0;
            int pr = ::poll(&p, 1, 100);
            waited += 100;
            if (pr > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                ok = (err == 0);
                break;
            }
            if (pr < 0 && errno != EINTR)
                break;
        }
        if (!ok) {
            ::close(fd);
            return -1;
        }
    }

    // Back to blocking + 500 ms receive timeout (bounded recv, d_running polled).
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

bool krakensdr_source_impl::send_all(int fd, const std::string& s)
{
    size_t sent = 0;
    while (sent < s.size()) {
        ssize_t r = ::send(fd, s.data() + sent, s.size() - sent, MSG_NOSIGNAL);
        if (r > 0) {
            sent += (size_t)r; // handle partial sends
            continue;
        }
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!d_running.load())
                return false;
            struct pollfd p;
            p.fd = fd;
            p.events = POLLOUT;
            p.revents = 0;
            ::poll(&p, 1, 200);
            continue;
        }
        return false; // treat as disconnect
    }
    return true;
}

void krakensdr_source_impl::sleep_while_running(int ms)
{
    int waited = 0;
    while (d_running.load() && waited < ms) {
        const int slice = std::min(50, ms - waited);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
        waited += slice;
    }
}

bool krakensdr_source_impl::throttle_ok(std::chrono::steady_clock::time_point& last)
{
    const auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(5)) {
        last = now;
        return true;
    }
    return false;
}

} // namespace krakensdr_v2
} // namespace gr
