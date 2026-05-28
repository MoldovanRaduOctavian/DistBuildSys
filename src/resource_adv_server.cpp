#include <boost/asio.hpp>

#include "resource_broadcaster.hpp"

int main() {
    boost::asio::io_context io_ctx;
    ResourceBroadcaster resource_broadcaster{
        io_ctx,
        10458,
        663,
        "192.168.1.14"
    };
    
    io_ctx.run();
    return 0;
}

