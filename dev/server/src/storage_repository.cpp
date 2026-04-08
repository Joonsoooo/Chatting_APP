#include "storage_repository.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
string current_iso_timestamp()
{
    const time_t now_time = time(nullptr);
    tm local_tm = {};
    localtime_s(&local_tm, &now_time);

    ostringstream oss;
    oss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

string escape_field(const string& value)
{
    string escaped;
    escaped.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        switch (ch)
        {
        case '%':
            escaped += "%25";
            break;
        case '\t':
            escaped += "%09";
            break;
        case '\n':
            escaped += "%0A";
            break;
        case '\r':
            escaped += "%0D";
            break;
        default:
            escaped.push_back(static_cast<char>(ch));
            break;
        }
    }

    return escaped;
}

string unescape_field(const string& value)
{
    string restored;
    restored.reserve(value.size());

    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%' && i + 2 < value.size())
        {
            const string token = value.substr(i, 3);
            if (token == "%25")
            {
                restored.push_back('%');
                i += 2;
                continue;
            }
            if (token == "%09")
            {
                restored.push_back('\t');
                i += 2;
                continue;
            }
            if (token == "%0A")
            {
                restored.push_back('\n');
                i += 2;
                continue;
            }
            if (token == "%0D")
            {
                restored.push_back('\r');
                i += 2;
                continue;
            }
        }

        restored.push_back(value[i]);
    }

    return restored;
}

vector<string> split_tab_line(const string& line)
{
    vector<string> parts;
    size_t start = 0;
    while (start <= line.size())
    {
        const size_t tab_pos = line.find('\t', start);
        if (tab_pos == string::npos)
        {
            parts.push_back(line.substr(start));
            break;
        }

        parts.push_back(line.substr(start, tab_pos - start));
        start = tab_pos + 1;
    }

    return parts;
}
}

bool storage_repository::init(const string& base_dir)
{
    lock_guard<mutex> lock(m_storage_lock);

    m_base_dir = base_dir;
    m_accounts_path = m_base_dir + "\\accounts.db";
    m_rooms_path = m_base_dir + "\\rooms.db";
    m_messages_path = m_base_dir + "\\messages.db";

    error_code ec;
    filesystem::create_directories(m_base_dir, ec);
    if (ec)
    {
        return false;
    }

    ofstream(m_accounts_path, ios::app).close();
    ofstream(m_rooms_path, ios::app).close();
    ofstream(m_messages_path, ios::app).close();
    return true;
}

string storage_repository::hash_password(const string& password) const
{
    const string salted = "chatting_app_salt::" + password;
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < salted.size(); ++i)
    {
        hash ^= static_cast<unsigned char>(salted[i]);
        hash *= 1099511628211ULL;
    }

    ostringstream oss;
    oss << hex << setw(16) << setfill('0') << hash;
    return oss.str();
}

bool storage_repository::load_accounts(vector<ACCOUNT_RECORD>& accounts)
{
    ifstream input(m_accounts_path);
    if (!input.is_open())
    {
        return false;
    }

    accounts.clear();
    string line;
    while (getline(input, line))
    {
        if (line.empty())
        {
            continue;
        }

        const vector<string> fields = split_tab_line(line);
        if (fields.size() < 4)
        {
            continue;
        }

        ACCOUNT_RECORD record;
        record.username = unescape_field(fields[0]);
        record.password_hash = unescape_field(fields[1]);
        record.display_name = unescape_field(fields[2]);
        record.created_at = unescape_field(fields[3]);
        accounts.push_back(record);
    }

    return true;
}

bool storage_repository::save_accounts(const vector<ACCOUNT_RECORD>& accounts)
{
    ofstream output(m_accounts_path, ios::trunc);
    if (!output.is_open())
    {
        return false;
    }

    for (size_t i = 0; i < accounts.size(); ++i)
    {
        output
            << escape_field(accounts[i].username) << '\t'
            << escape_field(accounts[i].password_hash) << '\t'
            << escape_field(accounts[i].display_name) << '\t'
            << escape_field(accounts[i].created_at) << '\n';
    }

    return true;
}

bool storage_repository::register_account(const string& username, const string& password, const string& display_name, string& error_message)
{
    lock_guard<mutex> lock(m_storage_lock);

    vector<ACCOUNT_RECORD> accounts;
    if (!load_accounts(accounts))
    {
        error_message = "failed to load account database";
        return false;
    }

    for (size_t i = 0; i < accounts.size(); ++i)
    {
        if (accounts[i].username == username)
        {
            error_message = "account already exists";
            return false;
        }
    }

    ACCOUNT_RECORD record;
    record.username = username;
    record.password_hash = hash_password(password);
    record.display_name = display_name;
    record.created_at = current_iso_timestamp();
    accounts.push_back(record);

    if (!save_accounts(accounts))
    {
        error_message = "failed to save account database";
        return false;
    }

    return true;
}

bool storage_repository::authenticate_account(const string& username, const string& password, string& display_name, string& error_message)
{
    lock_guard<mutex> lock(m_storage_lock);

    vector<ACCOUNT_RECORD> accounts;
    if (!load_accounts(accounts))
    {
        error_message = "failed to load account database";
        return false;
    }

    const string password_hash = hash_password(password);
    for (size_t i = 0; i < accounts.size(); ++i)
    {
        if (accounts[i].username == username)
        {
            if (accounts[i].password_hash != password_hash)
            {
                error_message = "invalid password";
                return false;
            }

            display_name = accounts[i].display_name;
            return true;
        }
    }

    error_message = "account not found";
    return false;
}

bool storage_repository::update_display_name(const string& username, const string& display_name)
{
    lock_guard<mutex> lock(m_storage_lock);

    vector<ACCOUNT_RECORD> accounts;
    if (!load_accounts(accounts))
    {
        return false;
    }

    for (size_t i = 0; i < accounts.size(); ++i)
    {
        if (accounts[i].username == username)
        {
            accounts[i].display_name = display_name;
            return save_accounts(accounts);
        }
    }

    return false;
}

bool storage_repository::load_room_state(map<string, string>& room_owners, map<string, bool>& room_bot_enabled)
{
    lock_guard<mutex> lock(m_storage_lock);

    ifstream input(m_rooms_path);
    if (!input.is_open())
    {
        return false;
    }

    room_owners.clear();
    room_bot_enabled.clear();

    string line;
    while (getline(input, line))
    {
        if (line.empty())
        {
            continue;
        }

        const vector<string> fields = split_tab_line(line);
        if (fields.size() < 3)
        {
            continue;
        }

        const string room_name = unescape_field(fields[0]);
        room_owners[room_name] = unescape_field(fields[1]);
        room_bot_enabled[room_name] = unescape_field(fields[2]) == "1";
    }

    return true;
}

bool storage_repository::save_room_state(const map<string, string>& room_owners, const map<string, bool>& room_bot_enabled)
{
    lock_guard<mutex> lock(m_storage_lock);

    ofstream output(m_rooms_path, ios::trunc);
    if (!output.is_open())
    {
        return false;
    }

    for (map<string, string>::const_iterator it = room_owners.begin(); it != room_owners.end(); ++it)
    {
        const bool bot_enabled = room_bot_enabled.find(it->first) != room_bot_enabled.end() && room_bot_enabled.at(it->first);
        output
            << escape_field(it->first) << '\t'
            << escape_field(it->second) << '\t'
            << (bot_enabled ? "1" : "0") << '\n';
    }

    return true;
}

void storage_repository::append_room_message(const ROOM_MESSAGE_RECORD& record)
{
    lock_guard<mutex> lock(m_storage_lock);

    ofstream output(m_messages_path, ios::app);
    if (!output.is_open())
    {
        return;
    }

    output
        << escape_field(record.timestamp) << '\t'
        << escape_field(record.room_name) << '\t'
        << escape_field(record.message_type) << '\t'
        << escape_field(record.sender) << '\t'
        << escape_field(record.payload) << '\n';
}

vector<ROOM_MESSAGE_RECORD> storage_repository::load_recent_room_messages(const string& room_name, size_t limit)
{
    lock_guard<mutex> lock(m_storage_lock);

    vector<ROOM_MESSAGE_RECORD> history;
    ifstream input(m_messages_path);
    if (!input.is_open())
    {
        return history;
    }

    string line;
    while (getline(input, line))
    {
        if (line.empty())
        {
            continue;
        }

        const vector<string> fields = split_tab_line(line);
        if (fields.size() < 5)
        {
            continue;
        }

        ROOM_MESSAGE_RECORD record;
        record.timestamp = unescape_field(fields[0]);
        record.room_name = unescape_field(fields[1]);
        record.message_type = unescape_field(fields[2]);
        record.sender = unescape_field(fields[3]);
        record.payload = unescape_field(fields[4]);

        if (record.room_name != room_name)
        {
            continue;
        }

        history.push_back(record);
        if (history.size() > limit)
        {
            history.erase(history.begin());
        }
    }

    return history;
}
