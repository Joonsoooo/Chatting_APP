#include "server_session.h"

server_session::server_session()
    : m_serv_sock(INVALID_SOCKET), m_running(false), m_wsa_ready(false)
{
}

server_session::~server_session()
{
    close();
}

bool server_session::init()
{
    WSADATA wsa_data;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        print_wsa_error("WSAStartup() error!");

        return false;
    }

    m_wsa_ready = true;

    m_serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if (m_serv_sock == INVALID_SOCKET)
    {
        print_wsa_error("socket() error!");

        WSACleanup();

        m_wsa_ready = false;

        return false;
    }

    return true;
}

bool server_session::start_server()
{
    SOCKADDR_IN serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9000);

    if (bind(m_serv_sock, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR)
    {
        print_wsa_error("bind() error!");
        
        return false;
    }

    if (listen(m_serv_sock, 5) == SOCKET_ERROR)
    {
        print_wsa_error("listen() error!");

        return false;
    }

    m_running = true;

    cout << "[Server] started on port 9000\n";

    return true;
}

void server_session::accept_loop()
{
    while (m_running)
    {
        SOCKADDR_IN clnt_addr;
        int sz_clnt_addr = sizeof(clnt_addr);

        SOCKET clnt_sock = accept(m_serv_sock, (SOCKADDR*)&clnt_addr, &sz_clnt_addr);
        if (clnt_sock == INVALID_SOCKET)
        {
            if (m_running)
            {
                print_wsa_error("accept() error");
            }

            continue;
        }

        USER_INFO user;
        user.sock = clnt_sock;
        user.nickname = "anonymous";

        {
            lock_guard<mutex> lock(m_client_lock);
            m_client_info[clnt_sock] = user;
        }

        cout << "[Server] client connected. sock=" << clnt_sock << "\n";

        thread client_thread(&server_session::handle_client, this, clnt_sock);

        client_thread.detach();
    }
}

void server_session::handle_client(SOCKET sock)
{
    MESSAGE_TYPE type = MESSAGE_TYPE::SYSTEM;

    string payload;

    if (!recv_packet(sock, type, payload) || type != MESSAGE_TYPE::NICKNAME)
    {
        remove_client(sock);

        closesocket(sock);

        return;
    }

    string nickname = payload;

    if (nickname.empty())
    {
        nickname = "anonymous";
    }

    {
        lock_guard<mutex> lock(m_client_lock);

        if (m_client_info.find(sock) != m_client_info.end())
        {
            m_client_info[sock].nickname = nickname;
        }
    }

    {
        string enter_msg = "[Server] " + nickname + " joined the chat.";
        cout << enter_msg << "\n";

        broadcast_message(MESSAGE_TYPE::SYSTEM, enter_msg, INVALID_SOCKET);
    }

    while (m_running)
    {
        if (!recv_packet(sock, type, payload))
        {
            break;
        }

        if (type != MESSAGE_TYPE::CHAT)
        {
            continue;
        }

        string chat_msg = nickname + " : " + payload;

        cout << chat_msg << "\n";
        broadcast_message(MESSAGE_TYPE::CHAT, chat_msg, sock);
    }

    {
        string leave_msg = "[Server] " + nickname + " left the chat.";
        cout << leave_msg << "\n";

        broadcast_message(MESSAGE_TYPE::SYSTEM, leave_msg, INVALID_SOCKET);
    }

    remove_client(sock);

    closesocket(sock);
}

void server_session::broadcast_message(MESSAGE_TYPE type, const string& msg, SOCKET sender_sock)
{
    vector<SOCKET> sockets;

    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            SOCKET target_sock = it->second.sock;
            if (target_sock != INVALID_SOCKET)
            {
                sockets.push_back(target_sock);
            }
        }
    }

    for (size_t i = 0; i < sockets.size(); ++i)
    {
        SOCKET target_sock = sockets[i];

        if (!send_packet(target_sock, type, msg))
        {
            cerr << "[Server] send() error. sock=" << target_sock;
            if (sender_sock != INVALID_SOCKET && target_sock == sender_sock)
            {
                cerr << " (sender)";
            }
            cerr << "\n";
            remove_client(target_sock);
            closesocket(target_sock);
        }
    }
}

void server_session::remove_client(SOCKET sock)
{
    lock_guard<mutex> lock(m_client_lock);

    map<SOCKET, USER_INFO>::iterator it = m_client_info.find(sock);
    if (it != m_client_info.end())
    {
        m_client_info.erase(it);
    }
}

void server_session::close()
{
    m_running = false;

    {
        lock_guard<mutex> lock(m_client_lock);

        map<SOCKET, USER_INFO>::iterator it = m_client_info.begin();
        for (it; it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                shutdown(it->second.sock, SD_BOTH);
                closesocket(it->second.sock);
            }
        }

        m_client_info.clear();
    }

    if (m_serv_sock != INVALID_SOCKET)
    {
        shutdown(m_serv_sock, SD_BOTH);
        closesocket(m_serv_sock);
        m_serv_sock = INVALID_SOCKET;
    }

    if (m_wsa_ready)
    {
        WSACleanup();
        m_wsa_ready = false;
    }
}
