#pragma once

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <string>

#include "logging.h"
#include "macros.h"
#include "time_utils.h"

namespace common {
struct SocketCfg {
  std::string ip_;
  std::string iface_;
  int port_{-1};
  bool isUdp_{false};
  bool isListening_{false};
  bool needsSoTimestamp_{false};

  auto toString() const {
    std::stringstream ss;
    ss << "SocketCFG is [ip: " << ip_ << " iface: " << iface_  // 网络接口名称
       << " port: " << port_ << " isUdp: " << isUdp_
       << " isListening: " << isListening_
       << " needsSoTimestamp: " << needsSoTimestamp_ << "]";
    return ss.str();
  }
};

// Maximum number of pending / unaccepted TCP connections
constexpr int MaxTCPServerBacklog = 1024;

// convert interface name "eth0" to ip "123.123.123.123"
inline auto getIfaceIP(const std::string& iface) -> std::string {
  char buf[NI_MAXHOST] = {'\0'};
  ifaddrs* ifaddr = nullptr;

  if (getifaddrs(&ifaddr) != -1) {
    for (ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
          iface == ifa->ifa_name) {
        getnameinfo(ifa->ifa_addr, sizeof(sockaddr), buf, NI_MAXHOST, nullptr,
                    0, NI_NUMERICHOST);
        break;
      }
    }
    freeifaddrs(ifaddr);
  }
  return buf;
}

// Sockets will not block on read,
// but instead return immediately if data is not available.
inline bool setNonBlocking(int fd) {
  const auto flags = fcntl(fd, F_GETFL, 0);
  if (flags & O_NONBLOCK)  // 避免冗余调用 system call
    return true;

  return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

// Disable Nagle's algorithm and associated delays.
/*
        纳格尔算法的影响：
        默认行为：缓冲小数据包，通过合并它们来减少网络开销。
        延迟成本：发送前需要等待更多数据或确认信息，这会引入延迟。
        交易影响：对于订单提交和市场数据而言，微秒级的延迟影响是不可接受的
*/
inline bool disableNagle(int fd) {
  int one = 1;
  return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != -1);
}

// Allow software receive timestamps on incoming packets.
inline auto setSOTimestamp(int fd) -> bool {
  int one = 1;
  return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP, &one, sizeof(one)) != -1);
}

// Add / Join membership / subscription to the multicast stream specified and on
// the interface specified.
inline bool join(int fd, const std::string& ip) {
  const ip_mreq mreq{{inet_addr(ip.c_str())}, {htonl(INADDR_ANY)}};
  return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) !=
          -1);
}

// Create a TCP / UDP socket to either connect to or listen for data on
// or listen for connections on the specified interface and IP:port information.
[[nodiscard]] inline int createSocket(Logger& logger,
                                      const SocketCfg& socketCFG) {
  std::string time_str;

  const auto ip =
      socketCFG.ip_.empty() ? getIfaceIP(socketCFG.iface_) : socketCFG.ip_;
  GetCurrentTimeStr(time_str);
  logger.log("%:% %() % cfg:%\n", __FILE__, __LINE__, __FUNCTION__, time_str,
             socketCFG.toString());

  const int inputFlags = (socketCFG.isListening_ ? AI_PASSIVE : 0) |
                         (AI_NUMERICHOST | AI_NUMERICSERV);
  const addrinfo hints{inputFlags,
                       AF_INET,
                       socketCFG.isUdp_ ? SOCK_DGRAM : SOCK_STREAM,
                       socketCFG.isUdp_ ? IPPROTO_UDP : IPPROTO_TCP,
                       0,
                       0,
                       nullptr,
                       nullptr};

  addrinfo* result = nullptr;
  // 将主机名和服务名转换为可用于套接字操作的地址结构
  const auto rc = getaddrinfo(
      ip.c_str(), std::to_string(socketCFG.port_).c_str(), &hints, &result);
  ASSERT(!rc, "getaddrinfo() failed error:" + std::string(gai_strerror(rc)) +
                  std::string(std::strerror(errno)));

  int socketFd = -1;
  int one = 1;
  for (addrinfo* rp = result; rp; rp = rp->ai_next) {
    ASSERT((socketFd =
                socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) != -1,
           "socket() failed. errno :" + std::string(strerror(errno)));

    ASSERT(setNonBlocking(socketFd),
           "setNonBlocking() failed. errno :" + std::string(strerror(errno)));

    if (!socketCFG.isUdp_) {  // disable Nagle for TCP sockets
      ASSERT(disableNagle(socketFd),
             "disableNagle() failed. errno :" + std::string(strerror(errno)));
    }

    if (!socketCFG.isListening_) {  // establish connection to specified address
      ASSERT(connect(socketFd, rp->ai_addr, rp->ai_addrlen) != -1,
             "connect() failed. errno :" + std::string(strerror(errno)));
    }

    if (socketCFG.isListening_) {
      ASSERT(setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &one,
                        sizeof(one)) == 0,
             "setsockopt() SO_REUSEADDR failed. errno :" +
                 std::string(strerror(errno)));
    }

    if (socketCFG.isListening_) {
      // bind to the specified port number
      const sockaddr_in addr{
          AF_INET, htons(socketCFG.port_), {htonl(INADDR_ANY)}, {}};
      ASSERT(bind(socketFd,
                  socketCFG.isUdp_ ? reinterpret_cast<const sockaddr*>(&addr)
                                   : rp->ai_addr,
                  sizeof(addr)) == 0,
             "bind() failed errno:%" + std::string(strerror(errno)));
    }

    if (!socketCFG.isUdp_ &&
        socketCFG.isListening_) {  // listen for incoming TCP connections.
      ASSERT(listen(socketFd, MaxTCPServerBacklog) == 0,
             "listen() failed. errno:" + std::string(strerror(errno)));
    }

    if (socketCFG.needsSoTimestamp_) {  // enable software receive timestamps.
      ASSERT(setSOTimestamp(socketFd),
             "setSOTimestamp() failed. errno:" + std::string(strerror(errno)));
    }
  }
  return socketFd;
}
}  // namespace common