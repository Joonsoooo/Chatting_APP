#pragma once

#include <iostream>
#include <WinSock2.h>

static void print_wsa_error(const char* msg)
{
	std::cerr << msg << " (WSA error: " << WSAGetLastError() << ")\n";
}
