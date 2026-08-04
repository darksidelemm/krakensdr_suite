#pragma once

#include <string>
#include "utils/raw_data_buffer.hpp"

class DataReceiver {
public:
    static void data_receiver_thread();
    static void decimation_processor_thread();  // NEW: Separate thread for decimation processing
    static void fft_processor_thread();
    static void fm_processor_thread();
    static void send_control_command(const std::string& cmd);

    // Discard heimdall's control-port responses/status broadcasts. Must be
    // called periodically: an unread control socket back-pressures heimdall's
    // control server until it stalls (see the implementation comment).
    static void drain_control_socket();
    
    // NEW: Raw data buffer management
    static RawDataBuffer::Stats get_raw_buffer_stats();
    static void print_raw_buffer_status();
    static void flush_data_pipeline();  // Clear all buffered IQ data (for frequency changes)
};