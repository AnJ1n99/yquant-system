/*
        该类为交易系统中的高吞吐量一对多通信提供基于UDP的多播套接字抽象。
        McastSocket它用于通过UDP多播流发布和消费市场数据，
        使交易所能够同时向多个交易客户端广播更新，而无需建立单独的TCP连接
*/
#pragma once

#include <cstddef>
#include <functional>

#include "logging.h"
#include "socket_utils.h"

namespace common {
constexpr size_t McastBufferSize = 64 * 1024 * 1024;

class McastSocket {
 public:
  explicit McastSocket(Logger& logger) : logger_(logger) {
    outbound_data_.resize(McastBufferSize);
    inbound_data_.resize(McastBufferSize);
  }

  /// Initialize multicast socket to read from or publish to a stream.
  /// Does not join the multicast stream yet.
  auto init(const std::string& ip, const std::string& iface, int port,
            bool is_listening) -> int;

  /// Add / Join membership / subscription to a multicast stream.
  auto join(const std::string& ip) -> bool;

  /// Remove / Leave membership / subscription to a multicast stream.
  auto leave() -> void;

  /// Publish outgoing data and read incoming data.
  auto sendAndRecv() noexcept -> bool;

  /// Copy data to send buffers - does not send them out yet.
  auto send(const void* data, size_t len) noexcept -> void;
 public:
  int socket_fd_ = -1;

  /// Send and receive buffers, typically only one or the other is needed, not
  /// both.
  std::vector<char> outbound_data_;
  size_t next_send_valid_index_ = 0;
  std::vector<char> inbound_data_;
  size_t next_recv_valid_index_ = 0;

  /// Function wrapper for the method to call when data is read.
  std::function<void(McastSocket* s)> recv_callback_ = nullptr;

  std::string time_str_;
  Logger& logger_;
};
}  // namespace common