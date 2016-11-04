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
#include <tucube/tucube_Module.h>
#include <tucube/tucube_ClData.h>
#include <libgonc/gonc_cast.h>
#include <libgonc/gonc_list.h>
#include "tucube_tcp_epoll.h"

int tucube_Module_init(struct tucube_Module_Config* moduleConfig, struct tucube_Module_List* moduleList) {
    if(GONC_LIST_ELEMENT_NEXT(moduleConfig) == NULL)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll requires another module", __FILE__, __LINE__);

    struct tucube_Module* module = malloc(1 * sizeof(struct tucube_Module));
    GONC_LIST_ELEMENT_INIT(module);
    GONC_LIST_APPEND(moduleList, module);

    module->pointer = malloc(1 * sizeof(struct tucube_tcp_epoll_Module));
    module->tlModuleKey = malloc(1 * sizeof(pthread_key_t));
    pthread_key_create(module->tlModuleKey, NULL);

    TUCUBE_MODULE_DLOPEN(module, moduleConfig);

    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_init);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_tlInit);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_clInit);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_service);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_clDestroy);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_tlDestroy);
    TUCUBE_MODULE_DLSYM(module, struct tucube_tcp_epoll_Module, tucube_tcp_epoll_Module_destroy);

    GONC_CAST(module->pointer,
            struct tucube_tcp_epoll_Module*)->clientTimeout.it_value.tv_sec =
        GONC_CAST(module->pointer,
                struct tucube_tcp_epoll_Module*)->clientTimeout.it_interval.tv_sec = 3;

    if(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.clientTimeoutSeconds") != NULL) {
        GONC_CAST(module->pointer,
                struct tucube_tcp_epoll_Module*)->clientTimeout.it_value.tv_sec =
            GONC_CAST(module->pointer,
                    struct tucube_tcp_epoll_Module*)->clientTimeout.it_interval.tv_sec =
            json_integer_value(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.clientTimeoutSeconds"));
    }

    GONC_CAST(module->pointer,
            struct tucube_tcp_epoll_Module*)->clientTimeout.it_value.tv_nsec = 
        GONC_CAST(module->pointer,
                struct tucube_tcp_epoll_Module*)->clientTimeout.it_interval.tv_nsec = 0;

    if(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.clientTimeoutNanoSeconds") != NULL) {
        GONC_CAST(module->pointer,
                struct tucube_tcp_epoll_Module*)->clientTimeout.it_value.tv_nsec =
            GONC_CAST(module->pointer,
                    struct tucube_tcp_epoll_Module*)->clientTimeout.it_interval.tv_nsec =
            json_integer_value(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.clientTimeoutNanoSeconds"));
    }

    if(GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_init(GONC_LIST_ELEMENT_NEXT(moduleConfig),
              moduleList) == -1) {
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll_Module_init() failed", __FILE__, __LINE__);
    }

    return 0;
}

int tucube_Module_tlInit(struct tucube_Module* module, struct tucube_Module_Config* moduleConfig) {
    struct tucube_tcp_epoll_TlModule* tlModule = malloc(1 * sizeof(struct tucube_tcp_epoll_TlModule));
    int workerCount = 0;
    int workerMaxClients = 0;

    if(json_object_get(json_array_get(moduleConfig->json, 1), "tucube.workerCount") != NULL)
        workerCount = json_integer_value(json_object_get(json_array_get(moduleConfig->json, 1), "tucube.workerCount"));

    if(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.workerMaxClients") != NULL)
        workerCount = json_integer_value(json_object_get(json_array_get(moduleConfig->json, 1), "tucube_tcp_epoll.workerMaxClients"));

    if(workerCount == 0) {
        warnx("%s: %u: Argument tucube.workerCount is required", __FILE__, __LINE__);
        pthread_exit(NULL);
    }

    if(workerMaxClients < 1 || workerMaxClients == LONG_MIN || workerMaxClients == LONG_MAX)
        workerMaxClients = 1024;

    tlModule->epollEventArraySize = workerMaxClients * 2 + 1; // '* 2': socket, timerfd; '+ 1': serverSocket; 
    tlModule->epollEventArray = malloc(tlModule->epollEventArraySize * sizeof(struct epoll_event));

    tlModule->clientArraySize = workerMaxClients * 2 * workerCount + 1 + 1 + 3; //'+ 1': server_socker; '+ 1': epoll_fd; '+ 3': stdin, stdout, stderr; multipliying workerCount because file descriptors are shared among threads;

    tlModule->clientSocketArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientSocketArray, -1, tlModule->clientArraySize * sizeof(int));

    tlModule->clientTimerFdArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientTimerFdArray, -1, tlModule->clientArraySize * sizeof(int));

    tlModule->clDataListArray = calloc(tlModule->clientArraySize, sizeof(struct tucube_ClData_List*));

    pthread_setspecific(*module->tlModuleKey, tlModule);

    GONC_CAST(module->pointer,
         struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_tlInit(GONC_LIST_ELEMENT_NEXT(module), GONC_LIST_ELEMENT_NEXT(moduleConfig));

    return 0;
}

int tucube_Module_start(struct tucube_Module* module, int* serverSocket, pthread_mutex_t* serverSocketMutex) {
    struct tucube_tcp_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);    
    fcntl(*serverSocket, F_SETFL, fcntl(*serverSocket, F_GETFL, 0) | O_NONBLOCK);
    int epoll_fd = epoll_create1(0);
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event)); // to avoid valgrind warning: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = *serverSocket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *serverSocket, &epollEvent);

    for(int epollEventCount;;) {
        if((epollEventCount = epoll_wait(epoll_fd, tlModule->epollEventArray, tlModule->epollEventArraySize, -1)) == -1) {
            warn("%s: %u", __FILE__, __LINE__);
            pthread_exit(NULL);
        }
        for(int index = 0; index < epollEventCount; ++index) {
            if(tlModule->epollEventArray[index].data.fd == *serverSocket) {
                int clientSocket;
                int mutexTryLockResult;
                if((mutexTryLockResult = pthread_mutex_trylock(serverSocketMutex)) != 0) {
                    if(mutexTryLockResult != EBUSY)
                        warnx("%s: %u: pthread_mutex_trylock() failed", __FILE__, __LINE__);
                    continue;
                }
                if((clientSocket = accept(*serverSocket, NULL, NULL)) == -1) {
                    if(errno != EAGAIN)
                        warn("%s: %u", __FILE__, __LINE__);
                    pthread_mutex_unlock(serverSocketMutex);
                    continue;
                }
                pthread_mutex_unlock(serverSocketMutex);

                if(clientSocket > (tlModule->clientArraySize - 1) - 1) { // '-1': room for timerfd
                    warnx("%s: %u: Unable to accept more clients", __FILE__, __LINE__);
                    close(clientSocket);
                    continue;
                }

                tlModule->clDataListArray[clientSocket] = malloc(1 * sizeof(struct tucube_ClData_List));
                GONC_LIST_INIT(tlModule->clDataListArray[clientSocket]);

                fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) | O_NONBLOCK);
                epollEvent.events = EPOLLIN | EPOLLET;
                epollEvent.data.fd = clientSocket;
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientSocket, &epollEvent) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                if((tlModule->clientTimerFdArray[clientSocket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = clientSocket;

                fcntl(tlModule->clientTimerFdArray[clientSocket], F_SETFL, fcntl(tlModule->clientTimerFdArray[clientSocket], F_GETFL, 0) | O_NONBLOCK);
                epollEvent.events = EPOLLIN | EPOLLET;
                epollEvent.data.fd = tlModule->clientTimerFdArray[clientSocket];
                if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &GONC_CAST(module->pointer, struct tucube_tcp_epoll_Module*)->clientTimeout, NULL) == -1)
                    warn("%s: %u", __FILE__, __LINE__);
                if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tlModule->clientTimerFdArray[clientSocket], &epollEvent) == -1)
                    warn("%s: %u", __FILE__, __LINE__);

                GONC_CAST(module->pointer,
                     struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_clInit(GONC_LIST_ELEMENT_NEXT(module),
                          tlModule->clDataListArray[clientSocket], &tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]);
            }
            else if(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                 tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] == -1) {
                if(tlModule->epollEventArray[index].events & EPOLLIN) {
                    if(timerfd_settime(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd],
                         0, &GONC_CAST(module->pointer, struct tucube_tcp_epoll_Module*)->clientTimeout, NULL) == -1)
                        warn("%s: %u", __FILE__, __LINE__);
                    int result;
                    if((result = GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_service(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]))) == 1) {
                        continue;
                    }
                    else if(result == -1)
                        warnx("%s: %u: tucube_tcp_epoll_Module_service() failed", __FILE__, __LINE__);

                    GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_clDestroy(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]));

                    free(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]);
                    close(tlModule->epollEventArray[index].data.fd);
                    close(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]);

                    tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd] = NULL;
                    tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]] = -1;
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] = -1;
                }
            }
            else if(tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] == -1) {
                //warnx("%s: %u: Client timeout", __FILE__, __LINE__);
                if(tlModule->epollEventArray[index].events & EPOLLIN) {
                    uint64_t clientTimerFdValue;
                    read(tlModule->epollEventArray[index].data.fd, &clientTimerFdValue, sizeof(uint64_t));
                    int clientSocket = tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd];
                    GONC_CAST(module->pointer,
                         struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_clDestroy(GONC_LIST_ELEMENT_NEXT(module),
                              GONC_LIST_HEAD(tlModule->clDataListArray[clientSocket]));
                    free(tlModule->clDataListArray[clientSocket]);
                    close(tlModule->epollEventArray[index].data.fd);
                    close(tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd]);
                    tlModule->clDataListArray[clientSocket] = NULL;
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] = -1;
                }
            }
            else {
                warnx("%s: %u: Unexpected file descriptor: %d", __FILE__, __LINE__, tlModule->epollEventArray[index].data.fd); // sometimes(when requests are too many) happens
                close(tlModule->epollEventArray[index].data.fd); // not sure
            }
        }
    }
    return 0;
}

int tucube_Module_tlDestroy(struct tucube_Module* module) {
    GONC_CAST(module->pointer, struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_tlDestroy(GONC_LIST_ELEMENT_NEXT(module));
    struct tucube_tcp_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);
    if(tlModule != NULL) {
        free(tlModule->epollEventArray);
        free(tlModule->clientSocketArray);
        free(tlModule->clientTimerFdArray);
        for(size_t index = 0; index != tlModule->clientArraySize; ++index)
            free(tlModule->clDataListArray[index]);
        free(tlModule->clDataListArray);
        free(tlModule);
    }

    return 0;
}

int tucube_Module_destroy(struct tucube_Module* module) {
    GONC_CAST(module->pointer, struct tucube_tcp_epoll_Module*)->tucube_tcp_epoll_Module_destroy(GONC_LIST_ELEMENT_NEXT(module));
//    dlclose(module->dl_handle);
    pthread_key_delete(*module->tlModuleKey);
    free(module->tlModuleKey);
    free(module->pointer);
    free(module);
    return 0;
}
