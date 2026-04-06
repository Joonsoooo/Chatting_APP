#include "server_session.h"

#include <chrono>
#include <iomanip>
#include <sstream>

static string current_timestamp()
{
    using namespace std::chrono;

    const system_clock::time_point now = system_clock::now();
    const time_t now_time = system_clock::to_time_t(now);
    tm local_tm = {};
    localtime_s(&local_tm, &now_time);

    ostringstream oss;
    oss << put_time(&local_tm, "%H:%M:%S");
    return oss.str();
}

static string timestamp_message(const string& message)
{
    return "[" + current_timestamp() + "] " + message;
}

static void log_server(const char* level, const string& message)
{
    ostream& stream = string(level) == "ERROR" ? cerr : cout;
    stream << "[" << current_timestamp() << "] "
           << "[" << level << "] "
           << message << "\n";
}

static bool starts_with_command(const string& payload, const string& command)
{
    return payload.rfind(command, 0) == 0;
}

static bool parse_whisper_command(const string& payload, string& target_nickname, string& message)
{
    size_t command_offset = string::npos;
    if (starts_with_command(payload, "/whisper "))
    {
        command_offset = 9;
    }
    else if (starts_with_command(payload, "/w "))
    {
        command_offset = 3;
    }
    else
    {
        return false;
    }

    const size_t first_space = payload.find(' ', command_offset);
    if (first_space == string::npos)
    {
        return false;
    }

    target_nickname = payload.substr(command_offset, first_space - command_offset);
    message = payload.substr(first_space + 1);
    return !target_nickname.empty() && !message.empty();
}

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

    log_server("INFO", "server started on port 9000");

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

        if (client_count() >= MAX_CONNECTED_CLIENTS)
        {
            const string limit_message = timestamp_message("server is full. max clients: " + to_string(MAX_CONNECTED_CLIENTS));
            log_server("ERROR", "connection rejected because server is full. sock=" + to_string(static_cast<long long>(clnt_sock)));
            send_packet(clnt_sock, MESSAGE_TYPE::SYSTEM_ERROR, limit_message);
            closesocket(clnt_sock);
            continue;
        }

        USER_INFO user;
        user.sock = clnt_sock;
        user.nickname = "anonymous";

        {
            lock_guard<mutex> lock(m_client_lock);
            m_client_info[clnt_sock] = user;
        }

        log_server("INFO", "client connected. sock=" + to_string(static_cast<long long>(clnt_sock)));

        lock_guard<mutex> thread_lock(m_thread_lock);
        m_client_threads.emplace_back(&server_session::handle_client, this, clnt_sock);
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

    if (!is_valid_nickname(nickname))
    {
        log_server("ERROR", "invalid nickname from sock=" + to_string(static_cast<long long>(sock)));

        send_packet(sock, MESSAGE_TYPE::NICKNAME_REJECTED, timestamp_message("invalid nickname"));

        remove_client(sock);

        closesocket(sock);

        return;
    }

    if (nickname_exists(nickname, sock))
    {
        log_server("ERROR", "duplicate nickname from sock=" + to_string(static_cast<long long>(sock)));

        send_packet(sock, MESSAGE_TYPE::NICKNAME_REJECTED, timestamp_message("nickname already in use"));

        remove_client(sock);

        closesocket(sock);

        return;
    }

    {
        lock_guard<mutex> lock(m_client_lock);

        if (m_client_info.find(sock) != m_client_info.end())
        {
            m_client_info[sock].nickname = nickname;
        }
    }

    if (!send_packet(sock, MESSAGE_TYPE::NICKNAME_ACCEPTED, timestamp_message("nickname accepted")))
    {
        log_server("ERROR", "failed to send nickname acceptance. sock=" + to_string(static_cast<long long>(sock)));

        remove_client(sock);

        closesocket(sock);

        return;
    }

    {
        const string enter_msg = timestamp_message(nickname + " joined the chat.");
        log_server("INFO", nickname + " joined the chat");
        broadcast_message(MESSAGE_TYPE::SYSTEM_JOIN, enter_msg, INVALID_SOCKET);
    }

    while (m_running)
    {
        if (!recv_packet(sock, type, payload))
        {
            break;
        }

        if (type != MESSAGE_TYPE::CHAT)
        {
            log_server("ERROR", "unexpected packet type from sock=" + to_string(static_cast<long long>(sock)));

            break;
        }

        if (!is_valid_chat_message(payload))
        {
            log_server("ERROR", "invalid chat payload from sock=" + to_string(static_cast<long long>(sock)));

            continue;
        }

        if (payload == "/list")
        {
            send_packet(sock, MESSAGE_TYPE::USER_LIST, timestamp_message(build_user_list()));
            continue;
        }

        if (payload == "/help")
        {
            send_packet(
                sock,
                MESSAGE_TYPE::SYSTEM_INFO,
                timestamp_message("commands: /help, /list, /name <new_nickname>, /whisper <nickname> <message>, /w <nickname> <message>"));
            continue;
        }

        if (starts_with_command(payload, "/name "))
        {
            const string new_nickname = payload.substr(6);

            if (!is_valid_nickname(new_nickname))
            {
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("invalid nickname"));
                continue;
            }

            if (nickname_exists(new_nickname, sock))
            {
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("nickname already in use"));
                continue;
            }

            const string old_nickname = nickname;
            {
                lock_guard<mutex> lock(m_client_lock);
                if (m_client_info.find(sock) != m_client_info.end())
                {
                    m_client_info[sock].nickname = new_nickname;
                }
            }

            nickname = new_nickname;
            log_server("INFO", old_nickname + " changed nickname to " + nickname);
            broadcast_message(
                MESSAGE_TYPE::NICKNAME_CHANGED,
                timestamp_message(old_nickname + " is now known as " + nickname),
                INVALID_SOCKET);
            continue;
        }

        string target_nickname;
        string whisper_message;
        if (parse_whisper_command(payload, target_nickname, whisper_message))
        {
            if (!is_valid_chat_message(whisper_message))
            {
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("invalid whisper message"));
                continue;
            }

            SOCKET target_sock = find_client_by_nickname(target_nickname);
            if (target_sock == INVALID_SOCKET)
            {
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("user not found: " + target_nickname));
                continue;
            }

            if (target_sock == sock)
            {
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("cannot whisper to yourself"));
                continue;
            }

            const string received_whisper = timestamp_message("[from " + nickname + "] " + whisper_message);
            if (!send_packet(target_sock, MESSAGE_TYPE::WHISPER, received_whisper))
            {
                log_server("ERROR", "failed to deliver whisper to " + target_nickname);
                send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("failed to deliver whisper"));
                continue;
            }

            const string sent_whisper = timestamp_message("[to " + target_nickname + "] " + whisper_message);
            send_packet(sock, MESSAGE_TYPE::WHISPER, sent_whisper);
            log_server("CHAT", nickname + " whispered to " + target_nickname + " : " + whisper_message);
            continue;
        }

        if (starts_with_command(payload, "/whisper") || starts_with_command(payload, "/w"))
        {
            send_packet(sock, MESSAGE_TYPE::SYSTEM_ERROR, timestamp_message("usage: /whisper <nickname> <message>"));
            continue;
        }

        string chat_msg = timestamp_message(nickname + " : " + payload);

        log_server("CHAT", nickname + " : " + payload);
        broadcast_message(MESSAGE_TYPE::CHAT, chat_msg, sock);
    }

    {
        const string leave_msg = timestamp_message(nickname + " left the chat.");
        log_server("INFO", nickname + " left the chat");
        broadcast_message(MESSAGE_TYPE::SYSTEM_LEAVE, leave_msg, INVALID_SOCKET);
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
            string log_message = "send() error. sock=" + to_string(static_cast<long long>(target_sock));
            if (sender_sock != INVALID_SOCKET && target_sock == sender_sock)
            {
                log_message += " (sender)";
            }
            log_server("ERROR", log_message);
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

    if (m_serv_sock != INVALID_SOCKET)
    {
        shutdown(m_serv_sock, SD_BOTH);
    }

    vector<SOCKET> client_sockets;

    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
        {
            if (it->second.sock != INVALID_SOCKET)
            {
                client_sockets.push_back(it->second.sock);
            }
        }
    }

    for (size_t i = 0; i < client_sockets.size(); ++i)
    {
        shutdown(client_sockets[i], SD_BOTH);
    }

    vector<thread> client_threads;
    {
        lock_guard<mutex> thread_lock(m_thread_lock);
        client_threads.swap(m_client_threads);
    }

    for (size_t i = 0; i < client_threads.size(); ++i)
    {
        if (client_threads[i].joinable())
        {
            client_threads[i].join();
        }
    }

    {
        lock_guard<mutex> lock(m_client_lock);
        for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
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

    if (m_wsa_ready)
    {
        WSACleanup();
        m_wsa_ready = false;
    }
}

bool server_session::nickname_exists(const string& nickname, SOCKET exclude_sock)
{
    lock_guard<mutex> lock(m_client_lock);

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->first == exclude_sock)
        {
            continue;
        }

        if (it->second.nickname == nickname)
        {
            return true;
        }
    }

    return false;
}

size_t server_session::client_count()
{
    lock_guard<mutex> lock(m_client_lock);
    return m_client_info.size();
}

SOCKET server_session::find_client_by_nickname(const string& nickname)
{
    lock_guard<mutex> lock(m_client_lock);

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.nickname == nickname && it->second.sock != INVALID_SOCKET)
        {
            return it->second.sock;
        }
    }

    return INVALID_SOCKET;
}

string server_session::build_user_list()
{
    lock_guard<mutex> lock(m_client_lock);

    string user_list = "connected users: ";
    bool first = true;

    for (map<SOCKET, USER_INFO>::iterator it = m_client_info.begin(); it != m_client_info.end(); ++it)
    {
        if (it->second.sock == INVALID_SOCKET)
        {
            continue;
        }

        if (!first)
        {
            user_list += ", ";
        }

        user_list += it->second.nickname;
        first = false;
    }

    if (first)
    {
        user_list += "(none)";
    }

    return user_list;
}
