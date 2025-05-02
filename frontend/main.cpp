#define _MAIN_CPP
#include "cloud-compile-frontend.hpp"

int main(int argc, char *argv[])
{
    ClientSocket connection_socket(SERVER_IP, DEFAULT_PORT);
    vector<char> msg {'H', 'l', 'e', '!'};
    connection_socket.send_binary("msg", msg);
}
