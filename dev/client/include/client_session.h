#pragma once
#include "def.h"

class client_session
{
public:
	client_session();
	~client_session();

	bool init();
	void close();
	void configure_endpoint(const string& host, const string& port);
	bool connect_server();
	void recv_loop();
	bool send_nickname(const string& nickname);
	bool send_auth_request(const string& mode, const string& username, const string& password, const string& display_name);
	bool send_chat(const string& msg);
	bool wait_for_nickname_response();
	bool is_running() const;
private:
	string m_host;
	string m_port;
	string m_auth_mode;
	USER_INFO m_user_info;
	atomic<bool> m_running;
	atomic<bool> m_wsa_ready;
	mutex m_state_lock;
};

