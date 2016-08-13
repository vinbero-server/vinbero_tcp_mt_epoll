#ifndef _TUCUBE_TCP_EPOLL_H
#define _TUCUBE_TCP_EPOLL_H

#include <tucube/tucube_module.h>
#include <libgonc/gonc_list.h>
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
    pthread_key_t* tlmodule_key;
};

struct tucube_tcp_epoll_tlmodule
{
    int* client_socket_array;
    size_t client_socket_array_size;
    int* timerfd_array;
    size_t timerfd_array_size;
    struct tucube_tcp_epoll_cldata* cldata_list_array;
    size_t cldata_array_size;
};

int tucube_module_init(struct tucube_module_args* module_args, struct tucube_module_list* module_list);
int tucube_module_tlinit(struct tucube_module* module, struct tucube_module_args* module_args);
int tucube_module_start(struct tucube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex);
int tucube_module_tldestroy(struct tucube_module* module);
int tucube_module_destroy(struct tucube_module* module);

#endif
