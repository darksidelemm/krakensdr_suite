#pragma once

// Built-in KrakenSDR web mapper output (replaces the external Node.js
// web_mapper_middleware).
//
// Publishes one legacy "doapost" record per decimator (a legacy "VFO") built
// from the same capture helpers as DOA_value.html / the DoA logger, so the
// values stay byte-compatible with the canonical KrakenSDR output - the
// external contract with map.krakenrf.com and the Android app.
//
// Three output modes, selected from the web UI sidebar (persisted):
//   remote - WSS client to the KrakenPro cloud map (map.krakenrf.com:2096):
//            {"apikey","data"} per record, settings registration on connect +
//            on change, keep-alive ping every 10 s, and settings pushed back
//            by the cloud are applied to the receiver (remote retune).
//   local  - plain WebSocket server broadcasting each record as JSON to
//            connected LAN clients (the on-network web map / app).
//   chasemapper - UDP broadcast of Horus/Chasemapper BEARING JSON on port
//            55672, with optional output decimation.
//
// Everything runs on one background thread; records are captured on a 200 ms
// tick (the same cadence the browser DoA stream uses). Each VFO's own squelch
// setting is respected: squelch off = its bearings always transmit, squelch
// on = only while that squelch is open. Station identity and location come
// from StationInfo (the "Station Information" sidebar panel) - they are NOT
// separate web-mapper settings.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

class WebMapper {
public:
    // --- Configuration (thread-safe; applies live - the worker rebuilds its
    // connection/server when a relevant value changes) ---
    void setEnabled(bool en);
    void setMode(const std::string& mode);       // "remote" | "local" | "chasemapper"
    void setKey(const std::string& key);         // KrakenPro API key
    void setServerUrl(const std::string& url);   // cloud map URL (wss://...)
    void setLocalWsPort(int port);               // local-mode broadcast port
    void setChasemapperDecimation(int decimation); // send one of every N frames

    bool        isEnabled() const { return enabled_.load(std::memory_order_relaxed); }
    std::string getMode() const;
    std::string getKey() const;
    std::string getServerUrl() const;
    int         getLocalWsPort() const { return local_ws_port_.load(std::memory_order_relaxed); }
    int         getChasemapperDecimation() const {
        return chasemapper_decimation_.load(std::memory_order_relaxed);
    }

    // Append the "web_mapper":{...} object (no leading comma) to a JSON
    // stream, for the periodic system_status broadcast.
    void appendStatusJson(std::string& out) const;

    void start();   // spawn the worker thread (idempotent)
    void stop();    // join it

private:
    void workerThread();

    std::atomic<bool> running_{false};
    std::thread worker_;

    // Config. String values are mutex-guarded; every setter bumps config_gen_
    // so the worker knows to rebuild its sink.
    std::atomic<bool>     enabled_{false};
    std::atomic<int>      local_ws_port_{8021};
    std::atomic<int>      chasemapper_decimation_{1};
    std::atomic<uint32_t> config_gen_{0};
    mutable std::mutex    cfg_mtx_;
    std::string mode_ = "remote";
    std::string key_;
    std::string server_url_ = "wss://map.krakenrf.com:2096";

    // Live status (worker -> status JSON).
    mutable std::mutex    status_mtx_;
    std::string           status_state_ = "disabled";  // disabled/connecting/connected/listening/error
    std::string           status_error_;
    std::atomic<int>      local_clients_{0};
    std::atomic<uint64_t> records_sent_{0};
};

extern WebMapper web_mapper;
