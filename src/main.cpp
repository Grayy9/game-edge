#include "game_edge/gateway.hpp"

#include <atomic>
#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::atomic_bool stop_requested = false;
void on_signal(int) { stop_requested = true; }

uint16_t parse_port(const char* text) {
  const std::string input(text);
  std::size_t consumed = 0;
  const int value = std::stoi(input, &consumed);
  if (value < 1 || value > 65535 || consumed != input.size())
    throw std::out_of_range("port");
  return static_cast<uint16_t>(value);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 6) {
    std::cerr << "usage: game_edge <listen-port> <upstream-ipv4> <upstream-port> [--idle-ms <ms>]\n"
                 "   or: game_edge <listen-port> --rooms <port,port,...>\n";
    return 2;
  }
  try {
    game_edge::GatewayConfig config{parse_port(argv[1]), argv[2], 0};
    if (argc == 4 && std::string(argv[2]) == "--rooms") {
      config.upstream_host = "127.0.0.1";
      const std::string ports = argv[3];
      std::size_t start = 0;
      while (start < ports.size()) {
        const auto comma = ports.find(',', start);
        const auto end = comma == std::string::npos ? ports.size() : comma;
        if (end == start) throw std::invalid_argument("empty room port");
        config.room_ports.push_back(parse_port(ports.substr(start, end - start).c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
      if (config.room_ports.empty() || ports.back() == ',') throw std::invalid_argument("room ports");
    } else {
      config.upstream_port = parse_port(argv[3]);
    }
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
