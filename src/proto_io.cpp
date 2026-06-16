#include "proto_io.hpp"

namespace proto_io {

/*
boost::asio::awaitable<void> send_msg
    (
    boost::asio::ip::tcp::socket &  tcp_socket,  
    const google::protobuf::Message & 
                                    msg
    )
{
    std::string msg_payload;
    msg.SerializeToString(&msg_payload);

    uint32_t network_msg_sz = htonl(msg_payload.size());
    
    std::vector<boost::asio::const_buffer> msg_buffers
        {
        boost::asio::buffer(&network_msg_sz, sizeof(network_msg_sz)),
        boost::asio::buffer(msg_payload)
        };
    
    co_await boost::asio::async_write
        (
        tcp_socket, 
        msg_buffers, 
        boost::asio::use_awaitable
        );

}   
*/

boost::asio::awaitable<void> send_msg
    (
    boost::asio::ip::tcp::socket &      socket,
    const google::protobuf::Message &   msg
    )
{
    std::string payload;
    msg.SerializeToString(&payload);

    uint32_t sz = htonl(payload.size());
    auto buffer = std::make_shared<std::string>();
    buffer->resize(sizeof(uint32_t) + payload.size());

    memcpy(buffer->data(), &sz, sizeof(uint32_t));
    memcpy(buffer->data() + sizeof(uint32_t), payload.data(), payload.size());

    co_await boost::asio::async_write(
        socket,
        boost::asio::buffer(*buffer),
        boost::asio::use_awaitable
    );

}

}

