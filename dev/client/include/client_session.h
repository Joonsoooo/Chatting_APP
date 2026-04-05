#pragma once
#include "def.h"

class client_session
{
public:
	client_session();
	~client_session();

	bool init();
	void close();
	bool connect_server();
	void recv_loop();
	bool send_nickname(const string& nickname);
	bool send_chat(const string& msg);
	bool is_running() const;
private:
	USER_INFO m_user_info;
	atomic<bool> m_running;
	atomic<bool> m_wsa_ready;
	mutex m_state_lock;
};

