#pragma once
#include "def.h"

class client_session
{
public:
	client_session() {}
	~client_session() {}

	void init();
	void clear();
	void begin_session();
	void print_err(const char* msg);
private:
	USER_INFO m_USER_INFO;

	int m_sz_addr;

};

