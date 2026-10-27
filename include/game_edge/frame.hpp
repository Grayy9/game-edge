#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace game_edge {

constexpr std::size_t max_payload = 64 * 1024;

struct Frame {
  uint8_t type;
  std::vector<uint8_t> payload;
};

std::vector<uint8_t> encode_frame(const Frame& frame);

class FrameDecoder {
 public:
  bool feed(const uint8_t* data, std::size_t size);
  bool pop(Frame& frame);

 private:
  std::array<uint8_t, 4> header_{};
  std::size_t header_size_ = 0;
  std::size_t body_size_ = 0;
  std::vector<uint8_t> body_;
  std::deque<Frame> ready_;
  bool valid_ = true;
};

}  // namespace game_edge
