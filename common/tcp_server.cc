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
  return !epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket->socket_fd, &ev);
}

auto TCPServer::listen(const std::string& iface, int port) -> void {
  epoll_fd = epoll_create(1);
  ASSERT(epoll_fd >= 0,
         "epoll_create() failed error:" + std::string(std::strerror(errno)));

  ASSERT(listener_socket.connect("", iface, port, true) >= 0,
         "Listener socket failed to connect. iface:" + iface +
             " port:" + std::to_string(port) +
             " error:" + std::string(std::strerror(errno)));

  ASSERT(addToEpollList(&listener_socket),
         "epoll_ctl() failed. error:" + std::string(std::strerror(errno)));
}

auto TCPServer::sendAndRecv() noexcept -> void {
  auto recv = false;
  std::for_each(recvSockets.begin(), recvSockets.end(),
                [&](auto socket) { recv |= socket->sendAndRecv(); });

  if (recv) {
    recvFinishCallback();
  }

  std::for_each(sendSockets.begin(), sendSockets.end(),
                [](auto socket) { socket->sendAndRecv(); });
}

auto TCPServer::poll() noexcept -> void {
  const int maxEvents = 1 + recvSockets.size() + sendSockets.size();
  const int n = epoll_wait(epoll_fd, events_, maxEvents, 0);
  bool haveNewConnection = false;

  for (int i = 0; i < n; i++) {
    const auto& event = events_[i];
    auto socket = reinterpret_cast<TCPSocket*>(event.data.ptr);

    // check for new connections
    if (event.events & EPOLLIN) {
      if (socket == &listener_socket) {
        getCurrentTimeStr(time_str_);
        logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__,
                    __FUNCTION__, time_str_, socket->socket_fd_);
        haveNewConnection = true;
        continue;
      }
      getCurrentTimeStr(time_str_);
      logger_.log("%:% %() % EPOLLIN socket:%\n", __FILE__, __LINE__,
                  __FUNCTION__, time_str_, socket->socket_fd_);
      // 没有此 socket
      if (std::find(recvSockets.begin(), recvSockets.end(), socket) ==
          recvSockets.end()) {
        recvSockets.push_back(socket);
      }
    }

    if (event.events & EPOLLOUT) {
      getCurrentTimeStr(time_str_);
      logger_.log("%:% %() % EPOLLOUT socket:%\n", __FILE__, __LINE__,
                  __FUNCTION__, time_str_, socket->socket_fd_);
      // 没有此 socket
      if (std::find(sendSockets.begin(), sendSockets.end(), socket) ==
          sendSockets.end()) {
        sendSockets.push_back(socket);
      }
    }

    if (event.events & (EPOLLERR | EPOLLHUP)) {
      getCurrentTimeStr(time_str_);
      logger_.log("%:% %() % EPOLLERR|EPOLLHUP socket:%\n", __FILE__, __LINE__,
                  __FUNCTION__, time_str_, socket->socket_fd_);
      if (std::find(recvSockets.begin(), recvSockets.end(), socket) ==
          recvSockets.end()) {
        recvSockets.push_back(socket);
      }
    }
  }

  // accept a new connection, create a TCPSocket and add it to our containers
  while (haveNewConnection) {
    getCurrentTimeStr(time_str_);
    logger_.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_);
    sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int fd = accept(listener_socket.socket_fd, (sockaddr*)&addr, &addr_len);
    if (fd < 0) {
      break;
    }
    ASSERT(setNonBlocking(fd) && disableNagle(fd),
           "setNonBlocking() or disableNagle() failed on socket:" +
               std::to_string(fd));

    getCurrentTimeStr(time_str_);
    logger_.log("%:% %() % accepted socket:%\n", __FILE__, __LINE__,
                __FUNCTION__, time_str_, fd);

    auto socket = new TCPSocket(logger_);
    socket->setSocketFd(fd);
    socket->setRecvback(recvCallback);
    ASSERT(addToEpollList(socket),
           "Unable to add socket. error:" + std::string(std::strerror(errno)));

    if (std::find(recvSockets.begin(), recvSockets.end(), socket) ==
        recvSockets.end()) {
      recvSockets.push_back(socket);
    }
  }
}

}  // namespace common
