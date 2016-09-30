#ifndef _TUCUBE_TCP_EPOLL_H
#define _TUCUBE_TCP_EPOLL_H

#include <tucube/tucube_Module.h>
#include <tucube/tucube_ClData.h>
#include <libgonc/gonc_list.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

struct tucube_tcp_epoll_Module {
    int (*tucube_tcp_epoll_Module_init)(struct tucube_Module_Args*, struct tucube_Module_List*);
    int (*tucube_tcp_epoll_Module_tlInit)(struct tucube_Module*, struct tucube_Module_Args*);
    int (*tucube_tcp_epoll_Module_clInit)(struct tucube_Module*, struct tucube_ClData_List*, int*);
    int (*tucube_tcp_epoll_Module_service)(struct tucube_Module*, struct tucube_ClData*);
    int (*tucube_tcp_epoll_Module_clDestroy)(struct tucube_Module*, struct tucube_ClData*);
    int (*tucube_tcp_epoll_Module_tlDestroy)(struct tucube_Module*);
    int (*tucube_tcp_epoll_Module_destroy)(struct tucube_Module*);
    struct itimerspec clientTimeout;
};

struct tucube_tcp_epoll_TlModule {
    struct epoll_event* epollEventArray;
    int epollEventArraySize;
    int* clientSocketArray;
    int* clientTimerFdArray;
    struct tucube_ClData_List** clDataListArray;
    int clientArraySize;
};

int tucube_Module_init(struct tucube_Module_Args* moduleArgs, struct tucube_Module_List* moduleList);
int tucube_Module_tlInit(struct tucube_Module* module, struct tucube_Module_Args* moduleArgs);
int tucube_Module_start(struct tucube_Module* module, int* serverSocket, pthread_mutex_t* serverSocketMutex);
int tucube_Module_tlDestroy(struct tucube_Module* module);
int tucube_Module_destroy(struct tucube_Module* module);

#endif
