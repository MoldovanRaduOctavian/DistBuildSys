#include <boost/asio.hpp>

#include "resource_listener.hpp"

int main() {
    
    boost::asio::io_context io_ctx;
    ResourceListener resource_listener{io_ctx, 10458};

    io_ctx.run();
    return 0;

}
