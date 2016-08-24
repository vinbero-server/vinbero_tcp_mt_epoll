#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <tucube/tucube_module.h>
#include <tucube/tucube_cldata.h>
#include <libgonc/gonc_cast.h>
#include <libgonc/gonc_list.h>
#include "tucube_tcp_epoll.h"

int tucube_module_init(struct tucube_module_args* module_args, struct tucube_module_list* module_list)
{
    if(GONC_LIST_ELEMENT_NEXT(module_args) == NULL)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll requires another module", __FILE__, __LINE__);

    struct tucube_module* module = malloc(1 * sizeof(struct tucube_module));
    GONC_LIST_ELEMENT_INIT(module);
    GONC_LIST_APPEND(module_list, module);

    module->pointer = malloc(1 * sizeof(struct tucube_tcp_epoll_module));
    module->tlmodule_key = malloc(1 * sizeof(pthread_key_t));
    pthread_key_create(module->tlmodule_key, NULL);

    TUCUBE_MODULE_DLOPEN(module, module_args);

    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_init);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_tlinit);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_clinit);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_service);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_cldestroy);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_tldestroy);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_module, tucube_tcp_epoll_module_destroy);

    GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_sec =
              GONC_CAST(module->pointer,
                   struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_sec = -1;

    GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_nsec =
              GONC_CAST(module->pointer,
                   struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_nsec = -1;

    GONC_LIST_FOR_EACH(module_args, struct tucube_module_arg, module_arg)
    {
        if(strncmp("client-timeout-seconds", module_arg->name, sizeof("client-timeout-seconds")) == 0)
        {
            GONC_CAST(module->pointer,
                 struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_sec =
                 GONC_CAST(module->pointer,
                      struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_sec =
                           strtol(module_arg->value, NULL, 10);
        }
        else if(strncmp("client-timeout-nano-seconds", module_arg->name, sizeof("client-timeout-nano-seconds")) == 0)
        {
            GONC_CAST(module->pointer,
                 struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_nsec =
                 GONC_CAST(module->pointer,
                      struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_nsec =
                           strtol(module_arg->value, NULL, 10);
        }
    }

    if(GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_sec < 0)
    {
        GONC_CAST(module->pointer,
             struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_sec =
                  GONC_CAST(module->pointer,
                       struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_sec = 3;
    }

    if(GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_nsec < 0)
    {
        GONC_CAST(module->pointer,
             struct tucube_tcp_epoll_module*)->client_timeout.it_value.tv_nsec =
                  GONC_CAST(module->pointer,
                       struct tucube_tcp_epoll_module*)->client_timeout.it_interval.tv_nsec = 0;
    }

    if(GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_init(GONC_LIST_ELEMENT_NEXT(module_args),
              module_list) == -1)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll_module_init() failed", __FILE__, __LINE__);

    return 0;
}

int tucube_module_tlinit(struct tucube_module* module, struct tucube_module_args* module_args)
{
    struct tucube_tcp_epoll_tlmodule* tlmodule = malloc(1 * sizeof(struct tucube_tcp_epoll_tlmodule));
    int worker_count = 0;
    int worker_max_clients = 0;
    GONC_LIST_FOR_EACH(module_args, struct tucube_module_arg, module_arg)
    {
        if(strncmp("tucube-worker-count", module_arg->name, sizeof("tucube-worker-count")) == 0)
            worker_count = strtol(module_arg->value, NULL, 10);
        else if(strncmp("worker-max-clients", module_arg->name, sizeof("worker-max-clients")) == 0)
            worker_max_clients = strtol(module_arg->value, NULL, 10);
    }

    if(worker_count == 0)
    {
        warnx("%s: %u: Argument tucube-worker-count is required", __FILE__, __LINE__);
        pthread_exit(NULL);
    }

    if(worker_max_clients < 1 || worker_max_clients == LONG_MIN || worker_max_clients == LONG_MAX)
        worker_max_clients = 1024;

    tlmodule->epoll_event_array_size = worker_max_clients * 2 + 1; // '* 2': socket, timerfd; '+ 1': server_socket; 
    tlmodule->epoll_event_array = malloc(tlmodule->epoll_event_array_size * sizeof(struct epoll_event));

    tlmodule->client_array_size = worker_max_clients * 2 * worker_count + 1 + 1 + 3; //'+ 1': server_socker; '+ 1': epoll_fd; '+ 3': stdin, stdout, stderr; multipliying worker_count because file descriptors are shared among threads;

    tlmodule->client_socket_array = malloc(tlmodule->client_array_size * sizeof(int));
    memset(tlmodule->client_socket_array, -1, tlmodule->client_array_size * sizeof(int));

    tlmodule->client_timerfd_array = malloc(tlmodule->client_array_size * sizeof(int));
    memset(tlmodule->client_timerfd_array, -1, tlmodule->client_array_size * sizeof(int));

    tlmodule->cldata_list_array = calloc(tlmodule->client_array_size, sizeof(struct tucube_cldata_list*));

    pthread_setspecific(*module->tlmodule_key, tlmodule);

    GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tlinit(GONC_LIST_ELEMENT_NEXT(module), GONC_LIST_ELEMENT_NEXT(module_args));

    return 0;
}

int tucube_module_start(struct tucube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex)
{
    struct tucube_tcp_epoll_tlmodule* tlmodule = pthread_getspecific(*module->tlmodule_key);    
    fcntl(*server_socket, F_SETFL, fcntl(*server_socket, F_GETFL, 0) | O_NONBLOCK);
    int epoll_fd = epoll_create1(0);
    struct epoll_event epoll_event;
    memset(&epoll_event, 0, 1 * sizeof(struct epoll_event)); // to avoid valgrind warning: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epoll_event.events = EPOLLIN | EPOLLET;
    epoll_event.data.fd = *server_socket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *server_socket, &epoll_event);

    for(int epoll_event_count;;)
    {
        if((epoll_event_count = epoll_wait(epoll_fd, tlmodule->epoll_event_array, tlmodule->epoll_event_array_size, -1)) == -1)
        {
            warn("%s: %u", __FILE__, __LINE__);
            pthread_exit(NULL);
        }
        for(int index = 0; index < epoll_event_count; ++index)
        {
            if(tlmodule->epoll_event_array[index].data.fd == *server_socket)
            {
                int client_socket;
                int mutex_trylock_result;
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

                if(client_socket > (tlmodule->client_array_size - 1) - 1) // '-1': room for timerfd
                {
                    warnx("%s: %u: Unable to accept more clients", __FILE__, __LINE__);
                    close(client_socket);
                    continue;
                }

                tlmodule->cldata_list_array[client_socket] = malloc(1 * sizeof(struct tucube_cldata_list));
                GONC_LIST_INIT(tlmodule->cldata_list_array[client_socket]);

                fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = client_socket;
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &epoll_event) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                if((tlmodule->client_timerfd_array[client_socket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                tlmodule->client_socket_array[tlmodule->client_timerfd_array[client_socket]] = client_socket;

                fcntl(tlmodule->client_timerfd_array[client_socket], F_SETFL, fcntl(tlmodule->client_timerfd_array[client_socket], F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = tlmodule->client_timerfd_array[client_socket];
                if(timerfd_settime(tlmodule->client_timerfd_array[client_socket], 0, &GONC_CAST(module->pointer, struct tucube_tcp_epoll_module*)->client_timeout, NULL) == -1)
                    warn("%s: %u", __FILE__, __LINE__);
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tlmodule->client_timerfd_array[client_socket], &epoll_event) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                GONC_CAST(module->pointer,
                     struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_clinit(GONC_LIST_ELEMENT_NEXT(module),
                          tlmodule->cldata_list_array[client_socket], &tlmodule->client_socket_array[tlmodule->client_timerfd_array[client_socket]]);
            }
            else if(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd] != -1 &&
                 tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd] == -1)
            {
                if(tlmodule->epoll_event_array[index].events & EPOLLIN)
                {
                    if(timerfd_settime(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd],
                         0, &GONC_CAST(module->pointer, struct tucube_tcp_epoll_module*)->client_timeout, NULL) == -1)
                        warn("%s: %u", __FILE__, __LINE__);
                    int result;
                    if((result = GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_service(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]))) == 1)
                    {
                        continue;
                    }
                    else if(result == -1)
                        warnx("%s: %u: tucube_tcp_epoll_module_service() failed", __FILE__, __LINE__);

                    GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_cldestroy(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]));

                    free(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]);
                    close(tlmodule->epoll_event_array[index].data.fd);
                    close(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd]);

                    tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd] = NULL;
                    tlmodule->client_socket_array[tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd]] = -1;
                    tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd] = -1;
                }
            }
            else if(tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd] != -1 &&
                 tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd] == -1)
            {
                warnx("%s: %u: Client timed out", __FILE__, __LINE__);
                if(tlmodule->epoll_event_array[index].events & EPOLLIN)
                {
                    uint64_t client_timerfd_value;
                    read(tlmodule->epoll_event_array[index].data.fd, &client_timerfd_value, sizeof(uint64_t));
                    int client_socket = tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd];
// sometimes tlmodule->cldata_list_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]] can be NULL and causes SEGFAULT
                    GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_cldestroy(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]]));
                    free(tlmodule->cldata_list_array[client_socket]);
                    close(tlmodule->epoll_event_array[index].data.fd);
                    close(tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]);
                    tlmodule->cldata_list_array[client_socket] = NULL;
                    tlmodule->client_timerfd_array[client_socket] = -1;
                    tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd] = -1;
                }
            }
            else
            {
                warnx("%s: %u: Unexpected file descriptor: %d", __FILE__, __LINE__, tlmodule->epoll_event_array[index].data.fd);
                pthread_exit(NULL);
            }
        }
    }
    return 0;
}

int tucube_module_tldestroy(struct tucube_module* module)
{
    GONC_CAST(module->pointer, struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tldestroy(GONC_LIST_ELEMENT_NEXT(module));
    struct tucube_tcp_epoll_tlmodule* tlmodule = pthread_getspecific(*module->tlmodule_key);
    if(tlmodule != NULL)
    {
        free(tlmodule->epoll_event_array);
        free(tlmodule->client_socket_array);
        free(tlmodule->client_timerfd_array);
        for(size_t index = 0; index != tlmodule->client_array_size; ++index)
        {
            free(tlmodule->cldata_list_array[index]);
        }
        free(tlmodule->cldata_list_array);
        free(tlmodule);
    }

    return 0;
}

int tucube_module_destroy(struct tucube_module* module)
{
    GONC_CAST(module->pointer, struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_destroy(GONC_LIST_ELEMENT_NEXT(module));
//    dlclose(module->dl_handle);
    pthread_key_delete(*module->tlmodule_key);
    free(module->tlmodule_key);
    free(module->pointer);
    free(module);
    return 0;
}
