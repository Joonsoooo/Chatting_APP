#include "client_session.h"

int main()
{
    client_session client;

    if (false == client.init())
    {
        return 1;
    }

    if (false == client.connect_server())
    {
        return 1;
    }

    string nickname;
    
    cout << "nickname: ";

    getline(cin, nickname);

    if (false == client.send_nickname(nickname))
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
