// heimdall_v2/heimdall_v2/c_heimdall_split/src/net/kraken_control_server.hpp
#pragma once

#include "../core/types.hpp"
#include "tcp_common.hpp"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>

// KrakenSDR Control Server - handles frequency/gain commands from Python DoA code
class KrakenControlServer {
private:
    int server_socket;
    std::vector<std::unique_ptr<TcpClient>> clients;
    std::mutex clients_mutex;
    std::thread server_thread;
    std::atomic<bool> running{true};
    
    void control_loop();
    void handle_client_command(TcpClient* client);
    void send_response(TcpClient* client, const std::string& response);
    bool process_command(const std::string& command, std::string& response);
    
public:
    KrakenControlServer();
    ~KrakenControlServer();
    
    bool start(int port = 5001);
    void stop();
};
