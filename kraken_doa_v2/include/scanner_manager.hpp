#pragma once

#include "scanner_config.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <string>

class ScannerManager {
public:
    ScannerManager();
    ~ScannerManager();

    // Configuration management
    bool loadConfig(const std::string& json_str);
    std::string getConfigJson() const;
    bool setSquelch(float squelch_db);
    bool setDwellTime(int dwell_ms);

    // Scanner control
    bool start();
    bool stop();
    bool pause();
    bool resume();
    bool next();

    // Status queries
    ScannerState getState() const { return status_.state.load(); }
    size_t getCurrentGroup() const { return status_.current_group.load(); }
    size_t getLockedFreqIndex() const { return status_.locked_freq_index.load(); }
    float getLockedSignalDb() const { return status_.locked_signal_db.load(); }
    bool isConfigLoaded() const { return status_.config_loaded.load(); }
    ScannerConfig getConfig() const;
    bool isRunning() const;

    // FFT monitoring - called from FFT processor
    void updateFFTData(const std::vector<float>& magnitudes, const std::vector<float>& frequencies);

    // Phase calibration status - called from data receiver
    void updatePhaseCalibrationStatus(uint32_t phase_state, uint32_t noise_source_active);

    // Getters for system status
    uint32_t getPhaseState() const { return phase_compensation_state_.load(); }
    bool isNoiseSourceActive() const { return noise_source_active_.load() != 0; }

    // steady_clock nanoseconds of the last packet that reported the noise
    // source active (0 = never seen). Updated per packet, so short calibration
    // bursts are visible even to consumers that only poll occasionally.
    int64_t getLastNoiseActiveNs() const { return last_noise_active_ns_.load(); }

private:
    // Thread function
    void scannerThread();

    // Core algorithms
    void buildFrequencyGroups();
    bool checkSignalPresent(size_t freq_index, const std::vector<float>& magnitudes,
                           const std::vector<float>& frequencies, float& signal_db);
    void lockOnSignal(size_t freq_index, float signal_db);
    void resumeScanning();

    // Run lockOnSignal/resumeScanning on a short-lived worker thread. Their
    // command-pacing sleeps (up to ~450 ms) must not run on the uWS broadcast
    // thread, which calls updateFFTData while holding the global fft_mutex.
    void startLockTransition(size_t freq_index, float signal_db);
    void startResumeTransition();
    void tuneToGroup(size_t group_index);

    // State machine helpers
    void processScanning();
    void processLocked();
    void processSignalLostWait();

    // Data members
    ScannerConfig config_;
    ScannerStatus status_;
    std::vector<FrequencyGroup> groups_;

    // Threading
    std::thread scanner_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> transition_in_progress_{false};  // Lock/resume worker active
    mutable std::mutex config_mutex_;  // Mutable for const getConfig functions
    std::mutex fft_mutex_;
    std::condition_variable state_cv_;

    // FFT data cache
    std::vector<float> cached_magnitudes_;
    std::vector<float> cached_frequencies_;
    std::chrono::steady_clock::time_point last_fft_update_;

    // Phase calibration status from server
    std::atomic<uint32_t> phase_compensation_state_{0};
    std::atomic<uint32_t> noise_source_active_{0};
    std::atomic<int64_t>  last_noise_active_ns_{0};

    // Timing
    std::chrono::steady_clock::time_point last_dwell_start_;
    std::chrono::steady_clock::time_point signal_lost_time_;
    std::chrono::steady_clock::time_point signal_lock_time_;  // Track when we locked onto signal
    std::chrono::steady_clock::time_point scan_resume_time_;  // Track when scanning resumed (for settling period)

    // Constants
    // With 5 tuners at 2.4 MHz sample rate and 80% usable bandwidth:
    // Total coverage = 5 tuners × (2.4 MHz × 0.8) = 9.6 MHz
    static constexpr float MAX_TUNER_SPAN_MHZ = 9.0f;  // Wideband array coverage (conservative)
    static constexpr float GROUP_OVERLAP_MHZ = 0.3f;   // Safety margin for edge rolloff
};
