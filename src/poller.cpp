#include "game_edge/poller.hpp"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/epoll.h>
#include <unistd.h>
#endif

namespace game_edge {

Poller::Poller() {
#ifndef _WIN32
  fd_ = epoll_create1(EPOLL_CLOEXEC);
#endif
}

Poller::~Poller() {
#ifndef _WIN32
  if (fd_ >= 0) close(fd_);
#endif
}

bool Poller::watch(Socket socket, bool read, bool write) {
#ifdef _WIN32
  watched_[socket] = static_cast<short>((read ? POLLRDNORM : 0) | (write ? POLLWRNORM : 0));
  return true;
#else
  epoll_event event{};
  event.data.fd = socket;
  event.events = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0) | EPOLLRDHUP;
  return fd_ >= 0 && epoll_ctl(fd_, EPOLL_CTL_ADD, socket, &event) == 0;
#endif
}

bool Poller::modify(Socket socket, bool read, bool write) {
#ifdef _WIN32
  if (!watched_.contains(socket)) return false;
  watched_[socket] = static_cast<short>((read ? POLLRDNORM : 0) | (write ? POLLWRNORM : 0));
  return true;
#else
  epoll_event event{};
  event.data.fd = socket;
  event.events = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0) | EPOLLRDHUP;
  return epoll_ctl(fd_, EPOLL_CTL_MOD, socket, &event) == 0;
#endif
}

void Poller::remove(Socket socket) {
#ifdef _WIN32
  watched_.erase(socket);
#else
  epoll_ctl(fd_, EPOLL_CTL_DEL, socket, nullptr);
#endif
}

std::vector<PollEvent> Poller::wait(int timeout_ms) {
  std::vector<PollEvent> result;
#ifdef _WIN32
  std::vector<WSAPOLLFD> fds;
  fds.reserve(watched_.size());
  for (const auto& [socket, events] : watched_) fds.push_back(WSAPOLLFD{socket, events, 0});
  if (fds.empty()) return result;
  const int count = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeout_ms);
  if (count <= 0) return result;
  for (const auto& fd : fds) {
    if (!fd.revents) continue;
    result.push_back({fd.fd, bool(fd.revents & (POLLRDNORM | POLLIN)),
                      bool(fd.revents & (POLLWRNORM | POLLOUT)),
                      bool(fd.revents & (POLLERR | POLLHUP | POLLNVAL))});
  }
#else
  epoll_event events[128];
  const int count = epoll_wait(fd_, events, 128, timeout_ms);
  for (int i = 0; i < count; ++i) {
    result.push_back({events[i].data.fd, bool(events[i].events & EPOLLIN),
                      bool(events[i].events & EPOLLOUT),
                      bool(events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))});
  }
#endif
  return result;
}

}  // namespace game_edge
