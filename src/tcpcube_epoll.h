#ifndef _TCPCUBE_EPOLL_H
#define _TCPCUBE_EPOLL_H

#include <tcpcube/tcpcube_module.h>
#include <libgonc/gonc_list.h>

struct tcpcube_epoll_cldata
{
    void* data;
    GONC_LIST_ELEMENT(struct tcpcube_epoll_cldata);
};

struct tcpcube_epoll_cldata_list
{
    GONC_LIST(struct tcpcube_epoll_cldata);
};

struct tcpcube_epoll_module
{
    void* dl_handle;
    int (*tcpcube_epoll_module_init)(struct tcpcube_module_args*, struct tcpcube_module_list*);
    int (*tcpcube_epoll_module_tlinit)(struct tcpcube_module*, struct tcpcube_module_args*);
    int (*tcpcube_epoll_module_clinit)(struct tcpcube_epoll_cldata_list*, int);
    int (*tcpcube_epoll_module_service)(struct tcpcube_module*, struct tcpcube_epoll_cldata*);
    int (*tcpcube_epoll_module_cldestroy)(struct tcpcube_epoll_cldata*);
    int (*tcpcube_epoll_module_tldestroy)(struct tcpcube_module*);
    int (*tcpcube_epoll_module_destroy)(struct tcpcube_module*);
    pthread_key_t* tlmodule_key;
};

struct tcpcube_epoll_tlmodule
{
    int* client_socket_array;
    size_t client_socket_array_size;
    int* timerfd_array;
    size_t timerfd_array_size;
    struct tcpcube_epoll_cldata* cldata_list_array;
    size_t cldata_array_size;
};


int tcpcube_module_init(struct tcpcube_module_args* module_args, struct tcpcube_module_list* module_list);
int tcpcube_module_tlinit(struct tcpcube_module* module, struct tcpcube_module_args* module_args);
int tcpcube_module_start(struct tcpcube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex);
int tcpcube_module_tldestroy(struct tcpcube_module* module);
int tcpcube_module_destroy(struct tcpcube_module* module);

#endif
