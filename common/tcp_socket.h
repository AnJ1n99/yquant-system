#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <vector>
#include <netinet/in.h>

#include "logging.h"
#include "time_utils.h"

namespace Common {
	// size of our send and receive buffers in bytes.
	constexpr size_t TCPBufferSize = 64 * 1024 * 1024; // 64MB

	class TCPSocket {
	public:
		explicit TCPSocket(Logger &logger) : logger_(logger) {
		    outbound_data_.resize(TCPBufferSize);
			inbound_data_.resize(TCPBufferSize);
		}

		// Creat TCPSocket with provide attribute to either listen-on / connect-to
		auto connect(const std::string &ip, const std::string &iface, int port, bool isListening) -> int;

		// Called to publish outgoing data from the buffers as well as check for and callback if data is avaiable in the read buffers
		auto sendAndRecv() noexcept -> bool ;

		// write outgoing data to the send buffers
		auto send(const void* data, size_t len) noexcept;

		auto getSocketFd() const -> int { return socket_fd; }
		auto setSocketFd(int fd) -> void { socket_fd = fd; }
		void setRecvback(std::function<void(TCPSocket* s, Nanos rx_time)> callback) { recv_callback = callback; }

		// Deleted default, copy & move constructors and assignment-operators.
		TCPSocket(const TCPSocket &other) = delete;
		TCPSocket &operator=(const TCPSocket &other) = delete;

		TCPSocket(TCPSocket &&other) = delete;
		TCPSocket &operator=(TCPSocket &&other) = delete;

	private:
		// File descriptor for the socket
		int socket_fd = -1;

		// Send and receive buffers and trackers for read/write indices
		std::vector<uint8_t> outbound_data_;
		size_t nextSendVaildIndex_ {0};
		std::vector<uint8_t> inbound_data_;
		size_t nextRevVaildIndex_ {0};

		// Socket attributes
		struct sockaddr_in socket_attrib {};
		
		// Function wrapper to callback when there is data to be processed.
		std::function<void(TCPSocket* s, Nanos rx_time)> recv_callback = nullptr;

		std::string timeStr_;
		Logger &logger_; // 引用需要在初始化函数构造
	};
}
