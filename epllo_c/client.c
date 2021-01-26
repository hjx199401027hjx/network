#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "log.h"
#include "epoll.h"

#define MAX_BUFFER		128
#define MAX_EPOLLSIZE	(384*1024)

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

int isContinue = 0;

static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}

static int ntySetReUseAddr(int fd) {
	//在重新启动服务器程序前，它需要在 1~4 分钟。这就是很多网络服务器程序被杀死后不能够马上重新启动的原因（错误提示为“ Address already in use ”），设置这个之后可以马上启动
	int reuse = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
}

int main(int argc, char **argv) {
	if (argc <= 2) {
		LOG_INFO("Usage: %s ip port", argv[0]);
		exit(0);
	}

	// 获取要连接的服务端的ip和端口
	const char *ip = argv[1];
	int port = atoi(argv[2]);
	
	int connections = 0;
	char buffer[128] = {0};
	int i = 0, index = 0;

	// 创建epoll
	struct epoll_event events[MAX_EPOLLSIZE];
	int epoll_fd = epoll_create(MAX_EPOLLSIZE);
	
	strcpy(buffer, " Data From MulClient\n");
		
	// 初始化服务器地址
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(ip);

	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	while (1) 
	{	
		struct epoll_event ev;
		int sockfd = 0;

		// 如果连接数小于340000，继续连接服务器
		if (connections < 340000 && !isContinue) 
		{
			sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if (sockfd == -1) {
				perror("socket");
				goto err;
			}

			//ntySetReUseAddr(sockfd);
			addr.sin_port = htons(port);
			
			// 连接服务器
			if (connect(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
				perror("connect");
				goto err;
			}
			
			ntySetNonblock(sockfd);  // 将套接字设置为非阻塞
			ntySetReUseAddr(sockfd); // 设置可重用本地地址

			// 向服务器发送数据
			sprintf(buffer, "Hello Server: client --> %d\n", connections);
			send(sockfd, buffer, strlen(buffer), 0);

			// 将套接字设置为可读可写，然后加入到epoll_wait()中
			ev.data.fd = sockfd;
			ev.events = EPOLLIN | EPOLLET;
			epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);
		
			connections ++;
		}
		
		// 如果每连接了一千个客户端或者连接数超过340000，那么就执行这个条件
		if (connections % 1000 == 999 || connections >= 340000) 
		{
			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);

			// 打印一下每连接1000个客户端所消耗的时间
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			LOG_INFO("connections: %d, sockfd:%d, time_used:%d", connections, sockfd, time_used);

			// 进行epoll_wait()
			int nfds = epoll_wait(epoll_fd, events, connections, 100);
			for (i = 0;i < nfds;i ++) 
			{
				int clientfd = events[i].data.fd;

				//LOG_INFO("clientfd:%d, events:%d", clientfd, events[i].events);

				// 执行写
				if (events[i].events & EPOLLOUT) {
					sprintf(buffer, "data from %d\n", clientfd);
					send(sockfd, buffer, strlen(buffer), 0);
					
					ev.data.fd=sockfd;
               	 	ev.events=EPOLLIN | EPOLLET;
                	epoll_ctl(epoll_fd, EPOLL_CTL_MOD,sockfd,&ev);
				} 
				// 执行读
				else if (events[i].events & EPOLLIN) {
					char rBuffer[MAX_BUFFER] = {0};				
					ssize_t length = recv(sockfd, rBuffer, MAX_BUFFER, 0);
					if (length > 0) {
						LOG_INFO(" RecvBuffer:%s", rBuffer);

						if (!strcmp(rBuffer, "quit")) {
							isContinue = 0;
						}
						
					} else if (length == 0) {
						LOG_INFO(" Disconnect clientfd:%d", clientfd);
						connections --;
						close(clientfd);
					} else {
						if (errno == EINTR) continue;
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							continue;
						}	

						LOG_INFO(" Error clientfd:%d, errno:%s", clientfd, strerror(errno));
						close(clientfd);
					}
					ev.data.fd=sockfd;
               	 	ev.events=EPOLLOUT | EPOLLET;
                	epoll_ctl(epoll_fd, EPOLL_CTL_MOD,sockfd,&ev);

				} else {
					LOG_INFO(" clientfd:%d, errno:%s", clientfd, strerror(errno));
					close(clientfd);
				}
			}
		}

		// 休眠1000微秒(0.01秒)
		usleep(1 * 1000);
	}

	return 0;

err:
	printf("error : %s\n", strerror(errno));
	return 0;
	
}