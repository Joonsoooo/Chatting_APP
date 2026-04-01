#include "server_session.h"

server_session::server_session()
    : m_serv_sock(INVALID_SOCKET)
{
}

server_session::~server_session()
{
    close();
}

void server_session::init()
{
    WSADATA wsa_data;

    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        print_err("WSAStartup() error!");
    }

    m_serv_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (m_serv_sock == INVALID_SOCKET)
    {
        print_err("socket() error!");
    }
}

void server_session::start_server()
{
    SOCKADDR_IN serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9000);

    if (bind(m_serv_sock, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR)
    {
        print_err("bind() error!");
    }

    if (listen(m_serv_sock, 5) == SOCKET_ERROR)
    {
        print_err("listen() error!");
    }

    cout << "[Server] started on port 9000\n";
}

void server_session::accept_loop()
{
    while (true)
    {
        SOCKADDR_IN clnt_addr;
        int sz_clnt_addr = sizeof(clnt_addr);

        SOCKET clnt_sock = accept(m_serv_sock, (SOCKADDR*)&clnt_addr, &sz_clnt_addr);
        if (clnt_sock == INVALID_SOCKET)
        {
            cerr << "[Server] accept() error\n";
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
    char buf[4097] = { 0 };

    int recv_size = recv(sock, buf, 4096, 0);
    if (recv_size == SOCKET_ERROR || recv_size == 0)
    {
        remove_client(sock);
        closesocket(sock);
        return;
    }

    buf[recv_size] = '\0';
    string nickname = buf;

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
        broadcast_message(enter_msg, INVALID_SOCKET);
    }

    while (true)
    {
        memset(buf, 0, sizeof(buf));

        recv_size = recv(sock, buf, 4096, 0);

        if (recv_size == SOCKET_ERROR)
        {
            cout << "[Server] recv() error. sock=" << sock << "\n";
            break;
        }
        else if (recv_size == 0)
        {
            cout << "[Server] client disconnected. sock=" << sock << "\n";
            break;
        }

        buf[recv_size] = '\0';

        string chat_msg = nickname + " : " + string(buf);

        cout << chat_msg << "\n";
        broadcast_message(chat_msg, sock);
    }

    {
        string leave_msg = "[Server] " + nickname + " left the chat.";
        cout << leave_msg << "\n";
        broadcast_message(leave_msg, INVALID_SOCKET);
    }

    remove_client(sock);
    closesocket(sock);
}

void server_session::broadcast_message(const string& msg, SOCKET sender_sock)
{
    lock_guard<mutex> lock(m_client_lock);

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        SOCKET target_sock = it->second.sock;

        if (target_sock == INVALID_SOCKET)
        {
            continue;
        }

        // 보낸 사람 제외하고 싶으면 이 조건 사용
        // if (target_sock == sender_sock) continue;

        int ret = send(target_sock, msg.c_str(), (int)msg.size(), 0);
        if (ret == SOCKET_ERROR)
        {
            cerr << "[Server] send() error. sock=" << target_sock << "\n";
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
    {
        lock_guard<mutex> lock(m_client_lock);

        map<SOCKET, USER_INFO>::iterator it = m_client_info.begin();
        for (it; it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                closesocket(it->second.sock);
            }
        }

        m_client_info.clear();
    }

    if (m_serv_sock != INVALID_SOCKET)
    {
        closesocket(m_serv_sock);
        m_serv_sock = INVALID_SOCKET;
    }

    WSACleanup();
}