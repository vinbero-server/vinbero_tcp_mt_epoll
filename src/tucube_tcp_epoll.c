#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <tucube/tucube_module.h>
#include <tucube/tucube_cast.h>
#include <libgonc/gonc_list.h>
#include "tucube_tcp_epoll.h"
#include "tucube_tcp_epoll_cldata.h"

int tucube_module_init(struct tucube_module_args* module_args, struct tucube_module_list* module_list)
{
    if(GONC_LIST_ELEMENT_NEXT(module_args) == NULL)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll requires another module", __FILE__, __LINE__);

    struct tucube_module* module = malloc(sizeof(struct tucube_module));
    GONC_LIST_ELEMENT_INIT(module);
    GONC_LIST_APPEND(module_list, module);

    module->pointer = malloc(sizeof(struct tucube_tcp_epoll_module));
    module->tlmodule_key = malloc(sizeof(pthread_key_t));
    pthread_key_create(module->tlmodule_key, NULL);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->dl_handle = dlopen(GONC_LIST_ELEMENT_NEXT(module_args)->module_path.chars, RTLD_LAZY)) == NULL)
        err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_init = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_init")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_init()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tlinit = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_tlinit")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_tlinit()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_clinit = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_clinit")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_clinit()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_service = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_service")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_service()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_cldestroy = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_cldestroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_cldestroy()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tldestroy = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_tldestroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_tldestroy()", __FILE__, __LINE__);

    if((TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_destroy = dlsym(TUCUBE_CAST(module->pointer,
              struct tucube_tcp_epoll_module*)->dl_handle, "tucube_tcp_epoll_module_destroy")) == NULL)
        errx(EXIT_FAILURE, "%s: %u: Unable to find tucube_tcp_epoll_module_destroy()", __FILE__, __LINE__);

    if(TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_init(GONC_LIST_ELEMENT_NEXT(module_args), module_list) == -1)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll_module_init() failed", __FILE__, __LINE__);

    return 0;
}

int tucube_module_tlinit(struct tucube_module* module, struct tucube_module_args* module_args)
{
    struct tucube_tcp_epoll_tlmodule* tlmodule = malloc(sizeof(struct tucube_tcp_epoll_tlmodule));
    int worker_count = 0;
    int worker_max_clients;
    GONC_LIST_FOR_EACH(module_args, struct tucube_module_arg, module_arg)
    {
        if(strncmp("tucube-worker-count", module_arg->name.chars, sizeof("tucube-worker-count") - 1) == 0)
        {
            worker_count = strtol(module_arg->value.chars, NULL, 10);
        }
        else if(strncmp("worker-max-clients", module_arg->name.chars, sizeof("worker-max-clients") - 1) == 0)
        {
            worker_max_clients = strtol(module_arg->value.chars, NULL, 10) + 1;
        }
    }

    if(worker_count == 0)
        errx(EXIT_FAILURE, "Argument tucube-worker-count is required");

    if(worker_max_clients < 1 || worker_max_clients == LONG_MIN || worker_max_clients == LONG_MAX)
        worker_max_clients = 1024;

    tlmodule->epoll_event_array_size = worker_max_clients + 1 + 3;
    tlmodule->epoll_event_array = malloc(sizeof(struct epoll_event) * tlmodule->epoll_event_array_size);

    tlmodule->client_array_size = worker_max_clients * worker_count + 1 + 3;

    tlmodule->client_socket_array = malloc(tlmodule->client_array_size * sizeof(int));
    memset(tlmodule->client_socket_array, -1, tlmodule->client_array_size);

    tlmodule->client_timerfd_array = malloc(tlmodule->client_array_size * sizeof(int));
    memset(tlmodule->client_timerfd_array, -1, tlmodule->client_array_size);

    tlmodule->cldata_list_array = calloc(tlmodule->client_array_size, sizeof(struct tucube_tcp_epoll_cldata_list*));

    pthread_setspecific(*module->tlmodule_key, tlmodule);
    TUCUBE_CAST(module->pointer,
         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tlinit(GONC_LIST_ELEMENT_NEXT(module), GONC_LIST_ELEMENT_NEXT(module_args));

    return 0;
}

int tucube_module_start(struct tucube_module* module, int* server_socket, pthread_mutex_t* server_socket_mutex)
{
    struct tucube_tcp_epoll_tlmodule* tlmodule = pthread_getspecific(*module->tlmodule_key);    
    fcntl(*server_socket, F_SETFL, fcntl(*server_socket, F_GETFL, 0) | O_NONBLOCK);
    int epoll_fd = epoll_create1(0);
    struct epoll_event epoll_event;
    memset(&epoll_event, 0, sizeof(struct epoll_event)); // to avoid valgrind warning: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epoll_event.events = EPOLLIN | EPOLLET;
    epoll_event.data.fd = *server_socket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *server_socket, &epoll_event);

    struct itimerspec client_itimerspec;
    client_itimerspec.it_value.tv_sec = 10;
    client_itimerspec.it_value.tv_nsec = 0;
    client_itimerspec.it_interval.tv_sec = 10;
    client_itimerspec.it_interval.tv_nsec = 0;

    for(int client_socket, epoll_event_count, mutex_trylock_result, module_service_result;;)
    {
        if((epoll_event_count = epoll_wait(epoll_fd, tlmodule->epoll_event_array, tlmodule->epoll_event_array_size, -1)) == -1)
            err(EXIT_FAILURE, "%s: %u", __FILE__, __LINE__);
        for(int index = 0; index < epoll_event_count; ++index)
        {
            warnx("this fd: %d, server_socket: %d", tlmodule->epoll_event_array[index].data.fd, *server_socket);
            if(tlmodule->epoll_event_array[index].data.fd == *server_socket)
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

                if(client_socket > (tlmodule->client_array_size - 1))
                {
                    close(client_socket);
                    continue;
                }

                tlmodule->cldata_list_array[client_socket] = malloc(sizeof(struct tucube_tcp_epoll_cldata_list));
                GONC_LIST_INIT(tlmodule->cldata_list_array[client_socket]);
                TUCUBE_CAST(module->pointer,
                     struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_clinit(GONC_LIST_ELEMENT_NEXT(module),
                          tlmodule->cldata_list_array[client_socket], client_socket);

                fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = client_socket;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &epoll_event);

                if((tlmodule->client_timerfd_array[client_socket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                tlmodule->client_socket_array[tlmodule->client_timerfd_array[client_socket]] = client_socket;

                fcntl(tlmodule->client_timerfd_array[client_socket], F_SETFL, fcntl(tlmodule->client_timerfd_array[client_socket], F_GETFL, 0) | O_NONBLOCK);
                epoll_event.events = EPOLLIN | EPOLLET;
                epoll_event.data.fd = tlmodule->client_timerfd_array[client_socket];
                if(timerfd_settime(tlmodule->client_timerfd_array[client_socket], 0, &client_itimerspec, NULL) == -1)
                    warn("%s: %u", __FILE__, __LINE__);
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tlmodule->client_timerfd_array[client_socket], &epoll_event);
            }
            else if(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd] != -1 && tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd] == -1)
            {
                if(tlmodule->epoll_event_array[index].events & EPOLLIN)
                {
                    if(timerfd_settime(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd], 0, &client_itimerspec, NULL) == -1)
                        warn("%s: %u", __FILE__, __LINE__);

                    if((module_service_result = TUCUBE_CAST(module->pointer,
                         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_service(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]))) == 0)
                    {
                        TUCUBE_CAST(module->pointer,
                             struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_cldestroy(GONC_LIST_ELEMENT_NEXT(module),
                                  GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]));

                        free(tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd]);
                        close(tlmodule->epoll_event_array[index].data.fd);
                        close(tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd]);

                        tlmodule->cldata_list_array[tlmodule->epoll_event_array[index].data.fd] = NULL;
                        tlmodule->client_socket_array[tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd]] = -1;
                        tlmodule->client_timerfd_array[tlmodule->epoll_event_array[index].data.fd] = -1;
                    }
                    else if(module_service_result == -1)
                        warnx("%s: %u: tucube_tcp_epoll_module_service() failed", __FILE__, __LINE__);
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

                    TUCUBE_CAST(module->pointer,
                         struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_cldestroy(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlmodule->cldata_list_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]]));

                    free(tlmodule->cldata_list_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]]);
                    close(tlmodule->epoll_event_array[index].data.fd);
                    close(tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]);

                    tlmodule->cldata_list_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]] = NULL;
                    tlmodule->client_timerfd_array[tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd]] = -1;
                    tlmodule->client_socket_array[tlmodule->epoll_event_array[index].data.fd] = -1;
                }
            }
            else
                errx(EXIT_FAILURE, "%s: %u: Unexpected file descriptor: %d", __FILE__, __LINE__, tlmodule->epoll_event_array[index].data.fd);
        }
    }
    return 0;
}

int tucube_module_tldestroy(struct tucube_module* module)
{
    TUCUBE_CAST(module->pointer, struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_tldestroy(GONC_LIST_ELEMENT_NEXT(module));
    struct tucube_tcp_epoll_tlmodule* tlmodule = pthread_getspecific(*module->tlmodule_key);
    free(tlmodule);
    return 0;
}

int tucube_module_destroy(struct tucube_module* module)
{
    TUCUBE_CAST(module->pointer, struct tucube_tcp_epoll_module*)->tucube_tcp_epoll_module_destroy(GONC_LIST_ELEMENT_NEXT(module));
    dlclose(TUCUBE_CAST(module->pointer, struct tucube_tcp_epoll_module*)->dl_handle);
    pthread_key_delete(*module->tlmodule_key);
    free(module->tlmodule_key);
    free(module->pointer);
    free(module);
    return 0;
}
