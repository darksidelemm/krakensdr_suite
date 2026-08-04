#include "channel_manager.hpp"
#include "config.hpp"
#include "globals.hpp"
#include "decimator_manager.hpp"
#include <iostream>

// Static member definitions
std::vector<ChannelInfo> ChannelManager::channel_info(MAX_CHANNELS);
std::mutex ChannelManager::channel_info_mutex;

void ChannelManager::initialize() {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    for (int i = 0; i < MAX_CHANNELS; i++) {
        channel_info[i].frequency_hz = 100000000.0f; // 100 MHz default
        channel_info[i].gain_db = 49.6f; // Default gain
    }
    std::cout << "Channel manager initialized with " << MAX_CHANNELS << " channels" << std::endl;
}

void ChannelManager::set_frequency(float freq_hz, int channel) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (channel == -1) {
        // Set all channels
        for (int i = 0; i < MAX_CHANNELS; i++) {
            channel_info[i].frequency_hz = freq_hz;
        }
    } else if (channel >= 0 && channel < MAX_CHANNELS) {
        channel_info[channel].frequency_hz = freq_hz;
    }
}

void ChannelManager::set_gain(float gain_db, int channel) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (channel == -1) {
        // Set all channels
        for (int i = 0; i < MAX_CHANNELS; i++) {
            channel_info[i].gain_db = gain_db;
        }
    } else if (channel >= 0 && channel < MAX_CHANNELS) {
        channel_info[channel].gain_db = gain_db;
    }
}

float ChannelManager::get_frequency(int channel) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (channel >= 0 && channel < MAX_CHANNELS) {
        return channel_info[channel].frequency_hz.load();
    }
    return 100000000.0f; // Default
}

float ChannelManager::get_gain(int channel) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (channel >= 0 && channel < MAX_CHANNELS) {
        return channel_info[channel].gain_db.load();
    }
    return 49.6f; // Default
}

const ChannelInfo& ChannelManager::get_channel_info(int channel) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (channel >= 0 && channel < MAX_CHANNELS) {
        return channel_info[channel];
    }
    return channel_info[0]; // Fallback to channel 0
}

void ChannelManager::update_channel_info(int ch, float frequency_hz, float gain_db) {
    std::lock_guard<std::mutex> lock(channel_info_mutex);
    if (ch >= 0 && ch < MAX_CHANNELS) {
        float old_freq = channel_info[ch].frequency_hz.load();
        channel_info[ch].frequency_hz = frequency_hz;
        channel_info[ch].gain_db = gain_db;

        // Update tuner_frequencies for wideband FFT stitching
        tuner_frequencies[ch].store(static_cast<uint64_t>(frequency_hz), std::memory_order_relaxed);

        // Update MUSIC processors when channel 0 (reference) frequency changes
        // This ensures MUSIC steering vectors are computed for the correct frequency
        // even on initial startup when frequency comes from server packets
        if (ch == 0 && std::abs(old_freq - frequency_hz) > 1000.0f) {
            auto all_decimators = decimator_manager.getAllDecimators();
            for (const auto& dec : all_decimators) {
                if (dec && dec->music_processor && !dec->being_deleted.load(std::memory_order_relaxed)) {
                    // Effective freq = base freq + decimator offset
                    float effective_freq = frequency_hz + dec->frequency_offset_hz;
                    dec->music_processor->setFrequency(effective_freq);
                }
            }
        }
    }
}
