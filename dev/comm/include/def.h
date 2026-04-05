#pragma once

//-----------
// include
//-----------
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <WinSock2.h>
#include <ws2tcpip.h>

#pragma comment(lib,"ws2_32")

using namespace std;

//-----------
// struct
//-----------
struct USER_INFO
{
	string nickname ="";
	int id = -1;

	SOCKET sock = INVALID_SOCKET;
};

enum class MESSAGE_TYPE : uint32_t
{
	NICKNAME = 1,
	CHAT = 2,
	SYSTEM = 3,
};

struct PACKET_HEADER
{
	uint32_t type = 0;
	uint32_t size = 0;
};

//-----------
// function
//-----------
static void print_wsa_error(const char* msg)
{
	cerr << msg << " (WSA error: " << WSAGetLastError() << ")\n";
}

static bool send_all(SOCKET sock, const char* data, int len)
{
	int total_sent = 0;

	while (total_sent < len)
	{
		int sent = send(sock, data + total_sent, len - total_sent, 0);
		if (sent == SOCKET_ERROR || sent == 0)
		{
			return false;
		}

		total_sent += sent;
	}

	return true;
}

static bool recv_all(SOCKET sock, char* data, int len)
{
	int total_recv = 0;

	while (total_recv < len)
	{
		int received = recv(sock, data + total_recv, len - total_recv, 0);
		if (received == SOCKET_ERROR || received == 0)
		{
			return false;
		}

		total_recv += received;
	}

	return true;
}

static bool send_packet(SOCKET sock, MESSAGE_TYPE type, const string& payload)
{
	PACKET_HEADER header;
	header.type = htonl(static_cast<uint32_t>(type));
	header.size = htonl(static_cast<uint32_t>(payload.size()));

	if (!send_all(sock, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof(header))))
	{
		return false;
	}

	if (payload.empty())
	{
		return true;
	}

	return send_all(sock, payload.data(), static_cast<int>(payload.size()));
}

static bool recv_packet(SOCKET sock, MESSAGE_TYPE& type, string& payload)
{
	PACKET_HEADER header;
	if (!recv_all(sock, reinterpret_cast<char*>(&header), static_cast<int>(sizeof(header))))
	{
		return false;
	}

	const uint32_t type_value = ntohl(header.type);
	const uint32_t payload_size = ntohl(header.size);

	if (payload_size > 64 * 1024)
	{
		cerr << "packet too large: " << payload_size << "\n";
		return false;
	}

	type = static_cast<MESSAGE_TYPE>(type_value);

	payload.clear();

	if (payload_size == 0)
	{
		return true;
	}

	vector<char> buffer(payload_size);
	if (!recv_all(sock, buffer.data(), static_cast<int>(payload_size)))
	{
		return false;
	}

	payload.assign(buffer.begin(), buffer.end());
	return true;
}
