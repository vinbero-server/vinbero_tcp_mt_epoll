#ifndef _TUCUBE_TEPOLL_CLDATA_H
#define _TUCUBE_TEPOLL_CLDATA_H

struct tucube_tepoll_cldata
{
    void* data;
    GONC_LIST_ELEMENT(struct tucube_tepoll_cldata);
};

struct tucube_tepoll_cldata_list
{
    GONC_LIST(struct tucube_tepoll_cldata);
};

#endif
