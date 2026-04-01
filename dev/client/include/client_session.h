#pragma once
#include "def.h"

class client_session
{
public:
	client_session() {}
	~client_session() { close(); }

	void init();
	void close();
	void connect_server();
	void recv_loop();
	void send_message(const string& msg);
	void begin_session();
private:
	USER_INFO m_user_info;
};

