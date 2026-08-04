#include "kraken_iq_header.hpp"
#include "tcp_common.hpp"
#include "../core/config.hpp"
#include "../core/utils.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
#include <cstring>

// External declarations from types.hpp
extern std::atomic<uint64_t> current_frequency;
extern std::atomic<int> current_gain;
extern std::atomic<bool> bias_tee_enabled;

KrakenIQHeader::KrakenIQHeader() {
    memset(this, 0, sizeof(KrakenIQHeader));
    initialize_for_heimdall();
}

void KrakenIQHeader::initialize_for_heimdall() {
    sync_word = SYNC_WORD;
    frame_type = FRAME_TYPE_DATA;
    
    // Set hardware ID
    strncpy(hardware_id, "HEIMDALL_V2", 15);
    hardware_id[15] = '\0';
    
    unit_id = 1;
    active_ant_chs = NUM_DEVICES;
    ioo_type = 0;
    rf_center_freq = CENTER_FREQ;
    adc_sampling_freq = SAMPLE_RATE;
    sampling_freq = SAMPLE_RATE;
    cpi_length = NUM_SAMPLES;
    
    // Initialize timestamp
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    time_stamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    daq_block_index = 0;
    cpi_index = 0;
    ext_integration_cntr = 0;
    data_type = 0; // Complex float32
    sample_bit_depth = 64; // KrakenSDR expects 64 bits for complex64 (32-bit real + 32-bit imag)
    adc_overdrive_flags = 0;
    
    // Initialize gains
    set_gains_from_current_settings();
    
    delay_sync_flag = 1; // Assume synchronized
    iq_sync_flag = 1;    // Assume synchronized
    sync_state = 1;      // Synchronized
    noise_source_state = bias_tee_enabled ? 1 : 0;
    
    clear_reserved();
    header_version = 7; // Current KrakenSDR header version
}

void KrakenIQHeader::clear_reserved() {
    memset(reserved, 0, sizeof(reserved));
}

void KrakenIQHeader::update_frame_data(uint32_t frame_index, uint64_t timestamp) {
    cpi_index = frame_index;
    time_stamp = timestamp;
    
    // Update frequency and sampling rate from current settings
    rf_center_freq = current_frequency.load();
    
    // Update gains
    set_gains_from_current_settings();
    
    // Update noise source state
    noise_source_state = bias_tee_enabled ? 1 : 0;
}

void KrakenIQHeader::set_gains_from_current_settings() {
    int gain = current_gain.load();
    uint32_t gain_value;
    
    if (gain == -1) {
        // Auto gain mode
        gain_value = 0; // KrakenSDR uses 0 for auto gain
    } else {
        // Manual gain - KrakenSDR expects gain in tenths of dB
        gain_value = static_cast<uint32_t>(gain); // gain is already in tenths of dB
    }
    
    // Set gains for active channels only
    for (int i = 0; i < 32; i++) {
        if (i < NUM_DEVICES) {
            if_gains[i] = gain_value;
        } else {
            if_gains[i] = 0; // Unused channels
        }
    }
}

// Helper function to write little-endian values
template<typename T>
void write_le(uint8_t*& ptr, T value) {
    memcpy(ptr, &value, sizeof(T));
    ptr += sizeof(T);
}

std::vector<uint8_t> KrakenIQHeader::encode() const {
    std::vector<uint8_t> buffer(HEADER_SIZE, 0);
    uint8_t* ptr = buffer.data();
    
    // Pack the header fields in the exact order expected by KrakenSDR Python code
    write_le(ptr, sync_word);           // uint32_t
    write_le(ptr, frame_type);          // uint32_t
    
    // Hardware ID - 16 bytes, null-padded
    memcpy(ptr, hardware_id, 16);
    ptr += 16;
    
    write_le(ptr, unit_id);             // uint32_t
    write_le(ptr, active_ant_chs);      // uint32_t
    write_le(ptr, ioo_type);            // uint32_t
    write_le(ptr, rf_center_freq);      // uint64_t
    write_le(ptr, adc_sampling_freq);   // uint64_t
    write_le(ptr, sampling_freq);       // uint64_t
    write_le(ptr, cpi_length);          // uint32_t
    write_le(ptr, time_stamp);          // uint64_t
    write_le(ptr, daq_block_index);     // uint32_t
    write_le(ptr, cpi_index);           // uint32_t
    write_le(ptr, ext_integration_cntr); // uint64_t
    write_le(ptr, data_type);           // uint32_t
    write_le(ptr, sample_bit_depth);    // uint32_t
    write_le(ptr, adc_overdrive_flags); // uint32_t
    
    // 32 gain values - uint32_t each
    for (int i = 0; i < 32; i++) {
        write_le(ptr, if_gains[i]);
    }
    
    write_le(ptr, delay_sync_flag);     // uint32_t
    write_le(ptr, iq_sync_flag);        // uint32_t
    write_le(ptr, sync_state);          // uint32_t
    write_le(ptr, noise_source_state);  // uint32_t
    
    // Reserved bytes - uint32_t each
    for (uint32_t i = 0; i < RESERVED_BYTES; i++) {
        write_le(ptr, reserved[i]);
    }
    
    write_le(ptr, header_version);      // uint32_t
    
    return buffer;
}

// Enhanced KrakenTcpServer with proper protocol handling
KrakenTcpServer::KrakenTcpServer() : server_socket(-1) {}

KrakenTcpServer::~KrakenTcpServer() {
    stop();
}

bool KrakenTcpServer::start(int port) {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) return false;
    
    if (!set_socket_reuse(server_socket)) {
        close(server_socket);
        return false;
    }
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_socket, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_socket);
        return false;
    }
    
    if (listen(server_socket, 5) < 0) {
        close(server_socket);
        return false;
    }
    
    server_thread = std::thread(&KrakenTcpServer::accept_loop, this);
    std::cout << "KrakenSDR TCP Server listening on port " << port << std::endl;
    return true;
}

void KrakenTcpServer::stop() {
    running = false;
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

void KrakenTcpServer::accept_loop() {
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        // Also check existing clients for commands
        int max_fd = server_socket;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (const auto& client : clients) {
                if (client->active) {
                    FD_SET(client->socket, &read_fds);
                    max_fd = std::max(max_fd, client->socket);
                }
            }
        }
        
        timeval timeout{0, 100000}; // 100ms timeout
        int result = select(max_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (result > 0) {
            // Check for new connections
            if (FD_ISSET(server_socket, &read_fds)) {
                sockaddr_in client_addr{};
                socklen_t addr_len = sizeof(client_addr);
                int client_socket = accept(server_socket, (sockaddr*)&client_addr, &addr_len);
                
                if (client_socket >= 0) {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    clients.push_back(std::make_unique<KrakenTcpClient>(client_socket));
                    std::cout << "KrakenSDR TCP: Client connected from " << inet_ntoa(client_addr.sin_addr) 
                         << ":" << ntohs(client_addr.sin_port) << std::endl;
                }
            }
            
            // Check existing clients for commands
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                for (auto& client : clients) {
                    if (client->active && FD_ISSET(client->socket, &read_fds)) {
                        handle_client_command(client.get());
                    }
                }
                
                // Remove inactive clients
                clients.erase(std::remove_if(clients.begin(), clients.end(),
                    [](const auto& client) { return !client->active; }), clients.end());
            }
        }
    }
}

void KrakenTcpServer::handle_client_command(KrakenTcpClient* client) {
    char buffer[256];
    ssize_t bytes_read = recv(client->socket, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    
    if (bytes_read <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            client->active = false;
        }
        return;
    }
    
    buffer[bytes_read] = '\0';
    std::string command(buffer);
    
    std::cout << "KrakenSDR TCP: Received command: '" << command << "'" << std::endl;
    
    // Handle KrakenSDR protocol commands
    if (command == "streaming") {
        client->streaming_mode = true;
        std::cout << "KrakenSDR TCP: Client entered streaming mode" << std::endl;
        
        // Send initial IQ frame immediately
        send_initial_frame(client);
    }
    else if (command == "IQDownload") {
        if (client->streaming_mode) {
            client->wants_iq_frame = true;
            std::cout << "KrakenSDR TCP: Client requested IQ frame" << std::endl;
        }
    }
    else if (command == "q") {
        std::cout << "KrakenSDR TCP: Client requested disconnect" << std::endl;
        client->active = false;
    }
    else {
        std::cout << "KrakenSDR TCP: Unknown command: " << command << std::endl;
    }
}

void KrakenTcpServer::send_initial_frame(KrakenTcpClient* client) {
    // Create a test frame with some sample data
    KrakenIQHeader header;
    header.update_frame_data(0, std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    auto header_bytes = header.encode();
    
    // Create some test IQ data
    size_t samples_per_channel = NUM_SAMPLES;
    size_t total_samples = NUM_DEVICES * samples_per_channel;
    size_t iq_data_size = total_samples * sizeof(std::complex<float>);
    
    std::vector<uint8_t> iq_data(iq_data_size);
    auto* iq_ptr = reinterpret_cast<std::complex<float>*>(iq_data.data());
    
    // Fill with test data (noise)
    for (size_t i = 0; i < total_samples; i++) {
        iq_ptr[i] = std::complex<float>(
            (rand() / float(RAND_MAX) - 0.5f) * 0.1f,
            (rand() / float(RAND_MAX) - 0.5f) * 0.1f
        );
    }
    
    // Send header
    ssize_t header_sent = send(client->socket, header_bytes.data(), header_bytes.size(), MSG_NOSIGNAL);
    if (header_sent != static_cast<ssize_t>(header_bytes.size())) {
        std::cout << "KrakenSDR TCP: Failed to send initial header" << std::endl;
        client->active = false;
        return;
    }
    
    // Send IQ data
    ssize_t data_sent = send(client->socket, iq_data.data(), iq_data.size(), MSG_NOSIGNAL);
    if (data_sent != static_cast<ssize_t>(iq_data.size())) {
        std::cout << "KrakenSDR TCP: Failed to send initial IQ data" << std::endl;
        client->active = false;
        return;
    }
    
    std::cout << "KrakenSDR TCP: Sent initial frame (" << header_bytes.size() 
              << " header + " << iq_data.size() << " IQ bytes)" << std::endl;
    
    // Debug: Print some header values
    std::cout << "DEBUG Header values:" << std::endl;
    std::cout << "  Sync word: 0x" << std::hex << header.sync_word << std::dec << std::endl;
    std::cout << "  Active channels: " << header.active_ant_chs << std::endl;
    std::cout << "  Center freq: " << header.rf_center_freq << " Hz (" << header.rf_center_freq/1e6 << " MHz)" << std::endl;
    std::cout << "  Sample rate: " << header.sampling_freq << " Hz (" << header.sampling_freq/1e6 << " MHz)" << std::endl;
    std::cout << "  CPI length: " << header.cpi_length << std::endl;
    std::cout << "  Sample bit depth: " << header.sample_bit_depth << std::endl;
}

void KrakenTcpServer::broadcast_data(const std::vector<ComplexBuffer>& channel_data) {
    if (channel_data.empty()) return;
    
    std::lock_guard<std::mutex> lock(clients_mutex);
    
    // Remove inactive clients
    clients.erase(std::remove_if(clients.begin(), clients.end(),
        [](const auto& client) { return !client->active; }), clients.end());
    
    if (clients.empty()) return;
    
    // Check if any client wants IQ frames
    bool any_wants_iq = false;
    for (auto& client : clients) {
        if (client->active && client->streaming_mode && client->wants_iq_frame) {
            any_wants_iq = true;
            break;
        }
    }
    
    if (any_wants_iq) {
        send_iq_frame(channel_data);
    }
}

void KrakenTcpServer::send_iq_frame(const std::vector<ComplexBuffer>& channel_data) {
    // Create KrakenSDR header
    KrakenIQHeader header;
    
    // Update header with current frame data
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    
    header.update_frame_data(frame_counter++, timestamp);
    
    // Encode header
    auto header_bytes = header.encode();
    
    // Prepare IQ data - KrakenSDR expects complex64 (float32 real + float32 imag)
    // Data layout: channel-by-channel, all samples for ch0, then all samples for ch1, etc.
    size_t samples_per_channel = channel_data[0].size();
    size_t total_samples = NUM_DEVICES * samples_per_channel;
    size_t iq_data_size = total_samples * sizeof(std::complex<float>);
    
    std::vector<uint8_t> iq_data(iq_data_size);
    auto* iq_ptr = reinterpret_cast<std::complex<float>*>(iq_data.data());
    
    // Copy channel data in KrakenSDR format (channel-by-channel)
    for (int ch = 0; ch < NUM_DEVICES; ch++) {
        if (ch < static_cast<int>(channel_data.size()) && channel_data[ch].size() == samples_per_channel) {
            // Copy samples for this channel
            memcpy(iq_ptr + (ch * samples_per_channel), 
                   channel_data[ch].data(), 
                   samples_per_channel * sizeof(std::complex<float>));
        } else {
            // Fill with zeros if channel data is missing or wrong size
            std::fill(iq_ptr + (ch * samples_per_channel), 
                     iq_ptr + ((ch + 1) * samples_per_channel), 
                     std::complex<float>(0.0f, 0.0f));
        }
    }
    
    // Send header + IQ data to clients that want it
    for (auto& client : clients) {
        if (client->active && client->streaming_mode && client->wants_iq_frame) {
            // Send header
            ssize_t header_sent = send(client->socket, header_bytes.data(), header_bytes.size(), MSG_NOSIGNAL);
            if (header_sent != static_cast<ssize_t>(header_bytes.size())) {
                client->active = false;
                continue;
            }
            
            // Send IQ data
            ssize_t data_sent = send(client->socket, iq_data.data(), iq_data.size(), MSG_NOSIGNAL);
            if (data_sent != static_cast<ssize_t>(iq_data.size())) {
                client->active = false;
                continue;
            }
            
            // Reset the wants_iq_frame flag - client needs to request next frame
            client->wants_iq_frame = false;
        }
    }
}
