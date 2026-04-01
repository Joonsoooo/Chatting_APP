#pragma once
#include "def.h"

class server_session
{
public:
    server_session();
    ~server_session();

    void init();
    void start_server();
    void accept_loop();
    void handle_client(SOCKET sock);
    void broadcast_message(const string& msg, SOCKET sender_sock);
    void remove_client(SOCKET sock);
    void close();

private:
    SOCKET m_serv_sock;
    map<SOCKET, USER_INFO> m_client_info;
    mutex m_client_lock;
};