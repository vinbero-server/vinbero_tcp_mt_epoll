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
#include <vinbero/vinbero_Module.h>
#include <vinbero/vinbero_ClData.h>
#include <vinbero/vinbero_IModule.h>
#include <vinbero/vinbero_ITLocal.h>
#include <vinbero/vinbero_ITlService.h>
#include <vinbero/vinbero_ICLocal.h>
#include <vinbero/vinbero_IClService.h>
#include <libgenc/genc_args.h>
#include <libgenc/genc_cast.h>
#include <libgenc/genc_Tree.h>
#include <gaio.h>

struct vinbero_tcp_mt_epoll_Interface {
    VINBERO_ITLOCAL_FUNCTION_POINTERS;
    VINBERO_ICLOCAL_FUNCTION_POINTERS;
    VINBERO_ICLSERVICE_FUNCTION_POINTERS;
};

struct vinbero_tcp_mt_epoll_Module {
    struct itimerspec clientTimeout;
};

struct vinbero_tcp_mt_epoll_TlModule {
    struct epoll_event* epollEventArray;
    int epollEventArraySize;
    int* clientSocketArray;
    int* clientTimerFdArray;
    struct vinbero_ClData** clDataArray;
    int clientArraySize;
};

VINBERO_IMODULE_FUNCTIONS;
VINBERO_ITLOCAL_FUNCTIONS;
VINBERO_ITLSERVICE_FUNCTIONS;

int vinbero_IModule_init(struct vinbero_Module* module, struct vinbero_Config* config, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    module->name = "vinbero_tcp_mt_epoll";
    module->version = "0.0.1";
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_tcp_mt_epoll_Module));
    module->tlModuleKey = malloc(1 * sizeof(pthread_key_t));
    pthread_key_create(module->tlModuleKey, NULL);
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    VINBERO_CONFIG_GET(config, module, "vinbero_tcp_mt_epoll.clientTimeoutSeconds", integer, &(localModule->clientTimeout.it_value.tv_sec), 3);
    VINBERO_CONFIG_GET(config, module, "vinbero_tcp_mt_epoll.clientTimeoutSeconds", integer, &(localModule->clientTimeout.it_interval.tv_sec), 3);
    VINBERO_CONFIG_GET(config, module, "vinbero_tcp_mt_epoll.clientTimeoutNanoSeconds", integer, &(localModule->clientTimeout.it_value.tv_nsec), 0);
    VINBERO_CONFIG_GET(config, module, "vinbero_tcp_mt_epoll.clientTimeoutNanoSeconds", integer, &(localModule->clientTimeout.it_interval.tv_nsec), 0);

    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        childModule->interface = malloc(sizeof(struct vinbero_tcp_mt_epoll_Interface));
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        int errorVariable;
        VINBERO_ITLOCAL_DLSYM(childInterface, childModule->dlHandle, &errorVariable);
        if(errorVariable == 1) {
            warnx("module %s doesn't satisfy ITLOCAL interface", childModule->id);
            return -1;
        }
        VINBERO_ICLOCAL_DLSYM(childInterface, childModule->dlHandle, &errorVariable);
        if(errorVariable == 1) {
            warnx("module %s doesn't satisfy ICLOCAL interface", childModule->id);
            return -1;
        }
        VINBERO_ICLSERVICE_DLSYM(childInterface, childModule->dlHandle, &errorVariable);
        if(errorVariable == 1) {
            warnx("module %s doesn't satisfy ICLSERVICE interface", childModule->id);
            return -1;
        }
    }
    return 0;
}

int vinbero_IModule_rInit(struct vinbero_Module* module, struct vinbero_Config* config, void* args[]) {
    return 0;
}

int vinbero_ITLocal_init(struct vinbero_Module* module, struct vinbero_Config* config, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    struct vinbero_tcp_mt_epoll_TlModule* tlModule = malloc(1 * sizeof(struct vinbero_tcp_mt_epoll_TlModule));
    int errorVariable;
    int workerCount;
    int workerMaxClients;
    VINBERO_CONFIG_GET_REQUIRED(config, module, "vinbero_mt.workerCount", integer, &workerCount, &errorVariable);
    if(errorVariable == 1)
        return -1;
    VINBERO_CONFIG_GET(config, module, "vinbero_tcp_mt_epoll.workerMaxClients", integer, &workerMaxClients, 1024);
    tlModule->epollEventArraySize = workerMaxClients * 2 + 1; // '* 2': socket, timerfd; '+ 1': serverSocket; 
    tlModule->epollEventArray = malloc(tlModule->epollEventArraySize * sizeof(struct epoll_event));
    tlModule->clientArraySize = workerMaxClients * 2 * workerCount + 1 + 1 + 3; //'+ 1': serverSocket; '+ 1': epollFd; '+ 3': stdin, stdout, stderr; multipliying workerCount because file descriptors are shared among threads;
    tlModule->clientSocketArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientSocketArray, -1, tlModule->clientArraySize * sizeof(int));
    tlModule->clientTimerFdArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientTimerFdArray, -1, tlModule->clientArraySize * sizeof(int));
    tlModule->clDataArray = calloc(tlModule->clientArraySize, sizeof(struct vinbero_ClData*));
    pthread_setspecific(*module->tlModuleKey, tlModule);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        if(childInterface->vinbero_ITLocal_init(childModule, config, args) == -1) {
            warnx("vinbero_ITLocal_init() failed at module %s", childModule->id);
            return -1;
        }
    }
    return 0;
}

int vinbero_ITLocal_rInit(struct vinbero_Module* module, struct vinbero_Config* config, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    return 0;
}

int vinbero_tcp_mt_epoll_preInitClData(struct vinbero_Module* module, struct vinbero_ClData* clData) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    GENC_TREE_NODE_INIT_CHILDREN(clData, GENC_TREE_NODE_CHILD_COUNT(module));
    GENC_TREE_NODE_ZERO_CHILDREN(clData);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        GENC_TREE_NODE_ADD_EMPTY_CHILD(clData);
        struct vinbero_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        GENC_TREE_NODE_INIT(childClData);
        GENC_TREE_NODE_SET_PARENT(childClData, clData);
        vinbero_tcp_mt_epoll_preInitClData(childModule, childClData);
    }
}

int vinbero_tcp_mt_epoll_initClData(struct vinbero_Module* module, struct vinbero_ClData* clData, struct gaio_Io* clientIo) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        if(childInterface->vinbero_ICLocal_init(childModule, childClData, GENC_ARGS(clientIo)) == -1) {
            warnx("%s: %u: vinbero_ICLocal_init() failed", __FILE__, __LINE__);
            return -1;
        }
    }
    return 0;
}

int vinbero_tcp_mt_epoll_destroyClData(struct vinbero_Module* module, struct vinbero_ClData* clData) {
    GENC_TREE_NODE_FOR_EACH_CHILD(clData, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        if(childInterface->vinbero_ICLocal_destroy(childModule, childClData) == -1) { // destruction failed? this should be fatal!
            warnx("%s: %u: %s: vinbero_ICLocal_destroy() failed", __FILE__, __LINE__, __FUNCTION__);
            GENC_TREE_NODE_FREE_CHILDREN(clData);
            return -1;
        }
    }
    GENC_TREE_NODE_FREE_CHILDREN(clData);
    return 0;
}

static void vinbero_tcp_mt_epoll_handleConnection(struct vinbero_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int epollFd, int* serverSocket) {
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    int clientSocket;
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event));

    if((clientSocket = accept(*serverSocket, NULL, NULL)) == -1) {
        if(errno != EAGAIN)
            warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
        return;
    }
    if(clientSocket > (tlModule->clientArraySize - 1) - 1) { // '-1': room for timerfd
        warnx("%s: %u: Unable to accept more clients", __FILE__, __LINE__);
        close(clientSocket);
        return; 
    }
    if(fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        return;
    }
    epollEvent.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP;
    epollEvent.data.fd = clientSocket;
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &epollEvent) == -1) {
        warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        return;
    }
    if((tlModule->clientTimerFdArray[clientSocket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
        warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        return;
    }
    if(fcntl(tlModule->clientTimerFdArray[clientSocket], F_SETFL, fcntl(tlModule->clientTimerFdArray[clientSocket], F_GETFL, 0) | O_NONBLOCK) == -1) {
        warn("%s: %u, %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
    if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &localModule->clientTimeout, NULL) == -1) {
        warn("%s: %u, %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = tlModule->clientTimerFdArray[clientSocket];
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, tlModule->clientTimerFdArray[clientSocket], &epollEvent) == -1) {
        warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = clientSocket;
/*
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
*/
    struct gaio_Io* clientIo = malloc(sizeof(struct gaio_Io));
    clientIo->object.integer = tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]];
    clientIo->methods = malloc(sizeof(struct gaio_Methods));
    clientIo->methods->read = gaio_Fd_read;
    clientIo->methods->write = gaio_Fd_write;
    clientIo->methods->sendfile = gaio_Generic_sendfile;
    clientIo->methods->fcntl = gaio_Fd_fcntl;
    clientIo->methods->fstat = gaio_Fd_fstat;
    clientIo->methods->fileno = gaio_Fd_fileno;
    clientIo->methods->close = gaio_Fd_close;

    tlModule->clDataArray[clientSocket] = malloc(1 * sizeof(struct vinbero_ClData));
    GENC_TREE_NODE_INIT(tlModule->clDataArray[clientSocket]);
    if(vinbero_tcp_mt_epoll_preInitClData(module, tlModule->clDataArray[clientSocket]) == -1) {
        vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]); // what if this also failed? (FATAL)
        free(tlModule->clDataArray[clientSocket]);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clDataArray[clientSocket] = NULL;
        tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
        tlModule->clientTimerFdArray[clientSocket] = -1;
    }
    if(vinbero_tcp_mt_epoll_initClData(module, tlModule->clDataArray[clientSocket], clientIo) == -1) {
        vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]); // what if this also failed? (FATAL)
        free(tlModule->clDataArray[clientSocket]);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clDataArray[clientSocket] = NULL;
        tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
}

static int vinbero_tcp_mt_epoll_handleRequest(
    struct vinbero_Module* module,
    struct vinbero_tcp_mt_epoll_Module* localModule,
    struct vinbero_tcp_mt_epoll_TlModule* tlModule,
    int* serverSocket,
    int clientSocket //
) {
    if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &localModule->clientTimeout, NULL) == -1)
        warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(tlModule->clDataArray[clientSocket], index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        if(childInterface->vinbero_IClService_call(childModule, childClData, GENC_ARGS(NULL)) == -1) {
            warnx("%s: %u: vinbero_IClService_call() failed", __FILE__, __LINE__);
            break;
        }
    }
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static void vinbero_tcp_mt_epoll_handleDisconnection(struct vinbero_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int clientSocket) {
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static int vinbero_tcp_mt_epoll_handleError(struct vinbero_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int clientSocket) {
    warnx("%s: %u: Error occured on a socket", __FILE__, __LINE__);
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static int vinbero_tcp_mt_epoll_handleTimeout(struct vinbero_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int timerFd) {
    uint64_t clientTimerFdValue;
    read(timerFd, &clientTimerFdValue, sizeof(uint64_t));
    int clientSocket = tlModule->clientSocketArray[timerFd];
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(tlModule->clDataArray[clientSocket], index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        childInterface->vinbero_ICLocal_destroy(childModule, childClData);
    }
    close(timerFd);
    close(tlModule->clientSocketArray[timerFd]);
    tlModule->clientTimerFdArray[tlModule->clientSocketArray[timerFd]] = -1;
    tlModule->clientSocketArray[timerFd] = -1;

}

static void vinbero_tcp_mt_epoll_handleUnexpected(struct vinbero_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int fd) {
    warnx("%s: %u: Unexpected file descriptor %d", __FILE__, __LINE__, fd); // This shouldn't happen at all
    warnx("%s: %u: %s: %d %d", __FILE__, __LINE__, __FUNCTION__,
        tlModule->clientTimerFdArray[fd],
        tlModule->clientSocketArray[fd]);
}

int vinbero_ITlService_call(struct vinbero_Module* module, void* args[]) {
warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    int* serverSocket = (int*)args[0];
    struct vinbero_tcp_mt_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);    
    if(fcntl(*serverSocket, F_SETFL, fcntl(*serverSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        warn("%s: %u", __FILE__, __LINE__);
        return -1;
    }
    int epollFd = epoll_create1(0);
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event)); // to avoid valgrind warning: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = *serverSocket;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, *serverSocket, &epollEvent);
    for(int epollEventCount;;) {
        if((epollEventCount = epoll_wait(epollFd, tlModule->epollEventArray, tlModule->epollEventArraySize, -1)) == -1) {
            warn("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
            return -1;
        }
        for(int index = 0; index < epollEventCount; ++index) {
            if(tlModule->epollEventArray[index].data.fd == *serverSocket) // serverSocket
                vinbero_tcp_mt_epoll_handleConnection(module, tlModule, epollFd, serverSocket);
            else if(tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                      tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] == -1) { // clientSocket
                if(tlModule->epollEventArray[index].events & EPOLLIN)
                    vinbero_tcp_mt_epoll_handleRequest(module, localModule, tlModule, serverSocket, tlModule->epollEventArray[index].data.fd);
                else if(tlModule->epollEventArray[index].events & EPOLLRDHUP)
                    vinbero_tcp_mt_epoll_handleDisconnection(module, tlModule, serverSocket, tlModule->epollEventArray[index].data.fd);
                else if(tlModule->epollEventArray[index].events & EPOLLHUP)
                    vinbero_tcp_mt_epoll_handleError(module, tlModule, serverSocket, tlModule->epollEventArray[index].data.fd);
            } else if(tlModule->clientSocketArray[tlModule->epollEventArray[index].data.fd] != -1 &&
                    tlModule->clientTimerFdArray[tlModule->epollEventArray[index].data.fd] == -1 &&
                    tlModule->epollEventArray[index].events & EPOLLIN) { // clientTimerFd
                vinbero_tcp_mt_epoll_handleTimeout(module, tlModule, serverSocket, tlModule->epollEventArray[index].data.fd);
            } else {
                vinbero_tcp_mt_epoll_handleUnexpected(module, tlModule, serverSocket, tlModule->epollEventArray[index].data.fd);
                return -1;
            }
        }
    }
    return 0;
}

int vinbero_ITLocal_destroy(struct vinbero_Module* module) {
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_tcp_mt_epoll_Interface* childInterface = childModule->interface;
        if(childInterface->vinbero_ITLocal_destroy(childModule) == -1) {
            warnx("vinbero_ITLocal_destroy() failed at module %s", childModule->id);
            return -1;
        }
    }
    return 0;
}

int vinbero_ITLocal_rDestroy(struct vinbero_Module* module) {
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    struct vinbero_tcp_mt_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);
    if(tlModule != NULL) {
        free(tlModule->epollEventArray);
        free(tlModule->clientSocketArray);
        free(tlModule->clientTimerFdArray);
        for(size_t index = 0; index != tlModule->clientArraySize; ++index)
            free(tlModule->clDataArray[index]);
        free(tlModule->clDataArray);
        free(tlModule);
    }
    return 0;
}

int vinbero_IModule_destroy(struct vinbero_Module* module) {
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
    return 0;
}

int vinbero_IModule_rDestroy(struct vinbero_Module* module) {
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    warnx("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
//    dlclose(module->dl_handle);
    pthread_key_delete(*module->tlModuleKey);
    free(module->tlModuleKey);
    free(module->localModule.pointer);
    free(module);
    return 0;
}
