#ifndef PROTO_IO_HPP
#define PROTO_IO_HPP

#include <vector>

#include <boost/asio.hpp>
#include <google/protobuf/message.h>


namespace proto_io {
    
boost::asio::awaitable<void> send_msg
    (
    boost::asio::ip::tcp::socket &  tcp_socket,  
    const google::protobuf::Message & 
                                    msg
    );

template<typename T>
boost::asio::awaitable<void> receive_msg
    (
    boost::asio::ip::tcp::socket & 
                tcp_socket,
    T &         msg
    )
{
    uint32_t network_msg_sz = 0;
    co_await boost::asio::async_read
        (
        tcp_socket,
        boost::asio::buffer(&network_msg_sz, sizeof(network_msg_sz)),
        boost::asio::use_awaitable
        );
    
    uint32_t host_msg_sz = ntohl(network_msg_sz);

    std::vector<char> msg_buffer(host_msg_sz);
    co_await boost::asio::async_read
        (
        tcp_socket,
        boost::asio::buffer(msg_buffer),
        boost::asio::use_awaitable
        );

    msg.ParseFromArray(msg_buffer.data(), host_msg_sz);

}   /* proto_io::receive() */

}

#endif /* PROTO_IO_HPP */

