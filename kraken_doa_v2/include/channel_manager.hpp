#pragma once

#include <vector>
#include <mutex>
#include "types.hpp"

class ChannelManager {
private:
    static std::vector<ChannelInfo> channel_info;
    static std::mutex channel_info_mutex;

public:
    static void initialize();
    static void set_frequency(float freq_hz, int channel = -1); // -1 means all channels
    static void set_gain(float gain_db, int channel = -1);
    static float get_frequency(int channel);
    static float get_gain(int channel);
    static const ChannelInfo& get_channel_info(int channel);
    static void update_channel_info(int ch, float frequency_hz, float gain_db);
};
