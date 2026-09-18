#pragma once

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>

#include "common/thread_utils.h"
#include "common/macros.h"
#include "common/tcp_socket.h"

#include "exchange/order_manager/client_request.h"
#include "exchange/order_manager/client_response.h"

namespace trading {
  using namespace common;
  class OrderGateway {
  public:
    OrderGateway(ClientId client_id,
                 exchange::ClientRequestLFQueue *client_requests,
                 exchange::ClientResponseLFQueue *client_responses,
                 std::string ip, const std::string &iface, int port);

    ~OrderGateway() {
      stop();

      if (tcp_socket_.socket_fd_ >= 0) {
        close(tcp_socket_.socket_fd_);
        tcp_socket_.setSocketFd(-1);
      }
    }

    /// Start and stop the order gateway main thread.
    auto start() {
      if (thread_) return;
      run_ = true;
      ASSERT(tcp_socket_.connect(ip_, iface_, port_, false) >= 0,
             "Unable to connect to ip:" + ip_ + " port:" + std::to_string(port_) + " on iface:" + iface_ + " error:" + std::string(std::strerror(errno)));
      thread_.reset(common::createAndStartThread(-1, "trading/OrderGateway", [this]() { run(); }));
      ASSERT(thread_ != nullptr, "Failed to start OrderGateway thread.");
    }

    auto stop() -> void {
      run_ = false;
      if (thread_ && thread_->joinable()) thread_->join();
      thread_.reset();
    }

    /// Deleted default, copy & move constructors and assignment-operators.
    OrderGateway() = delete;

    OrderGateway(const OrderGateway &) = delete;

    OrderGateway(const OrderGateway &&) = delete;

    OrderGateway &operator=(const OrderGateway &) = delete;

    OrderGateway &operator=(const OrderGateway &&) = delete;

  private:
    const ClientId client_id_;

    /// exchange's order server's TCP server address.
    std::string ip_;
    const std::string iface_;
    const int port_ = 0;

    /// Lock free queue on which we consume client requests from the trade engine and forward them to the exchange's order server.
    exchange::ClientRequestLFQueue *outgoing_requests_ = nullptr;

    /// Lock free queue on which we write client responses which we read and processed from the exchange, to be consumed by the trade engine.
    exchange::ClientResponseLFQueue *incoming_responses_ = nullptr;

    std::atomic<bool> run_{false};
    std::unique_ptr<std::thread> thread_;

    std::string time_str_;
    Logger logger_;

    /// Sequence numbers to track the sequence number to set on outgoing client requests and expected on incoming client responses.
    size_t next_outgoing_seq_num_ = 1;
    size_t next_exp_seq_num_ = 1;

    /// TCP connection to the exchange's order server.
    common::TCPSocket tcp_socket_;

  private:
    /// Main thread loop - sends out client requests to the exchange and reads and dispatches incoming client responses.
    auto run() noexcept -> void;

    /// Callback when an incoming client response is read, we perform some checks and forward it to the lock free queue connected to the trade engine.
    auto recvCallback(TCPSocket *socket, Nanos rx_time) noexcept -> void;
  };
}
