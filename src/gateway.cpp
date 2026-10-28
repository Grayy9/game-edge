#include "game_edge/gateway.hpp"

#include <array>
#include <iostream>
#include <utility>

namespace game_edge {

std::size_t Gateway::pending(const Endpoint& endpoint) {
  return endpoint.output.size() - endpoint.output_offset;
}

bool Gateway::enqueue(Endpoint& endpoint, const std::vector<uint8_t>& bytes) {
  if (bytes.size() > hard_limit - pending(endpoint)) return false;
  if (endpoint.output_offset != 0) {
    endpoint.output.erase(endpoint.output.begin(), endpoint.output.begin() + endpoint.output_offset);
    endpoint.output_offset = 0;
  }
  endpoint.output.insert(endpoint.output.end(), bytes.begin(), bytes.end());
  return true;
}

bool Gateway::flush(Endpoint& endpoint) {
  while (pending(endpoint) != 0) {
    const int count = socket_write(endpoint.socket, endpoint.output.data() + endpoint.output_offset,
                                   pending(endpoint));
    if (count > 0) {
      endpoint.output_offset += static_cast<std::size_t>(count);
    } else if (count < 0 && would_block()) {
      break;
    } else {
      return false;
    }
  }
  if (pending(endpoint) == 0) {
    endpoint.output.clear();
    endpoint.output_offset = 0;
  }
  return true;
}

bool Gateway::read(Session& session, bool from_client) {
  Endpoint& source = from_client ? session.client : session.upstream;
  Endpoint& target = from_client ? session.upstream : session.client;
  std::array<uint8_t, 8192> buffer{};
  // A busy connection must return control to the event loop promptly.
  for (int reads = 0; reads < 16 && !source.paused; ++reads) {
    const int count = socket_read(source.socket, buffer.data(), buffer.size());
    if (count == 0) {
      if (from_client) return false;
      end_upstream(session);
      return true;
    }
    if (count < 0) return would_block();
    if (from_client) client_bytes_ += static_cast<std::uint64_t>(count);
    else upstream_bytes_ += static_cast<std::uint64_t>(count);
    if (from_client) session.last_client = std::chrono::steady_clock::now();
    if (!source.decoder.feed(buffer.data(), static_cast<std::size_t>(count))) return false;
    Frame frame{};
    while (source.decoder.pop(frame)) {
      if (from_client) {
        if (frame.type == 2) {
          if (!enqueue(session.client, encode_frame(Frame{3, {}}))) return false;
        } else if (frame.type == 4) {
          if (config_.room_ports.empty() || session.room_selected) return false;
          const auto& bytes = frame.payload;
          const uint32_t room = (static_cast<uint32_t>(bytes[0]) << 24) |
                                (static_cast<uint32_t>(bytes[1]) << 16) |
                                (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3];
          const auto port = config_.room_ports[room % config_.room_ports.size()];
          const Socket upstream = connect_nonblocking(config_.upstream_host, port);
          if (upstream == invalid_socket) return false;
          if (!poller_.watch(upstream, true, true)) {
            close_socket(upstream);
            return false;
          }
          session.upstream.socket = upstream;
          session.room_selected = true;
          index_[upstream] = {session.id, false};
        } else if (frame.type == 1) {
          if (!session.room_selected) return false;
          if (!enqueue(target, encode_frame(frame))) return false;
        }
      } else {
        if (frame.type != 1 || !enqueue(target, encode_frame(frame))) return false;
      }
    }
    if (pending(target) >= high_water) {
      source.paused = true;
      ++backpressure_pauses_;
      break;
    }
  }
  return true;
}

bool Gateway::refresh(Session& session) {
  if (session.client.paused && pending(session.upstream) <= low_water) session.client.paused = false;
  if (session.upstream.paused && pending(session.client) <= low_water) session.upstream.paused = false;
  if (!poller_.modify(session.client.socket, !session.client.paused && !session.upstream_closed,
                      pending(session.client) != 0)) return false;
  if (!session.room_selected) return true;
  if (session.upstream_closed) return true;
  if (session.upstream_detached) {
    if (session.upstream.paused) return true;
    if (!poller_.watch(session.upstream.socket, true,
                       session.connecting || pending(session.upstream) != 0)) return false;
    session.upstream_detached = false;
    return true;
  }
  return poller_.modify(session.upstream.socket, !session.upstream.paused,
                        session.connecting || pending(session.upstream) != 0);
}

void Gateway::end_upstream(Session& session) {
  poller_.remove(session.upstream.socket);
  index_.erase(session.upstream.socket);
  close_socket(session.upstream.socket);
  session.upstream.socket = invalid_socket;
  session.upstream_closed = true;
  session.connecting = false;
}

bool Gateway::handle(Session& session, bool from_client, const PollEvent& event) {
  Endpoint& endpoint = from_client ? session.client : session.upstream;
  if (!from_client && endpoint.paused && event.error) {
    // A hangup is level-triggered even when reads are paused. Reattach after
    // the client's output falls below the low watermark, then drain the EOF.
    poller_.remove(endpoint.socket);
    session.upstream_detached = true;
    return refresh(session);
  }
  if (!from_client && session.connecting && (event.writable || event.error)) {
    if (pending_socket_error(endpoint.socket) != 0) return false;
    session.connecting = false;
  }
  if ((event.readable || (!from_client && event.error)) && !endpoint.paused &&
      !session.upstream_closed && !read(session, from_client)) return false;
  if (event.writable && endpoint.socket != invalid_socket &&
      !(!from_client && session.connecting) && !flush(endpoint)) return false;
  if (event.error && !event.readable && from_client) return false;
  if (session.upstream_closed && pending(session.client) == 0) return false;
  return refresh(session);
}

void Gateway::accept_clients() {
  while (true) {
    const Socket client = accept_nonblocking(listener_);
    if (client == invalid_socket) return;
    const bool legacy = config_.room_ports.empty();
    const Socket upstream = legacy ? connect_nonblocking(config_.upstream_host, config_.upstream_port)
                                   : invalid_socket;
    if (legacy && upstream == invalid_socket) {
      close_socket(client);
      continue;
    }
    auto session = std::make_unique<Session>();
    session->id = next_id_++;
    session->client.socket = client;
    session->upstream.socket = upstream;
    session->room_selected = legacy;
    session->last_client = std::chrono::steady_clock::now();
    if (!poller_.watch(client, true, false) ||
        (legacy && !poller_.watch(upstream, true, true))) {
      poller_.remove(client);
      if (legacy) poller_.remove(upstream);
      close_socket(client);
      close_socket(upstream);
      continue;
    }
    index_[client] = {session->id, true};
    if (legacy) index_[upstream] = {session->id, false};
    sessions_[session->id] = std::move(session);
    ++accepted_;
  }
}

void Gateway::close_session(int id, CloseReason reason) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return;
  if (reason == CloseReason::io) ++closed_io_;
  else if (reason == CloseReason::idle) ++closed_idle_;
  else ++closed_shutdown_;
  for (const Socket socket : {it->second->client.socket, it->second->upstream.socket}) {
    if (socket == invalid_socket) continue;
    poller_.remove(socket);
    index_.erase(socket);
    close_socket(socket);
  }
  sessions_.erase(it);
}

int Gateway::run(std::atomic_bool& stop_requested) {
  if (!init_network()) return 1;
  listener_ = create_listener(config_.listen_port);
  if (listener_ == invalid_socket || !poller_.watch(listener_, true, false)) {
    std::cerr << "failed to listen\n";
    close_socket(listener_);
    cleanup_network();
    return 1;
  }
  std::cerr << "listening on 127.0.0.1:" << local_port(listener_) << '\n';
  bool draining = false;
  std::chrono::steady_clock::time_point drain_deadline{};
  while (true) {
    if (stop_requested && !draining) {
      draining = true;
      drain_deadline = std::chrono::steady_clock::now() + config_.drain_timeout;
      poller_.remove(listener_);
      close_socket(listener_);
      listener_ = invalid_socket;
    }
    if (draining && (sessions_.empty() || std::chrono::steady_clock::now() >= drain_deadline)) break;
    for (const auto event : poller_.wait(100)) {
      if (event.socket == listener_) {
        accept_clients();
        continue;
      }
      const auto found = index_.find(event.socket);
      if (found == index_.end()) continue;
      const auto [id, from_client] = found->second;
      auto session = sessions_.find(id);
      if (session != sessions_.end() && !handle(*session->second, from_client, event))
        close_session(id, CloseReason::io);
    }
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    for (const auto& [id, session] : sessions_) {
      if (now - session->last_client >= config_.idle_timeout) expired.push_back(id);
    }
    for (const int id : expired) close_session(id, CloseReason::idle);
  }
  std::vector<int> remaining;
  for (const auto& [id, session] : sessions_) remaining.push_back(id);
  for (const int id : remaining) close_session(id, CloseReason::shutdown);
  if (listener_ != invalid_socket) {
    poller_.remove(listener_);
    close_socket(listener_);
  }
  std::cerr << "accepted=" << accepted_ << " client_bytes=" << client_bytes_
            << " upstream_bytes=" << upstream_bytes_ << " backpressure_pauses="
            << backpressure_pauses_ << " closed_io=" << closed_io_
            << " closed_idle=" << closed_idle_ << " closed_shutdown=" << closed_shutdown_ << '\n';
  cleanup_network();
  return 0;
}

}  // namespace game_edge
