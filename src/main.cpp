#include "game_edge/gateway.hpp"

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>

namespace {
std::atomic_bool stop_requested = false;
void on_signal(int) { stop_requested = true; }

uint16_t parse_port(const char* text) {
  const int value = std::stoi(text);
  if (value < 1 || value > 65535) throw std::out_of_range("port");
  return static_cast<uint16_t>(value);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 6) {
    std::cerr << "usage: game_edge <listen-port> <upstream-ipv4> <upstream-port> [--idle-ms <ms>]\n";
    return 2;
  }
  try {
    game_edge::GatewayConfig config{parse_port(argv[1]), argv[2], parse_port(argv[3])};
    if (argc == 6) {
      if (std::string(argv[4]) != "--idle-ms") throw std::invalid_argument("option");
      const int idle = std::stoi(argv[5]);
      if (idle < 1) throw std::out_of_range("idle");
      config.idle_timeout = std::chrono::milliseconds(idle);
    }
    std::signal(SIGINT, on_signal);
#ifndef _WIN32
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
#endif
    game_edge::Gateway gateway(config);
    return gateway.run(stop_requested);
  } catch (const std::exception& error) {
    std::cerr << "invalid arguments: " << error.what() << '\n';
    return 2;
  }
}
