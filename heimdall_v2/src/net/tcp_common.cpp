#include "tcp_common.hpp"

TcpClient::TcpClient(int s) : socket(s), active(true) {}

TcpClient::~TcpClient() { 
    if (socket >= 0) close(socket); 
}

bool set_socket_nonblocking(int socket) {
    return fcntl(socket, F_SETFL, O_NONBLOCK) >= 0;
}

bool set_socket_reuse(int socket) {
    int opt = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) >= 0;
}