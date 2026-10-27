#pragma once

#include "game_edge/net.hpp"

#include <vector>
#ifdef _WIN32
#include <map>
#endif

namespace game_edge {

struct PollEvent { Socket socket; bool readable; bool writable; bool error; };

class Poller {
 public:
  Poller();
  ~Poller();
  Poller(const Poller&) = delete;
  Poller& operator=(const Poller&) = delete;
  bool watch(Socket socket, bool read, bool write);
  bool modify(Socket socket, bool read, bool write);
  void remove(Socket socket);
  std::vector<PollEvent> wait(int timeout_ms);

 private:
#ifdef _WIN32
  std::map<Socket, short> watched_;
#else
  int fd_ = -1;
#endif
};

}  // namespace game_edge
