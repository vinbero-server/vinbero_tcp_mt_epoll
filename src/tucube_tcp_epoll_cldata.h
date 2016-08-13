#ifndef _TUCUBE_TCP_EPOLL_CLDATA_H
#define _TUCUBE_TCP_EPOLL_CLDATA_H

struct tucube_tcp_epoll_cldata
{
    void* data;
    GONC_LIST_ELEMENT(struct tucube_tcp_epoll_cldata);
};

struct tucube_tcp_epoll_cldata_list
{
    GONC_LIST(struct tucube_tcp_epoll_cldata);
};

#endif
