#pragma once

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <WinSock2.h>
#include <ws2tcpip.h>

#pragma comment(lib,"ws2_32")

using namespace std;

#include "error.h"
#include "packet.h"
#include "socket_utils.h"
#include "validation.h"

struct USER_INFO
{
	string nickname ="";
	string room = "Lobby";
	int id = -1;

	SOCKET sock = INVALID_SOCKET;
};
