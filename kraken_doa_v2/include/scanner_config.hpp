#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>

// Scanner state machine states
enum class ScannerState {
    IDLE,           // Scanner not running
    SCANNING,       // Actively scanning through frequency groups
    LOCKED,         // Signal detected, locked on frequency with DoA active
    PAUSED,         // Scanner paused by user
    SIGNAL_LOST_WAIT // Signal lost, waiting before resuming scan
};

// Individual frequency entry
struct ScannerFrequency {
    float freq_mhz;                 // Frequency in MHz
    float bandwidth_khz;            // Bandwidth in kHz
    std::string label;              // User label (e.g., "PMR Ch1")
    bool enabled;                   // Whether to scan this frequency

    ScannerFrequency()
        : freq_mhz(100.0f), bandwidth_khz(12.5f), label(""), enabled(true) {}

    ScannerFrequency(float freq, float bw, const std::string& lbl, bool en = true)
        : freq_mhz(freq), bandwidth_khz(bw), label(lbl), enabled(en) {}
};

// Group of frequencies that can be monitored with a single tune
struct FrequencyGroup {
    float center_freq_mhz;          // Center frequency for this group
    std::vector<size_t> freq_indices; // Indices into main frequency list
    float span_mhz;                 // Total span covered by this group

    FrequencyGroup() : center_freq_mhz(100.0f), span_mhz(0.0f) {}
};

// Scanner configuration
struct ScannerConfig {
    std::string name;               // Scanner profile name
    int version;                    // Config version for future compatibility

    // Settings
    float squelch_db;               // Squelch threshold in dB
    int dwell_time_ms;              // Time to monitor each group (ms)
    float hysteresis_db;            // Squelch hysteresis to prevent oscillation
    int lock_timeout_ms;            // Time to wait after signal lost before resuming (ms)

    // Frequencies
    std::vector<ScannerFrequency> frequencies;

    ScannerConfig()
        : name("Default Scanner"),
          version(1),
          squelch_db(-40.0f),
          dwell_time_ms(200),
          hysteresis_db(5.0f),
          lock_timeout_ms(200) {}
};

// Scanner runtime state
struct ScannerStatus {
    std::atomic<ScannerState> state;
    std::atomic<size_t> current_group;
    std::atomic<size_t> current_freq_index;  // Index of current frequency being checked/monitored
    std::atomic<size_t> locked_freq_index;   // Index of locked frequency
    std::atomic<float> locked_signal_db;     // Signal level of locked frequency
    std::atomic<bool> config_loaded;

    ScannerStatus()
        : state(ScannerState::IDLE),
          current_group(0),
          current_freq_index(0),
          locked_freq_index(0),
          locked_signal_db(-120.0f),
          config_loaded(false) {}
};
