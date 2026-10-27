#include "game_edge/net.hpp"
#include "game_edge/poller.hpp"
#include "check.hpp"

#include <cstdint>

int main() {
  using namespace game_edge;
  CHECK(init_network());
  const Socket listener = create_listener(0);
  CHECK(listener != invalid_socket);
  const auto port = local_port(listener);
  CHECK(port != 0);
  Poller poller;
  CHECK(poller.watch(listener, true, false));
  const Socket client = connect_nonblocking("127.0.0.1", port);
  CHECK(client != invalid_socket);
  bool accepted = false;
  Socket peer = invalid_socket;
  for (int i = 0; i < 20 && !accepted; ++i) {
    for (const auto event : poller.wait(100)) {
      if (event.socket == listener && event.readable) {
        peer = accept_nonblocking(listener);
        accepted = peer != invalid_socket;
      }
    }
  }
  CHECK(accepted);
  const uint8_t byte = 42;
  CHECK(socket_write(client, &byte, 1) == 1);
  CHECK(poller.watch(peer, true, false));
  bool received = false;
  for (int i = 0; i < 20 && !received; ++i) {
    for (const auto event : poller.wait(100)) {
      if (event.socket == peer && event.readable) {
        uint8_t value = 0;
        received = socket_read(peer, &value, 1) == 1 && value == byte;
      }
    }
  }
  CHECK(received);
  poller.remove(peer);
  poller.remove(listener);
  close_socket(peer);
  close_socket(client);
  close_socket(listener);
  cleanup_network();
}
