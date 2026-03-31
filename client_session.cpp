#include "server_session.h"
#include "client_session.h"

const long IP = 1;
const char* PORT = "2";


void client_session::init()
{
	//ck = new SOCKET();

}

void client_session::clear()
{
	//lete sock;
	
	//ck = nullptr;
}

void client_session::begin_session()
{
	WSADATA wsa_data;
	SOCKADDR_IN addr;

	int msg_size;

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		print_err("WSAStartup() error");
	}

	m_USER_INFO.sock = socket(PF_INET, SOCK_STREAM, 0);

	if (m_USER_INFO.sock == INVALID_SOCKET)
	{
		print_err("sock() error");
	}

	memset(&addr, 0, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = IP;
	addr.sin_port = htons(atoi(PORT));

	if (connect(m_USER_INFO.sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		print_err("connect() error!");
	}

	char msg[4096];
	msg_size = recv(m_USER_INFO.sock, msg, sizeof(msg), 0);

	if (msg_size == 1)
	{
		print_err("read() error!");
	}

	cout << msg << endl;

	closesocket(m_USER_INFO.sock);
	WSACleanup();
}

void client_session::print_err(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}
