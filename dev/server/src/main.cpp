#include "server_session.h"

int main()
{
    server_session server;

    if (!server.init())
    {
        return 1;
    }

    if (!server.start_server())
    {
        return 1;
    }

    server.accept_loop();

    return 0;
}
