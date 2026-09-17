// OrderManager管理来自交易客户端的 TCP 连接：
// 是交易所服务器的 TCP
// 入口点，负责接受客户端连接、接收订单请求、验证序列号并将响应路由回交易客户端
// 主要职责：
// 1. TCPServer使用epoll监听新的客户端连接
// 2. OrderManagerClientRequest从 TCP 套接字读取消息
// 3. 验证每个客户端的序列号（通过cid_next_exp_seq_num_数组）
// 4. 将已验证的请求转发至FIFOSequencer订购系统。
// 5. OrderManagerClientResponse向 client回复消息

#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "../../common/logging.h"
#include "../../common/tcp_server.h"
#include "../../common/time_utils.h"
#include "FIFOSequencer.h"
#include "client_request.h"
#include "client_response.h"

namespace exchange {
class OrderManager {
 public:
  OrderManager(ClientRequestLFQueue* clientRequests,
               ClientResponseLFQueue* clientResponses, const std::string& iface,
               int port);
  ~OrderManager();

  void start();
  void stop();

  auto run() noexcept {  // 该函数在独立线程中持续运行，负责将来自交易系统的响应消息通过
                    // TCP 发送给客户端，并保证消息顺序正确
    common::GetCurrentTimeStr(time_str_);
    logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__, time_str_);

    while (run_) {
      tcpServer.poll();
      tcpServer.sendAndRecv();

      for (auto clientResponse = outgoingResponses->GetNextToRead();
           clientResponse;
           clientResponse = outgoingResponses->GetNextToRead()) {
        TTT_MEASURE(T5t_OrderManager_LFQueue_read, logger);

        auto& nextOutgoingSeqNum =
            cidNextOutgoingSeqNum[clientResponse->client_id_];
        common::GetCurrentTimeStr(time_str_);
        logger.log("%:% %() % Processing cid:% seq:% %\n", __FILE__, __LINE__,
                   __FUNCTION__, time_str_, clientResponse->client_id_,
                   nextOutgoingSeqNum, clientResponse->toString());

        ASSERT(cidTcpSocketMap[clientResponse->client_id_] != nullptr,
               "Dont have a TCPSocket for ClientId:" +
                   std::to_string(clientResponse->client_id_));

        START_MEASURE(Exchange_TCPSocket_send);
        // 先发送序列号，再发送响应体
        cidTcpSocketMap[clientResponse->client_id_]->send(
            &nextOutgoingSeqNum, sizeof(nextOutgoingSeqNum));
        cidTcpSocketMap[clientResponse->client_id_]->send(
            clientResponse, sizeof(MatchingEngineClientResponse));
        END_MEASURE(Exchange_TCPSocket_send, logger);

        outgoingResponses->UpdateReadIndex();
        TTT_MEASURE(T6t_OrderManager_TCP_write, logger);

        ++nextOutgoingSeqNum;
      }
    }
  }

  // 回调函数：TCP服务器接收到数据时调用
  // 功能：从TCP套接字读取客户端请求，验证序列号和客户端身份，然后将有效请求转发到FIFOSequencer
  // 参数：
  //   - socket: 接收到数据的TCP套接字
  //   - rxTime: 接收数据的时间戳（纳秒）
  auto recvCallback(common::TCPSocket* socket, common::Nanos rxTime) noexcept {
    TTT_MEASURE(T1_OrderManager, logger);
    common::GetCurrentTimeStr(time_str_);
    logger.log("%:% %() % Received socket:% len:% rx:%\n", __FILE__, __LINE__,
               __FUNCTION__, time_str_, socket->socket_fd_,
               socket->next_recv_valid_index_, rxTime);

    // 检查接收缓冲区是否至少包含一个完整的请求（OrderManagerClientRequest结构体）
    if (socket->next_recv_valid_index_ >= sizeof(OrderManagerClientRequest)) {
      size_t i = 0;
      // 遍历接收缓冲区中的所有完整请求
      for (; i + sizeof(OrderManagerClientRequest) <=
             socket->next_recv_valid_index_;
           i += sizeof(OrderManagerClientRequest)) {
        auto request = reinterpret_cast<const OrderManagerClientRequest*>(
            socket->inbound_data_.data() + i);
        common::GetCurrentTimeStr(time_str_);
        logger.log("%:% %() % Received %\n", __FILE__, __LINE__, __FUNCTION__,
                   time_str_, request->toString());

        // 检查是否是来自该客户端的第一条消息
        // 如果是，则记录该客户端ID对应的TCP套接字
        if (UNLIKELY(cidTcpSocketMap[request->matching_engine_client_request
                                         .clientId_] ==
                     nullptr)) {  // first message from this ClientId.
          cidTcpSocketMap[request->matching_engine_client_request.clientId_] =
              socket;
        }

        // 验证客户端身份：检查请求是否来自该客户端绑定的套接字
        // 防止客户端使用错误的连接发送请求（安全检查）
        if (cidTcpSocketMap[request->matching_engine_client_request
                                .clientId_] !=
            socket) {  // TODO - change this to send a reject back to the
                       // client.
          common::GetCurrentTimeStr(time_str_);
          logger.log(
              "%:% %() % Received ClientRequest from ClientId:% on different "
              "socket:% expected:%\n",
              __FILE__, __LINE__, __FUNCTION__, time_str_,
              request->matching_engine_client_request.clientId_,
              socket->socket_fd_,
              cidTcpSocketMap[request->matching_engine_client_request.clientId_]
                  ->socket_fd_);
          continue;
        }

        // 获取该客户端期望的下一个序列号
        auto& nextExpSeqNum =
            cidNextExpSeqNum[request->matching_engine_client_request.clientId_];
        // 验证序列号：确保请求按顺序到达
        if (request->seqNum != nextExpSeqNum) {  // TODO - change this to send a
                                                 // reject back to the client.
          common::GetCurrentTimeStr(time_str_);
          logger.log(
              "%:% %() % Incorrect sequence number. ClientId:% SeqNum "
              "expected:% received:%\n",
              __FILE__, __LINE__, __FUNCTION__, time_str_,
              request->matching_engine_client_request.clientId_, nextExpSeqNum,
              request->seqNum);
          continue;
        }

        // 序列号验证通过，递增期望的序列号
        ++nextExpSeqNum;

        // 将有效的客户端请求添加到FIFOSequencer进行排序和处理
        START_MEASURE(Exchange_FIFOSequencer_addClientRequest);
        fifoSequencer.addClientRequest(rxTime,
                                       request->matching_engine_client_request);
        END_MEASURE(Exchange_FIFOSequencer_addClientRequest, logger);
      }

      // 将未处理的剩余数据移动到缓冲区开头，并更新有效数据长度
      memcpy(socket->inbound_data_.data(), socket->inbound_data_.data() + i,
             socket->next_recv_valid_index_ - i);
      socket->next_recv_valid_index_ -= i;
    }
  }

  auto recvFinishedCallback() noexcept {
    START_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish);
    fifoSequencer.sequenceAndPublish();
    END_MEASURE(Exchange_FIFOSequencer_sequenceAndPublish, logger);
  }

  OrderManager() = delete;
  OrderManager(const OrderManager&) = delete;
  OrderManager(const OrderManager&&) = delete;
  OrderManager& operator=(const OrderManager&) = delete;
  OrderManager& operator=(const OrderManager&&) = delete;

 private:
  const std::string iface;  // 绑定接口
  const int port;           // 监听端口

  ClientResponseLFQueue* outgoingResponses = nullptr;

  volatile bool run_ = false;

  std::string time_str_;
  common::Logger logger;

  // 记录下一个即将发出的序列号
  std::array<size_t, common::kMaxNumClients> cidNextOutgoingSeqNum;

  // 记录下一个期望收到的序列号
  std::array<size_t, common::kMaxNumClients> cidNextExpSeqNum;

  // // 客户端 socket 映射
  std::array<common::TCPSocket*, common::kMaxNumClients> cidTcpSocketMap;

  common::TCPServer tcpServer;

  FIFOSequencer fifoSequencer;  // incomingRequest
};
}  // namespace exchange
