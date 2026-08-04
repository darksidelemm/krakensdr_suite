#pragma once

#include <string>
#include <cstddef>

class TCPClient {
private:
    int socket_fd = -1;
    bool connected = false;
    
public:
    ~TCPClient();
    
    bool connect_to_port(int port, const std::string& host = "127.0.0.1");
    void disconnect();
    
    bool send_data(const void* data, size_t size);
    ssize_t receive_data(void* buffer, size_t size);
    
    void set_nonblocking(bool nonblock = true);
    
    bool is_connected() const;
    int get_socket() const;
};
