#include "client_session.h"

int main()
{
    client_session client;
    string host;
    string port;

    cout << "server ip (default 127.0.0.1): ";
    getline(cin, host);

    cout << "server port (default 9000): ";
    getline(cin, port);

    client.configure_endpoint(host, port);

    if (false == client.init())
    {
        return 1;
    }

    if (false == client.connect_server())
    {
        return 1;
    }

    string auth_mode;
    cout << "auth mode (guest/login/register, default guest): ";
    getline(cin, auth_mode);

    if (auth_mode.empty())
    {
        auth_mode = "guest";
    }

    bool handshake_sent = false;
    if (auth_mode == "login" || auth_mode == "register")
    {
        string username;
        string password;
        string display_name;

        cout << "account: ";
        getline(cin, username);
        cout << "password: ";
        getline(cin, password);
        cout << "display name: ";
        getline(cin, display_name);

        handshake_sent = client.send_auth_request(auth_mode, username, password, display_name);
    }
    else
    {
        string nickname;
        cout << "nickname: ";
        getline(cin, nickname);
        handshake_sent = client.send_nickname(nickname);
    }

    if (false == handshake_sent)
    {
        client.close();

        return 1;
    }

    if (false == client.wait_for_nickname_response())
    {
        client.close();

        return 1;
    }

    thread recv_thread(&client_session::recv_loop, &client);

    while (client.is_running())
    {
        string input;
        if (!getline(cin, input))
        {
            break;
        }

        if (input == "exit")
        {
            break;
        }

        if (!input.empty())
        {
            if (!client.send_chat(input))
            {
                break;
            }
        }
    }

    client.close();

    if (recv_thread.joinable())
    {
        recv_thread.join();
    }

    return 0;
}
