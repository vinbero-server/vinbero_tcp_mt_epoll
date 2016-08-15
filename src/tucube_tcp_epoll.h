#ifndef _TUCUBE_TCP_EPOLL_H
#define _TUCUBE_TCP_EPOLL_H

#include <tucube/tucube_module.h>
#include <libgonc/gonc_list.h>
#include <sys/epoll.h>
#include "tucube_tcp_epoll_cldata.h"

struct tucube_tcp_epoll_module
{
    void* dl_handle;
    int (*tucube_tcp_epoll_module_init)(struct tucube_module_args*, struct tucube_module_list*);
    int (*tucube_tcp_epoll_module_tlinit)(struct tucube_module*, struct tucube_module_args*);
    int (*tucube_tcp_epoll_module_clinit)(struct tucube_module*, struct tucube_tcp_epoll_cldata_list*, int);
    int (*tucube_tcp_epoll_module_service)(struct tucube_module*, struct tucube_tcp_epoll_cldata*);
    int (*tucube_tcp_epoll_module_cldestroy)(struct tucube_module*, struct tucube_tcp_epoll_cldata*);
    int (*tucube_tcp_epoll_module_tldestroy)(struct tucube_module*);
    int (*tucube_tcp_epoll_module_destroy)(struct tucube_module*);
};

struct tucube_tcp_epoll_tlmodule
{
    struct epoll_event* epoll_event_array;
    int epoll_event_array_size;
    int* client_socket_array;
    int* client_timerfd_array;
    struct tucube_tcp_epoll_cldata_list** cldata_list_array;
    int client_array_size;
};

int tucube_module_init(struct tucube_module_args* module_args, struct tucube_module_list* module_list);
int tucube_module_tlinit(struct tucube_module* module, struct tucube_module_args* module_args);
int tucube_module_start(struct tucube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex);
int tucube_module_tldestroy(struct tucube_module* module);
int tucube_module_destroy(struct tucube_module* module);

#endif
