#pragma once

#include "../core/types.hpp"
#include "tcp_common.hpp"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

class TcpDataServer {
private:
    int server_socket;
    std::vector<std::unique_ptr<TcpClient>> clients;
    std::mutex clients_mutex;
    std::thread server_thread;
    std::atomic<bool> running{true};
    
    void accept_loop();
    
public:
    TcpDataServer();
    ~TcpDataServer();
    
    bool start();
    void stop();
    void broadcast_data(const std::vector<ComplexBuffer>& channel_data);

    // Live connected-client count for the status dashboard.
    size_t client_count() { std::lock_guard<std::mutex> lk(clients_mutex); return clients.size(); }
};