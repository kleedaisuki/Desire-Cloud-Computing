#define _MAIN_CPP
#include "cloud-compile-frontend.hpp"

char eof_flag[] = {EOF};

int main(int argc, char *argv[])
{
    try
    {
        initialize();
    }
    catch (const runtime_error &err)
    {
        cerr << "Error occurred when initializing:" << endl
             << "    " << err.what() << endl
             << "Program terminated..." << endl;
        return EXIT_FAILURE;
    }

    Socket client(SERVER_IP, DEFAULT_PORT);
    client.send("Hello World!");

    try
    {
        finalize();
    }
    catch (const runtime_error &err)
    {
        cerr << "Error occurred when finalizing:" << endl
             << "    " << err.what() << endl
             << "Program terminated..." << endl;
        return EXIT_FAILURE;
    }
}
