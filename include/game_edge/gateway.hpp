#pragma once

#include "game_edge/frame.hpp"
#include "game_edge/net.hpp"
#include "game_edge/poller.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace game_edge {

struct GatewayConfig {
  uint16_t listen_port;
  const char* upstream_host;
  uint16_t upstream_port;
  std::chrono::milliseconds idle_timeout{30000};
  std::chrono::milliseconds drain_timeout{5000};
};

class Gateway {
 public:
  explicit Gateway(GatewayConfig config) : config_(config) {}
  int run(std::atomic_bool& stop_requested);

 private:
  static constexpr std::size_t hard_limit = 1024 * 1024;
  static constexpr std::size_t high_water = 768 * 1024;
  static constexpr std::size_t low_water = 256 * 1024;

  struct Endpoint {
    Socket socket = invalid_socket;
    FrameDecoder decoder;
    std::vector<uint8_t> output;
    std::size_t output_offset = 0;
    bool paused = false;
  };

  struct Session {
    int id;
    Endpoint client;
    Endpoint upstream;
    bool connecting = true;
    bool upstream_closed = false;
    bool upstream_detached = false;
    std::chrono::steady_clock::time_point last_client;
  };

  enum class CloseReason { io, idle, shutdown };

  static std::size_t pending(const Endpoint& endpoint);
  bool enqueue(Endpoint& endpoint, const std::vector<uint8_t>& bytes);
  bool flush(Endpoint& endpoint);
  bool read(Session& session, bool from_client);
  bool handle(Session& session, bool from_client, const PollEvent& event);
  bool refresh(Session& session);
  void end_upstream(Session& session);
  void accept_clients();
  void close_session(int id, CloseReason reason);

  GatewayConfig config_;
  Poller poller_;
  Socket listener_ = invalid_socket;
  int next_id_ = 1;
  std::unordered_map<int, std::unique_ptr<Session>> sessions_;
  std::unordered_map<Socket, std::pair<int, bool>> index_;
  std::uint64_t accepted_ = 0;
  std::uint64_t client_bytes_ = 0;
  std::uint64_t upstream_bytes_ = 0;
  std::uint64_t backpressure_pauses_ = 0;
  std::uint64_t closed_io_ = 0;
  std::uint64_t closed_idle_ = 0;
  std::uint64_t closed_shutdown_ = 0;
};

}  // namespace game_edge
