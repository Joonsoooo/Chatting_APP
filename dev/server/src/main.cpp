#include "server_session.h"

int main()
{
    server_session server;

    server.init();
    server.start_server();
    server.accept_loop();

    return 0;
}