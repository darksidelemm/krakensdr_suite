#include "kraken_control_server.hpp"
#include "tcp_common.hpp"
#include "../sdr/sdr_init.hpp"
#include "../core/config.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>

// External declarations
extern std::vector<std::unique_ptr<SDRDevice>> devices;
extern void handle_settings_change();

KrakenControlServer::KrakenControlServer() : server_socket(-1) {}

KrakenControlServer::~KrakenControlServer() {
    stop();
}

bool KrakenControlServer::start(int port) {
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
    
    if (bind(server_socket, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(server_socket, 5) < 0) {
        close(server_socket);
        return false;
    }
    
    server_thread = std::thread(&KrakenControlServer::control_loop, this);
    std::cout << "KrakenSDR Control Server listening on port " << port << std::endl;
    return true;
}

void KrakenControlServer::stop() {
    running = false;
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

void KrakenControlServer::control_loop() {
    while (running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket, &read_fds);
        
        int max_fd = server_socket;
        
        // Add existing clients to read set
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
                    clients.push_back(std::make_unique<TcpClient>(client_socket));
                    std::cout << "KrakenSDR Control: Client connected from " << inet_ntoa(client_addr.sin_addr) << std::endl;
                }
            }
            
            // Check existing clients for commands
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

void KrakenControlServer::handle_client_command(TcpClient* client) {
    char buffer[128];
    ssize_t bytes_read = recv(client->socket, buffer, sizeof(buffer), 0);
    
    if (bytes_read <= 0) {
        client->active = false;
        return;
    }
    
    // KrakenSDR control commands are 128 bytes fixed length
    if (bytes_read == 128) {
        std::string command(buffer, 4); // First 4 bytes are the command
        std::string response;
        
        if (process_command(command, response)) {
            send_response(client, response);
        } else {
            send_response(client, "FAIL");
        }
    }
}

void KrakenControlServer::send_response(TcpClient* client, const std::string& response) {
    char response_buffer[128];
    memset(response_buffer, 0, sizeof(response_buffer));
    
    // Copy response (ensure null termination)
    size_t copy_len = std::min(response.length(), size_t(3)); // Leave room for null terminator
    strncpy(response_buffer, response.c_str(), copy_len);
    response_buffer[copy_len] = '\0';
    
    ssize_t sent = send(client->socket, response_buffer, sizeof(response_buffer), MSG_NOSIGNAL);
    if (sent != sizeof(response_buffer)) {
        client->active = false;
    }
}

bool KrakenControlServer::process_command(const std::string& command, std::string& response) {
    if (command == "INIT") {
        // Initialization command
        std::cout << "KrakenSDR Control: Received INIT command" << std::endl;
        response = "FNSD"; // Finished
        return true;
    }
    else if (command == "FREQ") {
        // Frequency change command - frequency is in the next 8 bytes
        // For now, we'll just acknowledge. Full implementation would parse the frequency.
        std::cout << "KrakenSDR Control: Received FREQ command" << std::endl;
        response = "FNSD";
        return true;
    }
    else if (command == "GAIN") {
        // Gain change command
        std::cout << "KrakenSDR Control: Received GAIN command" << std::endl;
        response = "FNSD";
        return true;
    }
    else if (command == "AGC ") {
        // Auto gain control command
        std::cout << "KrakenSDR Control: Received AGC command" << std::endl;
        bool changed = update_sdr_settings(0, -1, devices); // Set to auto gain
        if (changed) handle_settings_change();
        response = "FNSD";
        return true;
    }
    else if (command == "EXIT") {
        // Exit command
        std::cout << "KrakenSDR Control: Received EXIT command" << std::endl;
        response = "FNSD";
        return true;
    }
    else {
        std::cout << "KrakenSDR Control: Unknown command: " << command << std::endl;
        response = "FAIL";
        return false;
    }
}
