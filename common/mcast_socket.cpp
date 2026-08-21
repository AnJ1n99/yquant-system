#include "mcast_socket.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>

#include "socket_utils.h"
#include "time_utils.h"

namespace common {
auto McastSocket::init(const std::string& ip, const std::string& iface,
                       int port, bool is_listening) -> int {
  const SocketCfg CFG{ip, iface, port, true, is_listening, false};
  socket_fd_ = createSocket(logger_, CFG);
  return socket_fd_;
}

auto McastSocket::join(const std::string& ip) -> bool {
  return common::join(socket_fd_, ip);
}

// Remove / Leave membership / subscription to a multicast stream.
// 关闭套接字会自动移除所有多播成员关系。
auto McastSocket::leave() -> void {
  close(socket_fd_);
  socket_fd_ = -1;
}

auto McastSocket::sendAndRecv() noexcept -> bool {
  // Read data and dispatch callbacks if data is available - non blocking
  const ssize_t n_rcv =
      recv(socket_fd_, inbound_data_.data() + next_recv_valid_index_,
           McastBufferSize - next_recv_valid_index_, MSG_DONTWAIT);
  if (n_rcv > 0) {
    next_recv_valid_index_ += n_rcv;
    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % read socket:% len:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_, socket_fd_, next_recv_valid_index_);
    recv_callback_(this);
  }

  // Publish market data in the send buffer to the multicast stream.
  if (next_send_valid_index_ > 0) {
    ssize_t n = ::send(socket_fd_, outbound_data_.data(),
                       next_send_valid_index_, MSG_DONTWAIT | MSG_NOSIGNAL);
    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_, socket_fd_, n);
  }
  next_send_valid_index_ = 0;

  return (n_rcv > 0);
}

// Copy data to send buffers - does not send them out yet.
auto McastSocket::send(const void* data, size_t len) noexcept -> void {
  memcpy(outbound_data_.data() + next_send_valid_index_, data, len);
  next_send_valid_index_ += len;
  ASSERT(next_send_valid_index_ < McastBufferSize,
         "Mcast socket buffer filled up and sendAndRecv() not called.");
}
}  // namespace common