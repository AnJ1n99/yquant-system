#pragma once

#include <sys/epoll.h>

#include <functional>
#include <vector>

#include "tcp_socket.h"
#include "time_utils.h"

namespace common {

class TCPServer {
 public:
  explicit TCPServer(Logger& logger)
      : logger_(logger), listener_socket(logger) {}

  // start listening for connections on the provided interface and port
  auto listen(const std::string& iface, int port) -> void;

  // check for new connections and or dead connections and update container that
  // track the sockets
  auto poll() noexcept -> void;

  // pubilsh outgoing data to the send buffers and incoming data from the recive
  // buffer
  auto sendAndRecv() noexcept -> void;

 private:
  // add and remove socket file descriptors to and from the EPOLL list
  auto addToEpollList(TCPSocket* socket);

 public:
  // Socket on which this server is listening for new connections on.
  int epoll_fd = -1;
  TCPSocket listener_socket;

  epoll_event events_[1024];

  // Collection of all sockets, sockets for incoming data, sockets for outgoing
  // data and dead connections.
  std::vector<TCPSocket*> recvSockets, sendSockets;

  // todo: use 函数指针
  std::function<void(TCPSocket* s, Nanos nx_time)> recvCallback = nullptr;

  std::function<void()> recvFinishCallback = nullptr;

  std::string time_str_;
  Logger& logger_;
};
}  // namespace common
