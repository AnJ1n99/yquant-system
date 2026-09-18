#include "order_gateway.h"

namespace trading {
  using namespace common;
  OrderGateway::OrderGateway(ClientId client_id,
                             exchange::ClientRequestLFQueue *client_requests,
                             exchange::ClientResponseLFQueue *client_responses,
                             std::string ip, const std::string &iface, int port)
      : client_id_(client_id), ip_(ip), iface_(iface), port_(port), outgoing_requests_(client_requests), incoming_responses_(client_responses),
      logger_("trading_order_gateway_" + std::to_string(client_id) + ".log"), tcp_socket_(logger_) {
    tcp_socket_.setRecvback([this](auto socket, auto rx_time) { recvCallback(socket, rx_time); });
  }

  /// Main thread loop - sends out client requests to the exchange and reads and dispatches incoming client responses.
  auto OrderGateway::run() noexcept -> void {
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, time_str_);
    while (run_ || outgoing_requests_->size()) {
      tcp_socket_.sendAndRecv();

      for(auto client_request = outgoing_requests_->GetNextToRead(); client_request; client_request = outgoing_requests_->GetNextToRead()) {
        TTT_MEASURE(T11_OrderGateway_LFQueue_read, logger_);

        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % Sending cid:% seq:% %\n", __FILE__, __LINE__, __FUNCTION__,
                    time_str_, client_id_, next_outgoing_seq_num_, client_request->toString());
        START_MEASURE(Trading_TCPSocket_send);
        tcp_socket_.send(&next_outgoing_seq_num_, sizeof(next_outgoing_seq_num_));
        tcp_socket_.send(client_request, sizeof(exchange::MatchingEngineClientRequest));
        END_MEASURE(Trading_TCPSocket_send, logger_);
        outgoing_requests_->UpdateReadIndex();
        TTT_MEASURE(T12_OrderGateway_TCP_write, logger_);

        next_outgoing_seq_num_++;
      }
    }
    tcp_socket_.sendAndRecv();
  }

  /// Callback when an incoming client response is read, we perform some checks and forward it to the lock free queue connected to the trade engine.
  auto OrderGateway::recvCallback(TCPSocket *socket, Nanos rx_time) noexcept -> void {
    TTT_MEASURE(T7t_OrderGateway_TCP_read, logger_);

    START_MEASURE(Trading_OrderGateway_recvCallback);
    common::GetCurrentTimeStr(time_str_);
    logger_.log("%:% %() % Received socket:% len:% %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, socket->socket_fd_, socket->next_recv_valid_index_, rx_time);

    if (socket->next_recv_valid_index_ >= sizeof(exchange::OrderManagerClientResponse)) {
      size_t i = 0;
      for (; i + sizeof(exchange::OrderManagerClientResponse) <= socket->next_recv_valid_index_; i += sizeof(exchange::OrderManagerClientResponse)) {
        exchange::OrderManagerClientResponse message{};
        std::memcpy(&message, socket->inbound_data_.data() + i, sizeof(message));
        const auto* response = &message;
        common::GetCurrentTimeStr(time_str_);
        logger_.log("%:% %() % Received %\n", __FILE__, __LINE__, __FUNCTION__, time_str_, response->matching_engine_client_response.toString());

        if(response->matching_engine_client_response.client_id_ != client_id_) { // this should never happen unless there is a bug at the exchange.
          common::GetCurrentTimeStr(time_str_);
          logger_.log("%:% %() % ERROR Incorrect client id. ClientId expected:% received:%.\n", __FILE__, __LINE__, __FUNCTION__,
                      time_str_, client_id_, response->matching_engine_client_response.client_id_);
          continue;
        }
        if(response->seqNum != next_exp_seq_num_) { // this should never happen since we use a reliable TCP protocol, unless there is a bug at the exchange.
          common::GetCurrentTimeStr(time_str_);
          logger_.log("%:% %() % ERROR Incorrect sequence number. ClientId:%. SeqNum expected:% received:%.\n", __FILE__, __LINE__, __FUNCTION__,
                      time_str_, client_id_, next_exp_seq_num_, response->seqNum);
          continue;
        }

        ++next_exp_seq_num_;

        auto* next_write = incoming_responses_->TryGetNextToWriteTo();
        while (!next_write && run_) {
          CPU_PAUSE();
          next_write = incoming_responses_->TryGetNextToWriteTo();
        }
        if (!next_write) continue;
        *next_write = response->matching_engine_client_response;
        incoming_responses_->UpdateWriteIndex();
        TTT_MEASURE(T8t_OrderGateway_LFQueue_write, logger_);
      }
      memmove(socket->inbound_data_.data(), socket->inbound_data_.data() + i, socket->next_recv_valid_index_ - i);
      socket->next_recv_valid_index_ -= i;
    }
    END_MEASURE(Trading_OrderGateway_recvCallback, logger_);
  }
}
