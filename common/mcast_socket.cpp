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
  socketFd = createSocket(logger, CFG);
  return socketFd;
}

auto McastSocket::join(const std::string& ip) -> bool {
  return common::join(socketFd, ip);
}

// Remove / Leave membership / subscription to a multicast stream.
// 关闭套接字会自动移除所有多播成员关系。
auto McastSocket::leave() -> void {
  close(socketFd);
  socketFd = -1;
}

auto McastSocket::sendAndRecv() noexcept -> bool {
  // Read data and dispatch callbacks if data is available - non blocking
  const ssize_t n_rcv = recv(socketFd, inboundData.data() + nextRcvValidIndex,
                             McastBufferSize - nextRcvValidIndex, MSG_DONTWAIT);
  if (n_rcv > 0) {
    nextRcvValidIndex += n_rcv;
    getCurrentTimeStr(timeStr);
    logger.log("%:% %() % read socket:% len:%\n", __FILE__, __LINE__,
               __FUNCTION__, timeStr, socketFd, nextRcvValidIndex);
    recvCallback(this);
  }

  // Publish market data in the send buffer to the multicast stream.
  if (nextSendValidIndex > 0) {
    ssize_t n = ::send(socketFd, outboundData.data(), nextSendValidIndex,
                       MSG_DONTWAIT | MSG_NOSIGNAL);
    getCurrentTimeStr(timeStr);
    logger.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__,
               __FUNCTION__, timeStr, socketFd, n);
  }
  nextSendValidIndex = 0;

  return (n_rcv > 0);
}

// Copy data to send buffers - does not send them out yet.
auto McastSocket::send(const void* data, size_t len) noexcept -> void {
  memcpy(outboundData.data() + nextSendValidIndex, data, len);
  nextSendValidIndex += len;
  ASSERT(nextSendValidIndex < McastBufferSize,
         "Mcast socket buffer filled up and sendAndRecv() not called.");
}
}  // namespace common