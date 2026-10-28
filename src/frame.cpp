#include "game_edge/frame.hpp"

#include <algorithm>
#include <stdexcept>

namespace game_edge {
namespace {
bool valid_type(uint8_t type, std::size_t payload_size) {
  return type == 1 || ((type == 2 || type == 3) && payload_size == 0) ||
         (type == 4 && payload_size == 4);
}
}  // namespace

std::vector<uint8_t> encode_frame(const Frame& frame) {
  if (frame.payload.size() > max_payload || !valid_type(frame.type, frame.payload.size())) {
    throw std::invalid_argument("invalid frame");
  }
  const auto length = static_cast<uint32_t>(frame.payload.size() + 1);
  std::vector<uint8_t> encoded{
      static_cast<uint8_t>(length >> 24), static_cast<uint8_t>(length >> 16),
      static_cast<uint8_t>(length >> 8), static_cast<uint8_t>(length), frame.type};
  encoded.insert(encoded.end(), frame.payload.begin(), frame.payload.end());
  return encoded;
}

bool FrameDecoder::feed(const uint8_t* data, std::size_t size) {
  if (!valid_) return false;
  std::size_t offset = 0;
  while (offset < size) {
    if (header_size_ < header_.size()) {
      header_[header_size_++] = data[offset++];
      if (header_size_ != header_.size()) continue;
      body_size_ = (static_cast<uint32_t>(header_[0]) << 24) |
                   (static_cast<uint32_t>(header_[1]) << 16) |
                   (static_cast<uint32_t>(header_[2]) << 8) | header_[3];
      if (body_size_ == 0 || body_size_ > max_payload + 1) {
        valid_ = false;
        return false;
      }
      body_.reserve(body_size_);
    }
    const auto count = std::min(body_size_ - body_.size(), size - offset);
    body_.insert(body_.end(), data + offset, data + offset + count);
    offset += count;
    if (body_.size() != body_size_) continue;
    const auto type = body_[0];
    if (!valid_type(type, body_size_ - 1)) {
      valid_ = false;
      return false;
    }
    ready_.push_back(Frame{type, {body_.begin() + 1, body_.end()}});
    body_.clear();
    header_size_ = 0;
    body_size_ = 0;
  }
  return true;
}

bool FrameDecoder::pop(Frame& frame) {
  if (ready_.empty()) return false;
  frame = std::move(ready_.front());
  ready_.pop_front();
  return true;
}

}  // namespace game_edge
