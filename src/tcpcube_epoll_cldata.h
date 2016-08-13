#ifndef _TCPCUBE_EPOLL_CLDATA_H
#define _TCPCUBE_EPOLL_CLDATA_H

struct tcpcube_epoll_cldata
{
    void* data;
    GONC_LIST_ELEMENT(struct tcpcube_epoll_cldata);
};

struct tcpcube_epoll_cldata_list
{
    GONC_LIST(struct tcpcube_epoll_cldata);
};

#endif
