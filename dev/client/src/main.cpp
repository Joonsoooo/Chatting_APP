#include "client_session.h"

int main()
{
    client_session client;

    client.init();
    client.connect_server();

    string nickname;
    cout << "nickname: ";
    getline(std::cin, nickname);
    client.send_message(nickname);

    thread recv_thread(&client_session::recv_loop, &client);

    while (true)
    {
        std::string input;
        std::getline(std::cin, input);

        if (input == "exit")
        {
            break;
        }

        if (!input.empty())
        {
            client.send_message(input);
        }
    }

    client.close();

    if (recv_thread.joinable())
    {
        recv_thread.detach();
    }

    return 0;
}