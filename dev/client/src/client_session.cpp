//#include "server_session.h"
#include "client_session.h"

const char* IP = "127.0.0.1";
const char* PORT = "9000";


void client_session::init()
{
	WSADATA wsa_data;

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		print_err("WSAStartup() error");
	}

	m_user_info.sock = socket(PF_INET, SOCK_STREAM, 0);

	if (m_user_info.sock == INVALID_SOCKET)
	{
		print_err("sock() error");
	}

}

void client_session::close()
{
	closesocket(m_user_info.sock);
	WSACleanup();
}

void client_session::connect_server()
{
    SOCKADDR_IN addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    InetPtonA(AF_INET, IP, &addr.sin_addr);
    addr.sin_port = htons(atoi(PORT));

    if (connect(m_user_info.sock, (SOCKADDR*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        print_err("connect() error!");
    }
}

void client_session::recv_loop()
{
    while (true)
    {
        char msg[4097] = { 0 };

        int msg_size = recv(m_user_info.sock, msg, 4096, 0);

        if (msg_size == SOCKET_ERROR)
        {
            std::cout << "recv() error!\n";
            break;
        }
        else if (msg_size == 0)
        {
            std::cout << "server closed connection\n";
            break;
        }

        msg[msg_size] = '\0';
        cout << msg << endl;
    }
}

void client_session::send_message(const string& msg)
{
    if (send(m_user_info.sock, msg.c_str(), (int)msg.size(), 0) == SOCKET_ERROR)
    {
        print_err("send() error!");
    }
}

void client_session::begin_session()
{
    while (true)
    {
        char msg[4097] = { 0 };

        int msg_size = recv(m_user_info.sock, msg, 4096, 0);

        if (msg_size == SOCKET_ERROR)
        {
            print_err("recv() error!");
        }
        else if (msg_size == 0)
        {
            printf("server closed connection\n");
            break;
        }

        msg[msg_size] = '\0';
        printf("recv message: %s\n", msg);
    }
}
