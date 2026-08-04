#pragma once

#include <atomic>
#include <mutex>
#include "config.hpp"

class BandwidthManager {
private:
    static std::atomic<int> bandwidth_index;
    static std::mutex bandwidth_mutex;

public:
    static void initialize();
    
    // Shared bandwidth control for both FM and MUSIC
    static void set_bandwidth_index(int index);
    static int get_bandwidth_index();
    static int get_decimation_factor();
    static float get_bandwidth_mhz();
    static const char* get_bandwidth_name();
    
    // Utility functions
    static bool is_valid_bandwidth_index(int index);
    static const BandwidthOption& get_bandwidth_option_safe(int index);
};
