#include "tcp_server.h"

#include <sys/socket.h>

#include <string>

#include "macros.h"
#include "socket_utils.h"
#include "tcp_socket.h"
#include "time_utils.h"

namespace common {
auto TCPServer::addToEpollList(TCPSocket* socket) {
  epoll_event ev{EPOLLET | EPOLLIN, {reinterpret_cast<void*>(socket)}};
  // 设置边缘触发(EPOLLET)和监听可读事件(EPOLLIN)
  // 将 socket->socket_fd_ 添加到 epoll_fd_ 的监听列表中
  return !epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket->socket_fd_, &ev);
}

auto TCPServer::listen(const std::string& iface, int port) -> void {
  epoll_fd_ = epoll_create(1);
  ASSERT(epoll_fd_ >= 0,
         "epoll_create() failed error:" + std::string(std::strerror(errno)));

  ASSERT(listener_socket_.connect("", iface, port, true) >= 0,
         "Listener socket failed to connect. iface:" + iface +
             " port:" + std::to_string(port) +
             " error:" + std::string(std::strerror(errno)));

  ASSERT(addToEpollList(&listener_socket_),
         "epoll_ctl() failed. error:" + std::string(std::strerror(errno)));
}

auto TCPServer::sendAndRecv() noexcept -> void {
  auto recv = false;
  std::for_each(recv_sockets_.begin(), recv_sockets_.end(),
                [&](auto socket) { recv |= socket->sendAndRecv(); });

  if (recv) {
    recv_finish_callback_();
  }
}

auto TCPServer::poll() noexcept -> void {
  // epoll 仅注册监听 socket，最多只有一个就绪事件
  const int n = epoll_wait(epoll_fd_, events_, 1, 0);
  bool haveNewConnection = false;

  for (int i = 0; i < n; i++) {
    const auto& event = events_[i];

    // EPOLLIN 即监听 socket 上有新连接到达
    if (event.events & EPOLLIN) {
      GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__,
                  __FUNCTION__, time_str_, listener_socket_.socket_fd_);
      haveNewConnection = true;
    }

    if (event.events & (EPOLLERR | EPOLLHUP)) {
      GetCurrentTimeStr(time_str_);
      logger_.log("%:% %() % EPOLLERR|EPOLLHUP listener_socket:%\n",
                  __FILE__, __LINE__, __FUNCTION__, time_str_,
                  listener_socket_.socket_fd_);
    }
  }

  // accept a new connection, create a TCPSocket and add it to our containers
  while (haveNewConnection) {
    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_);
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(listener_socket_.socket_fd_, (sockaddr*)&addr, &addr_len);
    if (fd < 0) {
      break;
    }
    ASSERT(setNonBlocking(fd) && disableNagle(fd),
           "setNonBlocking() or disableNagle() failed on socket:" +
               std::to_string(fd));

    GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % accepted socket:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_, fd);

    auto socket = new TCPSocket(logger_);
    socket->setSocketFd(fd);
    socket->setRecvback(recv_callback_);

    if (std::find(recv_sockets_.begin(), recv_sockets_.end(), socket) ==
        recv_sockets_.end()) {
      recv_sockets_.push_back(socket);
    }
  }
}

}  // namespace common
