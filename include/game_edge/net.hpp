#pragma once

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
namespace game_edge { using Socket = SOCKET; constexpr Socket invalid_socket = INVALID_SOCKET; }
#else
namespace game_edge { using Socket = int; constexpr Socket invalid_socket = -1; }
#endif

namespace game_edge {

bool init_network();
void cleanup_network();
bool set_nonblocking(Socket socket);
void close_socket(Socket socket);
bool would_block();
int pending_socket_error(Socket socket);
Socket create_listener(uint16_t port);
uint16_t local_port(Socket socket);
Socket accept_nonblocking(Socket listener);
Socket connect_nonblocking(const char* ipv4, uint16_t port);
int socket_read(Socket socket, uint8_t* data, std::size_t size);
int socket_write(Socket socket, const uint8_t* data, std::size_t size);

}  // namespace game_edge
