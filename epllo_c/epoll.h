/*
    Time:        2021.1.21
    Author:      CSDN
    Description: epoll, tcp网络编程
*/
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include "log.h"

typedef void (*callBack)(int, int, void*);
#define MAX_EPOLL_EVENT 1024*100
#define MAX_BUF_SIZE 4096

enum isRegisterEpoll {
    REGISTER_NOT,
    REGISTER_YES,
};

typedef struct eplloUserEvent
{
    int fd;                                 //accept返回的对应的事件的描述符
    int event;                              //事件的类型， 读写
    void* arg;                              //事件回调backFun的参数3
    void (*backFun)(int, int, void*arg);    //事件的回调函数
    char buffer[MAX_BUF_SIZE];              //定一个事件的数据缓冲区
    int length;                             //缓冲区数据长度
    enum isRegisterEpoll mask;              //事件是否有在epoll_fd中注册
    long lastActiveTime;                    //上次事件调用的活跃时间
    char* ip;                               //对应的fd IP
    int port;                               //对应的fd 端口
}eplloUserEvent;

//事件
typedef struct eplloReactor
{
    int epollFd;
    int listenFd;
    eplloUserEvent* reactorEvents;
}eplloReactor;

static int apiSetReUseAddr(int fd);
static int apiSetNoBlock(int listenFd);
static int apiUserEventRegister(eplloUserEvent* ev, int fd, char*Ip, int port, int event, enum isRegisterEpoll mask, callBack callFun, void *arg);
static int apiEventAdd(int epollFd, eplloUserEvent* ev);
static void apiRecvMsgCallFun(int fd, int event, void* ptr);
static void apiSendMsgCallFun(int d, int event, void* ptr);
static void apiAcceptCallFun(int fd, int event, void* ptr);

static int apiInitServer(char* ip, short port, eplloReactor* reactor) { 
    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd < 0) {
        LOG_INFO("创建socketFd出错, errno:%s", strerror(errno));
        return -1;
    }

    struct sockaddr_in server;
    memset(&server, 0 , sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(ip);
    server.sin_port=htons(port);


    if (apiSetNoBlock(sockFd) == -1){
        LOG_INFO("apiSetNoBlock, errno: %s", strerror(errno));
    }

    if (apiSetReUseAddr(sockFd) == -1){
        LOG_INFO("apiSetReUseAddr, errno: %s", strerror(errno));
    }

    if (bind(sockFd, (const struct sockaddr*)&server, sizeof(server)) == -1) {
        LOG_INFO("bind error, errno:%s", strerror(errno));
        return -1;
    }

    if (listen(sockFd, MAX_EPOLL_EVENT) == -1) {
        LOG_INFO("listen error, errno:%s", strerror(errno));
        return -1;
    }
    LOG_INFO("Listen start [%s:%d] in fd:%d ...", inet_ntoa(server.sin_addr), ntohs(server.sin_port), sockFd);

    reactor->listenFd = sockFd;
    apiUserEventRegister(&reactor->reactorEvents[sockFd], 
                        sockFd,
                        inet_ntoa(server.sin_addr),
                        ntohs(server.sin_port),
                        EPOLLIN,
                        REGISTER_NOT,
                        apiAcceptCallFun,
                        reactor);
    apiEventAdd(reactor->epollFd, &reactor->reactorEvents[sockFd]);
    return sockFd;
};

static eplloReactor* apiCreateReactor() {
    eplloReactor* reactor = (eplloReactor*)malloc(sizeof(struct eplloReactor));
    memset(reactor, 0, sizeof(struct eplloReactor));
    if (reactor == NULL) {
        LOG_INFO("malloc reactor error, errno:%s", strerror(errno));
        return NULL;
    }

    reactor->epollFd = epoll_create(1);
    if (reactor->epollFd == -1) {
        LOG_INFO("epoll_create error, errno:%s", strerror(errno));
        free(reactor);
        return NULL; 
    }

    eplloUserEvent* events = (eplloUserEvent*)malloc(sizeof(struct eplloUserEvent) * MAX_EPOLL_EVENT);
    if (events == NULL) {
        LOG_INFO("malloc events error, errno:%s", strerror(errno));
        close(reactor->epollFd);
        free(reactor);
        return NULL;
    }

    reactor->reactorEvents = events;
    return reactor;
}

static int apiUserEventRegister(eplloUserEvent* ev, int fd, char*Ip, int port, int event, enum isRegisterEpoll mask, callBack callFun, void *arg){
    if (ev == NULL || fd < 0 || callFun == NULL || arg == NULL) {
        LOG_INFO("apiEventRegister error, errno:%s", strerror(errno));
        return -1; 
    }

    ev->backFun = callFun;
    ev->event = event;
    ev->fd = fd;
    ev->mask = mask;
    ev->lastActiveTime = time(NULL);
    ev->arg = arg;
    ev->ip = Ip;
    ev->port = port;
    // memset(&ev->buffer, 0, MAX_BUF_SIZE);
    // ev->length = 0;
    
    return 0;
}

static int apiEventAdd(int epollFd, eplloUserEvent* ev){
    if (epollFd < 0 || ev == NULL) {
        LOG_INFO("apiEventAdd error, errno:%s", strerror(errno));
        LOG_INFO("epollFd:%d, ev: %p", epollFd, ev);
        return -1;
    }

    struct epoll_event epollEvent;
    memset(&epollEvent, 0 ,sizeof(struct epoll_event));
    epollEvent.events = ev->event;
    epollEvent.data.ptr = ev;
    //epollEvent.data.fd = ev->fd;   //data成员是一个联合体, 不能同时使用fd和ptr成员

    int opt;
    if (ev->mask == REGISTER_YES) {
        opt = EPOLL_CTL_MOD;
    } else {
        opt = EPOLL_CTL_ADD;
        ev->mask = REGISTER_YES;
    }
    
    if (epoll_ctl(epollFd, opt, ev->fd, &epollEvent) == -1){
        LOG_INFO("apiEventAdd epoll_ctl %d error, errno:%s, ev->Fd:%d", opt ,strerror(errno), ev->fd);
        return -1;
    }

    return 0;
}

static int apiEventDeL(int epollFd, eplloUserEvent* ev){
    if (epollFd < 0 || ev == NULL || ev->mask != REGISTER_YES) {
        LOG_INFO("apiEventDeL error, errno:%s", strerror(errno));
        LOG_INFO("epollFd:%d, ev:%p, ev->mater:%d", epollFd, ev, ev->mask);
        return -1;
    }

    struct epoll_event epollEvent;
    memset(&epollEvent, 0 ,sizeof(epollEvent));
    ev->mask = REGISTER_NOT;
    epollEvent.data.ptr = ev;
    //epollEvent.data.fd = ev->fd;   //data成员是一个联合体, 不能同时使用fd和ptr成员

    if (epoll_ctl(epollFd, EPOLL_CTL_DEL, ev->fd, &epollEvent) == -1){
        LOG_INFO("apiEventDeL error, errno:%s, ev->fd:%d", strerror(errno), ev->fd);
        return -1;
    }

    return 0;
}

static int apiSetReUseAddr(int fd) {
	/* 
    在重新启动服务器程序前，它需要在 1~4 分钟。
    这就是很多网络服务器程序被杀死后不能够马上重新启动的原因（错误提示为“ Address already in use ”），
    设置这个之后可以马上启动
    */
	int reuse = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
}

static int apiSetNoBlock(int listenFd) {
    int opts;
    opts=fcntl(listenFd,F_GETFL);
    if(opts<0){
        LOG_INFO("fcntl(sock,GETFL) error");
        return -1;
    }
    opts = opts|O_NONBLOCK;
    if(fcntl(listenFd,F_SETFL,opts)<0){
        LOG_INFO("fcntl(sock,SETFL,opts) error");
        return -1;
    }
}

static void apiAcceptCallFun(int fd, int event, void* ptr) {
    eplloReactor* reactor = (eplloReactor*)ptr;
    eplloUserEvent* ev = &reactor->reactorEvents[fd];

    struct sockaddr_in client;
    socklen_t len = sizeof(struct sockaddr_in);
    memset(&client, 0 ,sizeof(struct sockaddr_in));
    int clientFd = accept(ev->fd, (struct sockaddr*)&client, (socklen_t*)&len);
    LOG_INFO("epollFd:%d, clientFd: %d", reactor->epollFd, clientFd);
    if (clientFd < 0) {
        LOG_INFO("apiAcceptCallFun %d error, errno:%s", ev->fd, strerror(errno));
        return; 
    }

    int pos = 0;
    do {
        for (pos = 7; pos < MAX_EPOLL_EVENT; ++pos) {
            if (reactor->reactorEvents[pos].mask == REGISTER_NOT) {
                break;
            }
        }

        if (pos == MAX_EPOLL_EVENT) {
            LOG_INFO("max connect limit[%d]", MAX_EPOLL_EVENT);
            return;
        }

        apiSetNoBlock(clientFd);
        apiUserEventRegister(&reactor->reactorEvents[clientFd], clientFd, inet_ntoa(client.sin_addr), htons(client.sin_port), EPOLLIN, REGISTER_NOT, apiRecvMsgCallFun, reactor);
        apiEventAdd(reactor->epollFd, &reactor->reactorEvents[clientFd]);
    } while(0);

    LOG_INFO("New connection [%s:%d:%d] in [pos:%d] at [time:%ld]", 
                inet_ntoa(client.sin_addr), htons(client.sin_port), clientFd,
                pos, reactor->reactorEvents[pos].lastActiveTime);

    return;
}

static void apiSendMsgCallFun(int fd, int event, void* ptr) {
    eplloReactor* reactor = (eplloReactor*)ptr;
    eplloUserEvent* ev = reactor->reactorEvents + fd;

    //LOG_INFO("apiSendMsgCallFun:fd:%d, ev->fd:%d", fd, ev->fd);
    /*TODO:
        原始数据写入到发送队列中去
        当发现对端可写的时候从发送队列获取数据发送；*/
    do{
        int nByte = send(ev->fd, ev->buffer, ev->length, 0);
        if (nByte > 0) {
            apiEventDeL(reactor->epollFd, ev);
            LOG_INFO("Send[fd = %d]: %s", ev->fd, ev->buffer);
            apiUserEventRegister(ev, ev->fd, ev->ip, ev->port, EPOLLIN, REGISTER_NOT, apiRecvMsgCallFun, reactor);
            apiEventAdd(reactor->epollFd, ev);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            LOG_INFO("apiSendMsgCallFun error , errno: %s", strerror(errno));
            apiEventDeL(reactor->epollFd, ev);
            close(ev->fd);
        }
    }while(0);

    return;
}

static void apiRecvMsgCallFun(int fd, int event, void* ptr) {
    eplloReactor* reactor = (eplloReactor*)ptr;
    eplloUserEvent* ev = reactor->reactorEvents + fd;

    //LOG_INFO("apiRecvMsgCallFun:fd:%d, ev->fd:%d", fd, ev->fd);

    apiEventDeL(reactor->epollFd, ev);
    /*TODO:
        将数据进行解析，解析后的数据存储到接收队列中去*/
    do {
        int nByte = recv(ev->fd, ev->buffer, MAX_BUF_SIZE, 0);
        if (nByte > 0) {
            ev->buffer[nByte] = '\0';
            ev->length = strlen(ev->buffer);
            LOG_INFO("recv[fd = %d]: %s", ev->fd, ev->buffer);
            apiUserEventRegister(ev, ev->fd, ev->ip, ev->port, EPOLLOUT, REGISTER_NOT, apiSendMsgCallFun, reactor);
            apiEventAdd(reactor->epollFd, ev);
        } else if (nByte == 0) {
            LOG_INFO("Client closed the connection , fd: %d", ev->fd);
            close(ev->fd);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            LOG_INFO("apiRecvMsgCallFun error , errno: %s", strerror(errno));
            close(ev->fd);
        }
    }while(0);

    return;
}

static int apiTimeOutRemoveClient(eplloReactor* reactor, int timeOutLimit) {
    long nowTime = time(NULL);
    int checkPos = 0;
    for (int i = 0; i <= 100; i++, checkPos++) {
        if (checkPos == MAX_EPOLL_EVENT) { 
            checkPos = 0;
        }
        
        //没有注册过的cfd
        if (reactor->reactorEvents[checkPos].mask == REGISTER_NOT) {
            continue;
        }

        //如果活跃的时间低于60s，主动关闭客户端的连接
        long durTime = nowTime - reactor->reactorEvents[checkPos].lastActiveTime;
        if (durTime >= timeOutLimit && reactor->reactorEvents[checkPos].fd != reactor->listenFd) {
            LOG_INFO("客户端[%s:%d]in [fd: %d]超时连接", 
                reactor->reactorEvents[checkPos].ip,
                reactor->reactorEvents[checkPos].port,
                reactor->reactorEvents[checkPos].fd)
            apiEventDeL(reactor->epollFd, &reactor->reactorEvents[checkPos]);
            close(reactor->reactorEvents[checkPos].fd);
        }
    }
}

static int apiRun(eplloReactor* reactor){
    if (reactor == NULL || reactor->reactorEvents == NULL || reactor->epollFd < 0) {
        LOG_INFO("apiRun error, errno:%s", strerror(errno));
        return -1;
    }

    struct epoll_event events[MAX_EPOLL_EVENT + 1];
    int timeOutLimit = 5;
    while (true) {
        //超时
        apiTimeOutRemoveClient(reactor, timeOutLimit);
        int nEventReady = epoll_wait(reactor->epollFd, events, MAX_EPOLL_EVENT, 1000);
        if (nEventReady < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                LOG_INFO("epoll_wait error , erron: %s", strerror(errno));
                return -1;
            }
        //超时
        } else if (nEventReady == 0) {
            continue;
        } else {
            for (int i = 0; i < nEventReady; i++) {
                eplloUserEvent* ev = (eplloUserEvent*)events[i].data.ptr;
                if ((ev->event & EPOLLIN) && (events[i].events & EPOLLIN)) {
                    ev->backFun(ev->fd, ev->event, ev->arg);
                }
                if ((ev->event & EPOLLOUT) && (events[i].events & EPOLLOUT)) {
                    ev->backFun(ev->fd, ev->event, ev->arg);
                }
            }
        }
    }

    return 0;
}

static int apiFree(eplloReactor* reactor) {
    if (reactor == NULL || reactor->reactorEvents == NULL) {
        LOG_INFO("reactorArg is NULL, errno:%s", strerror(errno));
        return -1;
    }
    close(reactor->epollFd);
    free(reactor->reactorEvents);
    free(reactor);

    return 0;
}