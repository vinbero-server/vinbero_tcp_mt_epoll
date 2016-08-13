#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <tcpcube/tcpcube_module.h>
#include <libgonc/gonc_list.h>
#include "tcpcube_epoll.h"

int tcpcube_module_init(struct tcpcube_module_args* module_args, struct tcpcube_module_list* module_list)
{
    if(GONC_LIST_ELEMENT_NEXT(module_args) == NULL)
        errx(EXIT_FAILURE, "%s: %u: tcpcube_epoll requires another module", __FILE__, __LINE__);

    struct tcpcube_module* module = malloc(sizeof(struct tcpcube_module));
    GONC_LIST_ELEMENT_INIT(module);
    GONC_LIST_APPEND(module_list, module);

    module->object = malloc(sizeof(struct tcpcube_epoll_module));
    module->object_size = sizeof(struct tcpcube_epoll_module);
    TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->tlmodule_key = malloc(sizeof(pthread_key_t));
    pthread_key_create(TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->tlmodule_key, NULL);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->dl_handle = dlopen(GONC_LIST_ELEMENT_NEXT(module_args)->module_path.chars, RTLD_LAZY)) == NULL)
        err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_init = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_init")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_init()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_tlinit = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_tlinit")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_tlinit()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_clinit = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_clinit")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_clinit()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_service = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_service")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_service()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_cldestroy = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_cldestroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_cldestroy()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_tldestroy = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_tldestroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_tldestroy()", __FILE__, __LINE__);

    if((TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_destroy = dlsym(TCPCUBE_MODULE_CAST(module->object,
              struct tcpcube_epoll_module*)->dl_handle, "tcpcube_epoll_module_destroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tcpcube_epoll_module_destroy()", __FILE__, __LINE__);

    if(TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_init(GONC_LIST_ELEMENT_NEXT(module_args), module_list) == -1)
        errx(EXIT_FAILURE, "%s: %u: tcpcube_epoll_module_init() failed", __FILE__, __LINE__);

    return 0;
}

int tcpcube_module_tlinit(struct tcpcube_module* module, struct tcpcube_module_args* module_args)
{
    pthread_setspecific(*TCPCUBE_MODULE_CAST(module->object,
        struct tcpcube_epoll_module*)->tlmodule_key, malloc(sizeof(struct tcpcube_epoll_tlmodule)));

    TCPCUBE_MODULE_CAST(module->object,
         struct tcpcube_epoll_module*)->tcpcube_epoll_module_tlinit(GONC_LIST_ELEMENT_NEXT(module), module_args);
    return 0;
}

int tcpcube_module_start(struct tcpcube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex)
{
    int* client_socket_array = malloc(1024 * sizeof(int));
    memset(client_socket_array, -1, 1024);
    int* client_timerfd_array = malloc(1024 * sizeof(int));
    memset(client_timerfd_array, -1, 1024);
    struct tcpcube_epoll_cldata_list** cldata_list_array = calloc(1024, sizeof(struct tcpcube_epoll_cldata_list*));

    fcntl(*server_socket, F_SETFL, fcntl(*server_socket, F_GETFL, 0) | O_NONBLOCK);
    int epoll_fd = epoll_create1(0);
    struct epoll_event epoll_event;
    epoll_event.events = EPOLLIN | EPOLLET;
    epoll_event.data.fd = *server_socket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *server_socket, &epoll_event);
    int epoll_max_events = 1024;
    struct epoll_event *epoll_events = malloc(sizeof(struct epoll_event) * epoll_max_events);

    struct itimerspec client_itimerspec;
    client_itimerspec.it_value.tv_sec = 10;
    client_itimerspec.it_value.tv_nsec = 0;
    client_itimerspec.it_interval.tv_sec = 10;
    client_itimerspec.it_interval.tv_nsec = 0;

    for(int client_socket, epoll_event_count, mutex_trylock_result, module_service_result;;)
    {
        if((epoll_event_count = epoll_wait(epoll_fd, epoll_events, epoll_max_events, -1)) == -1)
            err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);
        for(int index = 0; index < epoll_event_count; ++index)
        {
            if(epoll_events[index].data.fd == *server_socket)
            {
                if((mutex_trylock_result = pthread_mutex_trylock(server_socket_mutex)) != 0)
                {
                    if(mutex_trylock_result != EBUSY)
                        warnx("%s: %u: pthread_mutex_trylock() failed", __FILE__, __LINE__);
                    continue;
                }
                if((client_socket = accept(*server_socket, NULL, NULL)) == -1)
                {
                    if(errno != EAGAIN)
                        warn("%s: %u", __FILE__, __LINE__);
                    pthread_mutex_unlock(server_socket_mutex);
                    continue;
                }
                pthread_mutex_unlock(server_socket_mutex);

                cldata_list_array[client_socket] = malloc(sizeof(struct tcpcube_epoll_cldata_list));
                GONC_LIST_INIT(cldata_list_array[client_socket]);
                TCPCUBE_MODULE_CAST(module->object,
                     struct tcpcube_epoll_module*)->tcpcube_epoll_module_clinit(cldata_list_array[client_socket], client_socket);

                fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = client_socket;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &epoll_event);

                if((client_timerfd_array[client_socket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                client_socket_array[client_timerfd_array[client_socket]] = client_socket;

                fcntl(client_timerfd_array[client_socket], F_SETFL, fcntl(client_timerfd_array[client_socket], F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = client_timerfd_array[client_socket];
                if(timerfd_settime(client_timerfd_array[client_socket], 0, &client_itimerspec, NULL) == -1)
                    warn("%s: %u", __FILE__, __LINE__);
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_timerfd_array[client_socket], &epoll_event);
            }
            else if(client_timerfd_array[epoll_events[index].data.fd] != -1 && client_socket_array[epoll_events[index].data.fd] == -1)
            {
                if(epoll_events[index].events & EPOLLIN)
                {
                    if(timerfd_settime(client_timerfd_array[epoll_events[index].data.fd], 0, &client_itimerspec, NULL) == -1)
                        warn("%s: %u", __FILE__, __LINE__);

                    if((module_service_result = TCPCUBE_MODULE_CAST(module->object,
                         struct tcpcube_epoll_module*)->tcpcube_epoll_module_service(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(cldata_list_array[epoll_events[index].data.fd]))) == 0)
                    {
                        close(client_timerfd_array[epoll_events[index].data.fd]);
                        close(epoll_events[index].data.fd);
                        TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->tcpcube_epoll_module_cldestroy(
                             GONC_LIST_HEAD(cldata_list_array[epoll_events[index].data.fd]));
                        free(cldata_list_array[epoll_events[index].data.fd]);
                        client_timerfd_array[epoll_events[index].data.fd] = -1;
                        client_socket_array[client_timerfd_array[epoll_events[index].data.fd]] = -1;
                        cldata_list_array[epoll_events[index].data.fd] = NULL;
                    }
                    else if(module_service_result == -1)
                        warnx("%s: %u: tcpcube_epoll_module_service() failed", __FILE__, __LINE__);
                }
            }
            else if(client_socket_array[epoll_events[index].data.fd] != -1 && client_timerfd_array[epoll_events[index].data.fd] == -1)
            {
                warnx("%s: %u: Client timed out", __FILE__, __LINE__);
                if(epoll_events[index].events & EPOLLIN)
                {
                    uint64_t client_timerfd_value;
                    read(epoll_events[index].data.fd, &client_timerfd_value, sizeof(uint64_t));
                    close(epoll_events[index].data.fd);
                    close(client_socket_array[epoll_events[index].data.fd]);
                    TCPCUBE_MODULE_CAST(module->object,
                         struct tcpcube_epoll_module*)->tcpcube_epoll_module_cldestroy(GONC_LIST_HEAD(cldata_list_array[client_socket_array[epoll_events[index].data.fd]]));
                    free(cldata_list_array[client_socket_array[epoll_events[index].data.fd]]);
                    client_timerfd_array[client_socket_array[epoll_events[index].data.fd]] = -1;
                    client_socket_array[epoll_events[index].data.fd] = -1;
                    cldata_list_array[client_socket_array[epoll_events[index].data.fd]] = NULL;
                }
            }
            else
                warnx("%s: %u: Unexpected file descriptor: %d", __FILE__, __LINE__, epoll_events[index].data.fd);
        }
    }
    return 0;
}

int tcpcube_module_tldestroy(struct tcpcube_module* module)
{
    TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->tcpcube_epoll_module_tldestroy(GONC_LIST_ELEMENT_NEXT(module));
    warnx("tcpcube_module_tldestroy()");
    return 0;
}

int tcpcube_module_destroy(struct tcpcube_module* module)
{
    TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->tcpcube_epoll_module_destroy(GONC_LIST_ELEMENT_NEXT(module));
    dlclose(TCPCUBE_MODULE_CAST(module->object, struct tcpcube_epoll_module*)->dl_handle);
    free(module->object);
    free(module);
    return 0;
}
