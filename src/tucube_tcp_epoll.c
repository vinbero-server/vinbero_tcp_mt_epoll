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
#include <tucube/tucube_IBase.h>
#include <tucube/tucube_ITlService.h>
#include <tucube/tucube_ICLocal.h>
#include <tucube/tucube_IClService.h>
#include <libgenc/genc_cast.h>
#include <libgenc/genc_Tree.h>
#include <gaio.h>

struct tucube_tcp_epoll_Interface {
    TUCUBE_ICLOCAL_FUNCTION_POINTERS;
    TUCUBE_ICLSERVICE_FUNCTION_POINTERS;
};

struct tucube_tcp_epoll_Module {
    struct itimerspec clientTimeout;
};

struct tucube_tcp_epoll_TlModule {
    struct epoll_event* epollEventArray;
    int epollEventArraySize;
    int* clientSocketArray;
    int* clientTimerFdArray;
    struct tucube_ClData_List** clDataListArray;
    int clientArraySize;
};

TUCUBE_IBASE_FUNCTIONS;
TUCUBE_ITLSERVICE_FUNCTIONS;

int tucube_IBase_init(struct tucube_Module* module, struct tucube_Config* config, void* args[]) {
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_Module_Names childModuleNames;
    GENC_ARRAY_LIST_INIT(&childModuleNames);
    TUCUBE_MODULE_GET_CHILD_MODULE_NAMES(config, module->name, &childModuleNames);
    if(GENC_ARRAY_LIST_SIZE(&childModuleNames) == 0)
        errx(EXIT_FAILURE, "%s: %u: tucube_tcp_epoll requires next modules", __FILE__, __LINE__);

    module->localModule.pointer = malloc(1 * sizeof(struct tucube_tcp_epoll_Module));
    module->tlModuleKey = malloc(1 * sizeof(pthread_key_t));
    pthread_key_create(module->tlModuleKey, NULL);

    TUCUBE_ICLOCAL_DLSYM(module, struct tucube_tcp_epoll_Module);
    TUCUBE_ICLSERVICE_DLSYM(module, struct tucube_tcp_epoll_Module);

    struct tucube_tcp_epoll_TlModule* localModule = module->localModule.pointer;

    TUCUBE_MODULE_GET_CONFIG(config, "tucube_tcp_epoll.clientTimeoutSeconds", integer, &(localModule->clientTimeout.it_value.tv_sec), 3);
    TUCUBE_MODULE_GET_CONFIG(config, "tucube_tcp_epoll.clientTimeoutSeconds", integer, &(localModule->clientTimeout.it_interval.tv_sec), 3);
    TUCUBE_MODULE_GET_CONFIG(config, "tucube_tcp_epoll.clientTimeoutNanoSeconds", integer, &(localModule->clientTimeout.it_value.tv_nsec), 0);
    TUCUBE_MODULE_GET_CONFIG(config, "tucube_tcp_epoll.clientTimeoutNanoSeconds", integer, &(localModule->clientTimeout.it_interval.tv_nsec), 0);

    return 0;
}

int tucube_IBase_tlInit(struct tucube_Module* module, struct tucube_Module_Config* config, void* args[]) {
    struct tucube_tcp_epoll_TlModule* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_tcp_epoll_TlModule* tlModule = malloc(1 * sizeof(struct tucube_tcp_epoll_TlModule));
    int workerCount = 0;
    int workerMaxClients = 0;

    if(json_object_get(json_array_get(config->json, 1), "tucube.workerCount") != NULL)
        workerCount = json_integer_value(json_object_get(json_array_get(config->json, 1), "tucube.workerCount"));

    if(json_object_get(json_array_get(config->json, 1),
    "tucube_tcp_epoll.workerMaxClients") != NULL)
        workerCount = json_integer_value(json_object_get(json_array_get(config->json, 1),
	"tucube_tcp_epoll.workerMaxClients"));

    if(workerCount == 0) {
        warnx("%s: %u: Argument tucube.workerCount is required", __FILE__, __LINE__);
        pthread_exit(NULL);
    }

    if(workerMaxClients < 1 || workerMaxClients == LONG_MIN || workerMaxClients == LONG_MAX)
        workerMaxClients = 1024;

    tlModule->epollEventArraySize = workerMaxClients * 2 + 1; // '* 2': socket, timerfd; '+ 1': serverSocket; 
    tlModule->epollEventArray = malloc(tlModule->epollEventArraySize * sizeof(struct epoll_event));

    tlModule->clientArraySize = workerMaxClients * 2 * workerCount + 1 + 1 + 3; //'+ 1': serverSocket; '+ 1': epollFd; '+ 3': stdin, stdout, stderr; multipliying workerCount because file descriptors are shared among threads;

    tlModule->clientSocketArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientSocketArray, -1, tlModule->clientArraySize * sizeof(int));

    tlModule->clientTimerFdArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientTimerFdArray, -1, tlModule->clientArraySize * sizeof(int));

    tlModule->clDataListArray = calloc(tlModule->clientArraySize, sizeof(struct tucube_ClData_List*));

    pthread_setspecific(*module->tlModuleKey, tlModule);

    localModule->tucube_IBase_tlInit(GENC_LIST_ELEMENT_NEXT(module), GENC_LIST_ELEMENT_NEXT(config), (void*[]){NULL});

    return 0;
}

int tucube_ITlService_call(struct tucube_Module* module, void* args[]) {
    struct tucube_tcp_epoll_TlModule* localModule = module->localModule.pointer;
    int* serverSocket = (int*)args[0];
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct tucube_tcp_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);    
    if(fcntl(*serverSocket, F_SETFL, fcntl(*serverSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        warn("%s: %u", __FILE__, __LINE__);
        pthread_exit(NULL);
    }
    int epollFd = epoll_create1(0);
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event)); // to avoid valgrind warning: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = *serverSocket;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, *serverSocket, &epollEvent);

    for(int epollEventCount;;) {
        if((epollEventCount = epoll_wait(epollFd, tlModule->epollEventArray, tlModule->epollEventArraySize, -1)) == -1) {
            warn("%s: %u", __FILE__, __LINE__);
            pthread_exit(NULL);
        }
        for(int index = 0; index < epollEventCount; ++index) {
            if(tlModule->epollEventArray[index].data.fd == *serverSocket) { // serverSocket
                int clientSocket;
                if((clientSocket = accept(*serverSocket, NULL, NULL)) == -1) {
                    if(errno != EAGAIN)
                        warn("%s: %u", __FILE__, __LINE__);
                    continue;
                }

                if(clientSocket > (tlModule->clientArraySize - 1) - 1) { // '-1': room for timerfd
                    warnx("%s: %u: Unable to accept more clients", __FILE__, __LINE__);
                    close(clientSocket);
                    continue;
                }

                if(fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
                    warn("%s: %u:", __FILE__, __LINE__);
                    close(clientSocket);
                    continue;
                }

                epollEvent.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP;
                epollEvent.data.fd = clientSocket;
                if(epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &epollEvent) == -1) {
                    warn("%s: %u", __FILE__, __LINE__);
                    close(clientSocket);
                    continue;
                }

                if((tlModule->clientTimerFdArray[clientSocket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
                    warn("%s: %u", __FILE__, __LINE__);
                    close(clientSocket);
                    continue;
                }

                if(fcntl(tlModule->clientTimerFdArray[clientSocket], F_SETFL, fcntl(tlModule->clientTimerFdArray[clientSocket], F_GETFL, 0) | O_NONBLOCK) == -1) {
                    warn("%s: %u", __FILE__, __LINE__);
                    close(clientSocket);
                    close(tlModule->clientTimerFdArray[clientSocket]);
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    continue;
                }

                if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &localModule->clientTimeout, NULL) == -1) {
                    warn("%s: %u", __FILE__, __LINE__);
                    close(clientSocket);
                    close(tlModule->clientTimerFdArray[clientSocket]);
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    continue;
                }

                epollEvent.events = EPOLLIN | EPOLLET;
                epollEvent.data.fd = tlModule->clientTimerFdArray[clientSocket];
                if(epoll_ctl(epollFd, EPOLL_CTL_ADD, tlModule->clientTimerFdArray[clientSocket], &epollEvent) == -1) {
                    warn("%s: %u", __FILE__, __LINE__);
                    close(clientSocket);
                    close(tlModule->clientTimerFdArray[clientSocket]);
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    continue;
                }

                tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = clientSocket;
                tlModule->clDataListArray[clientSocket] = malloc(1 * sizeof(struct tucube_ClData_List));
                GENC_LIST_INIT(tlModule->clDataListArray[clientSocket]);
		struct gaio_Io clientIo = {
		    .object.integer = tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]],
                };
		struct gaio_Methods clientIoMethods = {
		    .read = gaio_Fd_read,
		    .write = gaio_Fd_write,
		    .sendfile = gaio_Generic_sendfile,
		    .fcntl = gaio_Fd_fcntl,
		    .fstat = gaio_Fd_fstat,
		    .fileno = gaio_Fd_fileno,
		    .close = gaio_Fd_close
		};
		clientIo.methods = &clientIoMethods;

                if(localModule->tucube_ICLocal_init(
                    GENC_LIST_ELEMENT_NEXT(module),
                    tlModule->clDataListArray[clientSocket],
		    (void*[]){&clientIo, NULL}) == -1) {

                    warnx("%s: %u: tucube_ICLocal_init() failed", __FILE__, __LINE__);
                    free(tlModule->clDataListArray[clientSocket]);
                    close(clientSocket);
                    close(tlModule->clientTimerFdArray[clientSocket]);
                    tlModule->clDataListArray[clientSocket] = NULL;
                    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    continue;
                }
            }
            else if(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                 tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] == -1) { // clientSocket
                if(tlModule->epollEventArray[index].events & EPOLLIN) {
                    if(timerfd_settime(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd],
                         0, &localModule->clientTimeout, NULL) == -1)
                        warn("%s: %u", __FILE__, __LINE__);
                    int result;
                    if((result = localModule->tucube_IClService_call(GENC_LIST_ELEMENT_NEXT(module),
                              GENC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]), (void*[]){NULL})) == 1) {
                        continue;
                    }
                    else if(result == -1)
                        warnx("%s: %u: tucube_IClService_call() failed", __FILE__, __LINE__);

                    if(localModule->tucube_ICLocal_destroy(GENC_LIST_ELEMENT_NEXT(module),
                              GENC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd])) == -1) {
                        warnx("%s: %u: tucube_ICLocal_destroy() failed", __FILE__, __LINE__);
		    }

                    free(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]);

                    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]]); // to prevent double close
                    close(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]);

                    tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd] = NULL;
                    tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]] = -1;
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] = -1;
                } else if(tlModule->epollEventArray[index].events & EPOLLRDHUP) {
                    localModule->tucube_ICLocal_destroy(GENC_LIST_ELEMENT_NEXT(module),
                              GENC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]));

                    free(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]);

                    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]]); // to prevent double close
                    close(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]);

                    tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd] = NULL;
                    tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]] = -1;
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] = -1;
                } else if(tlModule->epollEventArray[index].events & EPOLLHUP) {
                    warnx("%s: %u: Error occured on a socket", __FILE__, __LINE__);
                    localModule->tucube_ICLocal_destroy(GENC_LIST_ELEMENT_NEXT(module),
                              GENC_LIST_HEAD(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]));

                    free(tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd]);

                    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]]); // to prevent double close
                    close(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]);

                    tlModule->clDataListArray[tlModule->epollEventArray[index].data.fd] = NULL;
                    tlModule->clientSocketArray[tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd]] = -1;
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] = -1;
                }
            } else if(tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] == -1) { // clientTimerFd
                if(tlModule->epollEventArray[index].events & EPOLLIN) {
                    uint64_t clientTimerFdValue;
                    read(tlModule->epollEventArray[index].data.fd, &clientTimerFdValue, sizeof(uint64_t));
                    int clientSocket = tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd];
                    localModule->tucube_ICLocal_destroy(GENC_LIST_ELEMENT_NEXT(module),
                              GENC_LIST_HEAD(tlModule->clDataListArray[clientSocket]));

                    free(tlModule->clDataListArray[clientSocket]);

                    close(tlModule->epollEventArray[index].data.fd);
                    close(tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd]);

                    tlModule->clDataListArray[clientSocket] = NULL;
                    tlModule->clientTimerFdArray[clientSocket] = -1;
                    tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] = -1;
                }
            } else {
                warnx("%s: %u: Unexpected file descriptor %d", __FILE__, __LINE__, tlModule->epollEventArray[index].data.fd); // This shouldn't happen at all
		warnx("%s: %u: %d %d", __FILE__, __LINE__, tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd], tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd]);
                pthread_exit(NULL);
            }
        }
    }
    return 0;
}

int tucube_IBase_tlDestroy(struct tucube_Module* module) {
struct tucube_tcp_epoll_TlModule* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    localModule->tucube_IBase_tlDestroy(GENC_LIST_ELEMENT_NEXT(module));
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

int tucube_IBase_destroy(struct tucube_Module* module) {
struct tucube_tcp_epoll_TlModule* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    localModule->tucube_IBase_destroy(GENC_LIST_ELEMENT_NEXT(module));
//    dlclose(module->dl_handle);
    pthread_key_delete(*module->tlModuleKey);
    free(module->tlModuleKey);
    free(module->localModule.pointer);
    free(module);
    return 0;
}
