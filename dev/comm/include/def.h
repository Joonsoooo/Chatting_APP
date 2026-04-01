#pragma once

//-----------
// include
//-----------
#include <iostream>
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <mutex>
#include <map>

#pragma comment(lib,"ws2_32")

using namespace std;

//-----------
// string
//-----------
//const char* CLIENT_IP = "1";
//const char* SERVER_IP = "2";

//-----------
// struct
//-----------
struct USER_INFO
{
	string nickname ="";
	int id = -1;

	SOCKET sock = INVALID_SOCKET;
};

//-----------
// function
//-----------
static void print_err(const char* msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}