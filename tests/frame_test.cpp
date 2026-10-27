#include "game_edge/frame.hpp"
#include "check.hpp"

#include <cstdint>
#include <vector>

int main() {
  using game_edge::Frame;
  using game_edge::FrameDecoder;
  using game_edge::encode_frame;

  FrameDecoder decoder;
  const auto first = encode_frame(Frame{1, {'h', 'i'}});
  CHECK(decoder.feed(first.data(), 2));
  Frame out;
  CHECK(!decoder.pop(out));
  CHECK(decoder.feed(first.data() + 2, first.size() - 3));
  CHECK(!decoder.pop(out));
  CHECK(decoder.feed(first.data() + first.size() - 1, 1));
  CHECK(decoder.pop(out) && out.type == 1 && out.payload == std::vector<uint8_t>({'h', 'i'}));

  const auto ping = encode_frame(Frame{2, {}});
  const auto pong = encode_frame(Frame{3, {}});
  std::vector<uint8_t> joined = ping;
  joined.insert(joined.end(), pong.begin(), pong.end());
  CHECK(decoder.feed(joined.data(), joined.size()));
  CHECK(decoder.pop(out) && out.type == 2 && out.payload.empty());
  CHECK(decoder.pop(out) && out.type == 3 && out.payload.empty());
  CHECK(!decoder.pop(out));

  FrameDecoder invalid;
  const uint8_t zero[] = {0, 0, 0, 0};
  CHECK(!invalid.feed(zero, sizeof(zero)));
  FrameDecoder oversized;
  const uint8_t large[] = {0, 1, 0, 2};
  CHECK(!oversized.feed(large, sizeof(large)));
  FrameDecoder unknown;
  const uint8_t bad_type[] = {0, 0, 0, 1, 9};
  CHECK(!unknown.feed(bad_type, sizeof(bad_type)));
  FrameDecoder ping_payload;
  const uint8_t bad_ping[] = {0, 0, 0, 2, 2, 7};
  CHECK(!ping_payload.feed(bad_ping, sizeof(bad_ping)));
}
