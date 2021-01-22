#include "log.h"
#include "epoll.h"

int main (int argc, char** argv) {
    if (argc != 3) {
        LOG_INFO("usage: ./%s [ip] [port]\n", argv[0]);
        exit(-1);
    }

    eplloReactor* reactor = apiCreateReactor();
    if (reactor == NULL) {
        LOG_INFO("creat reactor error, errno: %s", strerror(errno));
        exit(-1);
    }

    char* ip = argv[1];
    short port = atoi(argv[2]);
    int listenFd = apiInitServer(ip, port, reactor);
    if (listenFd < 0) {
        LOG_INFO("init tcp server error, errno: %s", strerror(errno));
        exit(-1);
    }

    apiRun(reactor);
    apiFree(reactor);

    close(listenFd);
    return 0;
}