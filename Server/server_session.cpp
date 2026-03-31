#include "server_session.h"

void server_session::init()
{

}

void server_session::clear()
{
	WSAData wsa_data;
	SOCKET serv_sock, clnt_sock;
	SOCKADDR_IN serv_addr, clnt_addr;
	int sz_clnt_addr;

	char msg[4096];

	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		print_err("WSAStartup() error!");

	serv_sock = socket(PF_INET, SOCK_STREAM, 0);

	if (serv_sock == INVALID_SOCKET)
		print_err("socker() error");

	memset(&wsa_data, 0, sizeof(wsa_data));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(atoi(SERVER_IP));

	if (bind(serv_sock, (SOCKADDR*)&serv_addr, sizeof(serv_addr)) == SOCKET_ERROR)
		print_err("bind() error!");

	if (listen(serv_sock, 5) == SOCKET_ERROR)
		print_err("listen() error!");

	sz_clnt_addr = sizeof(clnt_addr);
	clnt_sock = accept(serv_sock, (SOCKADDR*)&clnt_addr, &sz_clnt_addr);

	if (clnt_sock == INVALID_SOCKET)
		print_err("accept() error!");

	send(clnt_sock, msg, sizeof(msg), 0);

	closesocket(clnt_sock);
	closesocket(serv_sock);

	WSACleanup();
}

void server_session::accept_session()
{

}