#include "order_manager.h"

#include <chrono>

#include "FIFOSequencer.h"

namespace Exchange {

OrderManager::OrderManager(ClientRequestLFQueue* clientRequests,
                           ClientResponseLFQueue* clientResponses,
                           const std::string& iface, int port)
    : iface(iface),
      port(port),
      outgoingResponses(clientResponses),
      logger("Exchange_Order_Manager.log"),
      tcpServer(logger),
      fifoSequencer(clientRequests, &logger) {
  cidNextOutgoingSeqNum.fill(1);
  cidNextExpSeqNum.fill(1);
  cidTcpSocketMap.fill(nullptr);

  tcpServer.recv_callback_ = [this](auto socket, auto rx_time) {
    recvCallback(socket, rx_time);
  };
  tcpServer.recvFinishedCallback_ = [this]() { recvFinishedCallback(); };
}

OrderManager::~OrderManager() {
  stop();

  std::this_thread::sleep_for(std::chrono::seconds(1));
}

auto OrderManager::start() -> void {
  run_ = true;

  tcpServer.listen(iface, port);

  ASSERT(Common::createAndStartThread(1, "Exchange/OrderServer",
                                      [this]() { run(); }) != nullptr,
         "Failed to start OrderServer thread.");
}

auto OrderManager::stop() -> void { run_ = false; }

}  // namespace Exchange
