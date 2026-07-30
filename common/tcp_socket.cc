#include "tcp_socket.h"

#include <sys/socket.h>

#include <cstddef>
#include <cstring>

#include "socket_utils.h"
#include "time_utils.h"

namespace common {
// 既用于客户端“连接”，也用于服务端“监听”
auto TCPSocket::connect(const std::string& ip, const std::string& iface,
                        int port, bool isListening) -> int {
  // Note that needs_so_timestamp=true for FIFOSequencer.
  const SocketCfg socketCFG{ip, iface, port, false, isListening, true};
  socket_fd = createSocket(logger_, socketCFG);

  socket_attrib.sin_addr.s_addr = INADDR_ANY;
  socket_attrib.sin_port = htons(port);
  socket_attrib.sin_family = AF_INET;

  return socket_fd;
}

// 执行实际的 I/O 操作
auto TCPSocket::sendAndRecv() noexcept -> bool {
  // 目的是获取内核在收到数据包时记录的硬件/内核时间戳
  char ctrl[CMSG_SPACE(sizeof(struct timeval))];
  auto cmsg = reinterpret_cast<struct cmsghdr*>(&ctrl);

  // struct iovec 是用于分散/聚集 I/O（scatter/gather I/O）的重要数据结构，
  // 允许在一次系统调用中读写多个不连续的内存缓冲区 允许未来扩展为多段缓冲区
  iovec iov{inbound_data_.data() + nextRevVaildIndex_,
            TCPBufferSize - nextRevVaildIndex_};
  msghdr msg{&socket_attrib, sizeof(socket_attrib), &iov, 1,
             ctrl,           sizeof(ctrl),          0};

  // 非阻塞调用，读取可用数据
  const auto readSize = recvmsg(socket_fd, &msg, MSG_DONTWAIT);

  if (readSize > 0) {
    nextRevVaildIndex_ += readSize;
    Nanos kernelTime = 0;
    timeval timeKernel;
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMP &&
        cmsg->cmsg_len == CMSG_LEN(sizeof(timeKernel))) {
      memcpy(&timeKernel, CMSG_DATA(cmsg), sizeof(timeKernel));
      kernelTime = timeKernel.tv_sec * NANOS_TO_SECS +
                   timeKernel.tv_usec * NANOS_TO_MICROS;
    }

    const auto userTime = GetCurrentNanos();

    // 据包到达网卡到代码开始处理它”之间的软件处理延迟-> userTime - kernelTime
    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % read socket:% len:% utime:% ktime:% diff:%\n",
                __FILE__, __LINE__, __FUNCTION__, time_str_, socket_fd,
                nextRevVaildIndex_, userTime, kernelTime,
                (userTime - kernelTime));

    recv_callback(this, kernelTime);
  }
  if (nextSendVaildIndex_ > 0) {
    // 非阻塞调用，发送数据
    const auto n = ::send(socket_fd, outbound_data_.data(), nextSendVaildIndex_,
                          MSG_DONTWAIT | MSG_NOSIGNAL);
    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % send socket:% len:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_, socket_fd, n);
  }

  nextSendVaildIndex_ = 0;

  return (readSize > 0);
}

void TCPSocket::send(const void* data, size_t len) noexcept {
  memcpy(outbound_data_.data() + nextSendVaildIndex_, data, len);
  nextSendVaildIndex_ += len;
}
}  // namespace common