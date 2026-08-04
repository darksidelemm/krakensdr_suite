/* -*- c++ -*- */
/*
 * Copyright 2026 KrakenRF Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_H
#define INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_H

#include <gnuradio/krakensdr_v2/api.h>
#include <gnuradio/sync_block.h>
#include <cstdint>
#include <string>

namespace gr {
namespace krakensdr_v2 {

/*!
 * \brief Coherent multi-channel source for the Heimdall v2 server.
 * \ingroup krakensdr_v2
 *
 * Connects to heimdall_v2's TCP data server (default port 8091) and streams
 * the phase-coherent, calibration-compensated channels as complex float
 * streams. Frequency and gain are controlled through the JSON control port
 * (default 8092); both are settable at runtime (GRC callbacks).
 *
 * With coherent_only=true (default), packets carrying calibration noise
 * (noise_source=1) or captured mid-retune are dropped, so the outputs only
 * ever carry antenna data. Set coherent_only=false to also pass calibration
 * noise frames (useful for the correlation check example).
 *
 * Stream tags (on every output, at the first item of a frame whose metadata
 * changed): rx_freq (double, Hz), phase_state (long, 4 = converged),
 * noise_source (bool).
 */
class KRAKENSDR_V2_API krakensdr_source : virtual public gr::sync_block
{
public:
    typedef std::shared_ptr<krakensdr_source> sptr;

    /*!
     * \param ip_addr Heimdall v2 server address
     * \param data_port TCP data server port (heimdall TCP_DATA_PORT, 8091)
     * \param ctrl_port TCP control server port (heimdall TCP_CONTROL_PORT, 8092)
     * \param num_channels Number of output channels (must be <= server channels)
     * \param freq_mhz Center frequency in MHz, sent to the server on start
     * \param gain_db Tuner gain in dB (0-50, applies to all tuners; negative = AGC)
     * \param coherent_only Drop calibration-noise / mid-retune frames
     * \param dc_correction Subtract the per-frame DC mean per channel
     * \param debug Verbose logging
     */
    static sptr make(const std::string& ip_addr = "127.0.0.1",
                     int data_port = 8091,
                     int ctrl_port = 8092,
                     int num_channels = 5,
                     double freq_mhz = 100.0,
                     double gain_db = 40.2,
                     bool coherent_only = true,
                     bool dc_correction = true,
                     bool debug = false);

    //! Retune the server (MHz). Triggers heimdall's cooldown + phase recal.
    virtual void set_freq(double freq_mhz) = 0;
    //! Set tuner gain in dB on all channels (negative = AGC). Triggers recal.
    virtual void set_gain(double gain_db) = 0;

    //! Phase compensation state from the last received packet (4 = CONVERGED).
    virtual uint32_t phase_state() const = 0;
    //! True if the last received packet carried calibration noise.
    virtual bool noise_source_active() const = 0;
    //! Center frequency in Hz as reported by the last packet's metadata.
    virtual double center_freq_hz() const = 0;
};

} // namespace krakensdr_v2
} // namespace gr

#endif /* INCLUDED_KRAKENSDR_V2_KRAKENSDR_SOURCE_H */
