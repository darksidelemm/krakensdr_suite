#include "bandwidth_manager.hpp"
#include <iostream>

// Static member definitions
std::atomic<int> BandwidthManager::bandwidth_index{DEFAULT_BANDWIDTH_INDEX};
std::mutex BandwidthManager::bandwidth_mutex;

void BandwidthManager::initialize() {
    std::lock_guard<std::mutex> lock(bandwidth_mutex);
    
    bandwidth_index = DEFAULT_BANDWIDTH_INDEX;
    
    std::cout << "Bandwidth Manager initialized:" << std::endl;
    std::cout << "  Shared bandwidth: " << get_bandwidth_name() << " (" << get_bandwidth_mhz() << " MHz)" << std::endl;
    std::cout << "  Used for both FM and MUSIC processing" << std::endl;
}

// Shared bandwidth control
void BandwidthManager::set_bandwidth_index(int index) {
    if (is_valid_bandwidth_index(index)) {
        bandwidth_index = index;
        std::cout << "Bandwidth set to: " << get_bandwidth_name() 
                  << " (" << get_bandwidth_mhz() << " MHz, decimation=" << get_decimation_factor() << ")" << std::endl;
        std::cout << "This affects both FM and MUSIC processing" << std::endl;
    }
}

int BandwidthManager::get_bandwidth_index() {
    return bandwidth_index.load();
}

int BandwidthManager::get_decimation_factor() {
    int index = bandwidth_index.load();
    return get_bandwidth_option_safe(index).decimation_factor;
}

float BandwidthManager::get_bandwidth_mhz() {
    int index = bandwidth_index.load();
    return get_bandwidth_option_safe(index).bandwidth_mhz;
}

const char* BandwidthManager::get_bandwidth_name() {
    int index = bandwidth_index.load();
    return get_bandwidth_option_safe(index).display_name;
}

// Utility functions
bool BandwidthManager::is_valid_bandwidth_index(int index) {
    return index >= 0 && index < NUM_BANDWIDTH_OPTIONS;
}

const BandwidthOption& BandwidthManager::get_bandwidth_option_safe(int index) {
    if (is_valid_bandwidth_index(index)) {
        return BANDWIDTH_OPTIONS[index];
    }
    return BANDWIDTH_OPTIONS[DEFAULT_BANDWIDTH_INDEX]; // fallback
}
