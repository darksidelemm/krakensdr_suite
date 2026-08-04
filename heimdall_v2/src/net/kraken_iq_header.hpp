#pragma once

#include "../core/types.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

// Forward declaration
struct TcpClient;

// Extended TcpClient for KrakenSDR protocol
struct KrakenTcpClient {
    int socket;
    bool active;
    bool streaming_mode = false;
    bool wants_iq_frame = false;
    
    KrakenTcpClient(int s) : socket(s), active(true) {}
    ~KrakenTcpClient() { if (socket >= 0) close(socket); }
};

// KrakenSDR IQ Header format - 1024 bytes total
struct KrakenIQHeader {
    static constexpr uint32_t SYNC_WORD = 0x2BF7B95A;
    static constexpr uint32_t HEADER_SIZE = 1024;
    static constexpr uint32_t RESERVED_BYTES = 192;
    
    // Frame types
    static constexpr uint32_t FRAME_TYPE_DATA = 0;
    static constexpr uint32_t FRAME_TYPE_DUMMY = 1;
    static constexpr uint32_t FRAME_TYPE_RAMP = 2;
    static constexpr uint32_t FRAME_TYPE_CAL = 3;
    static constexpr uint32_t FRAME_TYPE_TRIGW = 4;
    static constexpr uint32_t FRAME_TYPE_EMPTY = 5;
    
    uint32_t sync_word;
    uint32_t frame_type;
    char hardware_id[16];
    uint32_t unit_id;
    uint32_t active_ant_chs;
    uint32_t ioo_type;
    uint64_t rf_center_freq;
    uint64_t adc_sampling_freq;
    uint64_t sampling_freq;
    uint32_t cpi_length;
    uint64_t time_stamp;
    uint32_t daq_block_index;
    uint32_t cpi_index;
    uint64_t ext_integration_cntr;
    uint32_t data_type;
    uint32_t sample_bit_depth;
    uint32_t adc_overdrive_flags;
    uint32_t if_gains[32];
    uint32_t delay_sync_flag;
    uint32_t iq_sync_flag;
    uint32_t sync_state;
    uint32_t noise_source_state;
    uint32_t reserved[RESERVED_BYTES];
    uint32_t header_version;
    
    KrakenIQHeader();
    void initialize_for_heimdall();
    std::vector<uint8_t> encode() const;
    void update_frame_data(uint32_t frame_index, uint64_t timestamp);
    void set_gains_from_current_settings();
    
private:
    void clear_reserved();
};

// KrakenSDR TCP Data Server - sends data in KrakenSDR format
class KrakenTcpServer {
private:
    int server_socket;
    std::vector<std::unique_ptr<KrakenTcpClient>> clients;
    std::mutex clients_mutex;
    std::thread server_thread;
    std::atomic<bool> running{true};
    uint32_t frame_counter{0};
    
    void accept_loop();
    void handle_client_command(KrakenTcpClient* client);
    void send_initial_frame(KrakenTcpClient* client);
    void send_iq_frame(const std::vector<ComplexBuffer>& channel_data);
    
public:
    KrakenTcpServer();
    ~KrakenTcpServer();
    
    bool start(int port = 5000);
    void stop();
    void broadcast_data(const std::vector<ComplexBuffer>& channel_data);
};
