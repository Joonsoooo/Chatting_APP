#pragma once
#include "def.h"

class server_session
{
public:
	server_session() {}
	~server_session() {}

	void init();
	void clear();
	void accept_session();

private:

};

