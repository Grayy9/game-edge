# GameEdge Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a testable C++20 long-connection game gateway that relays framed client data to one room backend with bounded memory and clear connection behavior.

**Architecture:** A frame codec owns parsing and encoding. A platform poller wraps Linux `epoll` or Windows `WSAPoll`; one gateway event loop owns client/upstream socket pairs and their buffers. Python fixtures exercise the complete data path.

**Tech Stack:** C++20, sockets, Linux epoll, Windows WSAPoll for development, CMake, Python standard library.

**Spec:** `docs/superpowers/specs/2026-10-27-game-edge-design.md`

## Global Constraints

- Protocol frame length is a 4-byte big-endian value that includes a 1-byte type; payload limit is 64 KiB.
- Data/Ping/Pong types are `0x01`/`0x02`/`0x03`; Ping/Pong carry no payload.
- Per-direction hard buffer limit is 1 MiB; high/low watermarks are 768/256 KiB.
- Client idle timeout is 30 seconds; shutdown drain limit is 5 seconds.
- No third-party runtime dependencies; Linux production path uses epoll.

## Review Focus

- Incomplete length header across reads must not be mistaken for an invalid frame: Task 1 test.
- A length that exceeds the limit must be rejected before allocating its announced size: Task 1 test.
- A backend disconnect must not close other client sessions: Task 3 integration test.
- A slow peer must never grow the pending output beyond the hard limit: Task 3 integration test.
- Shutdown during an active connection must close resources within the drain limit: Task 3 integration test.

---

### Task 1: Framing codec

**Files:** `include/game_edge/frame.hpp`, `src/frame.cpp`, `tests/frame_test.cpp`, `CMakeLists.txt`.

**Interfaces:** `Frame { uint8_t type; std::vector<uint8_t> payload; }`; `FrameDecoder::feed(const uint8_t*, size_t) -> bool`; `FrameDecoder::pop(Frame&) -> bool`; `encode_frame(const Frame&) -> std::vector<uint8_t>`.

- [x] Write tests for partial header/body, coalesced frames, illegal type, zero/oversize length, and empty Ping/Pong.
- [x] Compile and run them to observe failure before codec implementation.
- [x] Implement the smallest streaming decoder and encoder satisfying the protocol.
- [x] Run the codec tests and confirm all pass on Windows.

### Task 2: Poller and portable socket helpers

**Files:** `include/game_edge/net.hpp`, `src/net.cpp`, `include/game_edge/poller.hpp`, `src/poller.cpp`, `tests/net_test.cpp`.

**Interfaces:** `Socket` handle, `set_nonblocking`, `close_socket`, `connect_nonblocking`, `Poller::watch/modify/remove/wait`; readiness includes readable, writable, error.

- [x] Write a loopback test that accepts, connects, sends bytes and observes readiness.
- [x] Run it and observe failure before networking implementation.
- [x] Implement socket helpers and Linux epoll / Windows WSAPoll polling.
- [x] Run loopback tests and confirm all pass on Windows.

### Task 3: Gateway event loop

**Files:** `include/game_edge/gateway.hpp`, `src/gateway.cpp`, `src/main.cpp`, `tests/test_gateway.py`, `scripts/mock_room.py`.

**Interfaces:** `GatewayConfig { listen_port, upstream_host, upstream_port }`; `Gateway::run(std::atomic_bool&) -> int`.

- [x] Write Python integration tests for forwarding, fragment/coalescing, heartbeat, invalid frames, upstream failure isolation, slow peer isolation, idle timeout override, and signal shutdown.
- [x] Run tests to observe failure before gateway implementation.
- [x] Implement session pairs, decoding, forwarding, write queues, backpressure, timeouts and shutdown.
- [x] Run available Windows integration tests; one SIGTERM test is Linux-only.
- [ ] Run Linux CI integration tests and inspect the results.

### Task 4: Portfolio documentation and benchmark

**Files:** `README.md`, `scripts/bench.py`, `.github/workflows/ci.yml`.

**Interfaces:** `bench.py` accepts host/port, clients, requests and prints environment plus throughput and P50/P99 latencies.

- [x] Write a smoke check that benchmark output includes workload and latency fields.
- [x] Run it and observe failure before the benchmark script exists.
- [x] Implement benchmark script, README and Linux CI build/test steps.
- [x] Run the smoke check and full available Windows test suite.
- [ ] Run CMake build and tests in Linux CI once a GitHub repository exists.
