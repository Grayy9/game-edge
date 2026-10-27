#include "game_edge/net.hpp"

#include <algorithm>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace game_edge {

bool init_network() {
#ifdef _WIN32
  WSADATA data{};
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return true;
#endif
}

void cleanup_network() {
#ifdef _WIN32
  WSACleanup();
#endif
}

bool set_nonblocking(Socket socket) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void close_socket(Socket socket) {
  if (socket == invalid_socket) return;
#ifdef _WIN32
  closesocket(socket);
#else
  close(socket);
#endif
}

bool would_block() {
#ifdef _WIN32
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

int pending_socket_error(Socket socket) {
  int error = 0;
#ifdef _WIN32
  int size = sizeof(error);
#else
  socklen_t size = sizeof(error);
#endif
  if (getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) != 0) return -1;
  return error;
}

Socket create_listener(uint16_t port) {
  Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket == invalid_socket) return invalid_socket;
  int reuse = 1;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (!set_nonblocking(socket) || bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(socket, 128) != 0) {
    close_socket(socket);
    return invalid_socket;
  }
  return socket;
}

uint16_t local_port(Socket socket) {
  sockaddr_in address{};
#ifdef _WIN32
  int size = sizeof(address);
#else
  socklen_t size = sizeof(address);
#endif
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) != 0) return 0;
  return ntohs(address.sin_port);
}

Socket accept_nonblocking(Socket listener) {
  Socket peer = accept(listener, nullptr, nullptr);
  if (peer == invalid_socket) return peer;
  if (!set_nonblocking(peer)) {
    close_socket(peer);
    return invalid_socket;
  }
  return peer;
}

Socket connect_nonblocking(const char* ipv4, uint16_t port) {
  Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket == invalid_socket) return socket;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  if (inet_pton(AF_INET, ipv4, &address.sin_addr) != 1 || !set_nonblocking(socket)) {
    close_socket(socket);
    return invalid_socket;
  }
  if (connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
#else
    if (errno != EINPROGRESS) {
#endif
      close_socket(socket);
      return invalid_socket;
    }
  }
  return socket;
}

int socket_read(Socket socket, uint8_t* data, std::size_t size) {
  return recv(socket, reinterpret_cast<char*>(data), static_cast<int>(std::min<std::size_t>(size, 1 << 20)), 0);
}

int socket_write(Socket socket, const uint8_t* data, std::size_t size) {
  return send(socket, reinterpret_cast<const char*>(data), static_cast<int>(std::min<std::size_t>(size, 1 << 20)), 0);
}

}  // namespace game_edge
