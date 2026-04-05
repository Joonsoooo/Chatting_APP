#include "client_session.h"

const char* IP = "127.0.0.1";
const char* PORT = "9000";

client_session::client_session()
	: m_running(false), m_wsa_ready(false)
{
}

client_session::~client_session()
{
	close();

	if (m_wsa_ready)
	{
		WSACleanup();

		m_wsa_ready = false;
	}
}

bool client_session::init()
{
	WSADATA wsa_data;

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		print_wsa_error("WSAStartup() error");

		return false;
	}

	m_wsa_ready = true;

	m_user_info.sock = socket(PF_INET, SOCK_STREAM, 0);

	if (m_user_info.sock == INVALID_SOCKET)
	{
		print_wsa_error("socket() error");

		WSACleanup();

		m_wsa_ready = false;

		return false;
	}

	return true;
}

void client_session::close()
{
	lock_guard<mutex> lock(m_state_lock);

	m_running = false;

	if (m_user_info.sock != INVALID_SOCKET)
	{
		shutdown(m_user_info.sock, SD_BOTH);

		closesocket(m_user_info.sock);

		m_user_info.sock = INVALID_SOCKET;
	}
}

bool client_session::connect_server()
{
    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    InetPtonA(AF_INET, IP, &addr.sin_addr);
    addr.sin_port = htons(atoi(PORT));

    if (connect(m_user_info.sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        print_wsa_error("connect() error!");

		return false;
    }

	m_running = true;

	return true;
}

void client_session::recv_loop()
{
    while (m_running)
    {
		MESSAGE_TYPE type = MESSAGE_TYPE::SYSTEM;

		string payload;

		if (!recv_packet(m_user_info.sock, type, payload))
		{
			if (m_running)
			{
				cout << "server closed connection\n";
			}

			break;
		}

		if (type == MESSAGE_TYPE::CHAT || type == MESSAGE_TYPE::SYSTEM)
		{
			cout << payload << endl;
		}
    }

	m_running = false;
}

bool client_session::send_nickname(const string& nickname)
{
	if (!send_packet(m_user_info.sock, MESSAGE_TYPE::NICKNAME, nickname))
	{
		print_wsa_error("send nickname error");

		m_running = false;

		return false;
	}

	return true;
}

bool client_session::send_chat(const string& msg)
{
	if (!m_running)
	{
		return false;
	}

	if (!send_packet(m_user_info.sock, MESSAGE_TYPE::CHAT, msg))
	{
		print_wsa_error("send chat error");

		m_running = false;

		return false;
	}

	return true;
}

bool client_session::is_running() const
{
	return m_running;
}
