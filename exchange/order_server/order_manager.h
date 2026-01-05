// OrderServer管理来自交易客户端的 TCP 连接：

// 主要职责：

// TCPServer使用epoll监听新的客户端连接
// OMClientRequest从 TCP 套接字读取消息
// 验证每个客户端的序列号（通过cid_next_exp_seq_num_数组）
// 将已验证的请求转发至FIFOSequencer订购系统。
// OMClientResponse向客户回复消息

#pragma once

#include <string>
namespace Exchange {
    class OrderManager {
    public:
    private:
        const std::string iface_;
        const int port;


    };
}
