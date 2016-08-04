#ifndef _TCPCUBE_EPOLL_H
#define _TCPCUBE_EPOLL_H

#include <tcpcube/tcpcube_module.h>

struct tcpcube_epoll_data
{
    int fd;
    void* ptr;
};

struct tcpcube_epoll_module
{
    void* dl_handle;
    int (*tcpcube_epoll_module_init)(struct tcpcube_module_args*, struct tcpcube_module_list*);
    int (*tcpcube_epoll_module_tlinit)(struct tcpcube_module*);
    int (*tcpcube_epoll_module_service)(struct tcpcube_module*, struct tcpcube_epoll_data*);
    int (*tcpcube_epoll_module_tldestroy)(struct tcpcube_module*);
    int (*tcpcube_epoll_module_destroy)(struct tcpcube_module*);
};

int tcpcube_module_init(struct tcpcube_module_args* module_args, struct tcpcube_module_list* module_list);
int tcpcube_module_tlinit(struct tcpcube_module* module);
int tcpcube_module_start(struct tcpcube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex);
int tcpcube_module_tldestroy(struct tcpcube_module* module);
int tcpcube_module_destroy(struct tcpcube_module* module);

#endif
