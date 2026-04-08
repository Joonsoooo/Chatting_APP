#pragma once

#include "def.h"

struct ROOM_MESSAGE_RECORD
{
	string timestamp;
	string room_name;
	string message_type;
	string sender;
	string payload;
};

class storage_repository
{
public:
    bool init(const string& base_dir);

    bool register_account(const string& username, const string& password, const string& display_name, string& error_message);
    bool authenticate_account(const string& username, const string& password, string& display_name, string& error_message);
    bool update_display_name(const string& username, const string& display_name);

    bool load_room_state(map<string, string>& room_owners, map<string, bool>& room_bot_enabled);
    bool save_room_state(const map<string, string>& room_owners, const map<string, bool>& room_bot_enabled);

    void append_room_message(const ROOM_MESSAGE_RECORD& record);
    vector<ROOM_MESSAGE_RECORD> load_recent_room_messages(const string& room_name, size_t limit);

private:
    struct ACCOUNT_RECORD
    {
        string username;
        string password_hash;
        string display_name;
        string created_at;
    };

    string hash_password(const string& password) const;
    bool load_accounts(vector<ACCOUNT_RECORD>& accounts);
    bool save_accounts(const vector<ACCOUNT_RECORD>& accounts);

    string m_base_dir;
    string m_accounts_path;
    string m_rooms_path;
    string m_messages_path;
    mutex m_storage_lock;
};
