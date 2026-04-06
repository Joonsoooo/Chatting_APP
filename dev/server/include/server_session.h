#pragma once
#include "def.h"

class server_session
{
public:
    server_session();
    ~server_session();

    bool init();
    bool start_server();
    void accept_loop();
    void handle_client(SOCKET sock);
    void broadcast_message(MESSAGE_TYPE type, const string& msg, SOCKET sender_sock = INVALID_SOCKET);
    size_t client_count();
    SOCKET find_client_by_nickname(const string& nickname);
    bool nickname_exists(const string& nickname, SOCKET exclude_sock = INVALID_SOCKET);
    string build_user_list();
    void remove_client(SOCKET sock);
    void close();

private:
    SOCKET m_serv_sock;
    map<SOCKET, USER_INFO> m_client_info;
    mutex m_client_lock;
    vector<thread> m_client_threads;
    mutex m_thread_lock;
    atomic<bool> m_running;
    atomic<bool> m_wsa_ready;
};
