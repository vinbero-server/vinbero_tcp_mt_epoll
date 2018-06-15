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
#include <vinbero_common/vinbero_common_Call.h>
#include <vinbero_common/vinbero_common_Config.h>
#include <vinbero_common/vinbero_common_Log.h>
#include <vinbero_common/vinbero_common_Module.h>
#include <vinbero_common/vinbero_common_ClData.h>
#include <vinbero/vinbero_interface_MODULE.h>
#include <vinbero/vinbero_interface_TLOCAL.h>
#include <vinbero/vinbero_interface_TLSERVICE.h>
#include <vinbero/vinbero_interface_CLOCAL.h>
#include <vinbero/vinbero_interface_CLSERVICE.h>
#include <libgenc/genc_args.h>
#include <libgenc/genc_cast.h>
#include <libgenc/genc_Tree.h>
#include <gaio.h>

struct vinbero_tcp_mt_epoll_Module {
    struct itimerspec clientTimeout;
};

struct vinbero_tcp_mt_epoll_TlModule {
    struct epoll_event* epollEventArray;
    int epollEventArraySize;
    int* clientSocketArray;
    int* clientTimerFdArray;
    struct vinbero_common_ClData** clDataArray;
    int clientArraySize;
};

VINBERO_INTERFACE_MODULE_FUNCTIONS;
VINBERO_INTERFACE_TLOCAL_FUNCTIONS;
VINBERO_INTERFACE_TLSERVICE_FUNCTIONS;

int vinbero_interface_MODULE_init(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    module->name = "vinbero_tcp_mt_epoll";
    module->version = "0.0.1";
    module->localModule.pointer = malloc(1 * sizeof(struct vinbero_tcp_mt_epoll_Module));
    module->tlModuleKey = malloc(1 * sizeof(pthread_key_t));
    pthread_key_create(module->tlModuleKey, NULL);
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    int out;
    vinbero_common_Config_getInt(module->config, module, "vinbero_tcp_mt_epoll.clientTimeoutSeconds", &out, 3);
    localModule->clientTimeout.it_value.tv_sec = out;
    vinbero_common_Config_getInt(module->config, module, "vinbero_tcp_mt_epoll.clientTimeoutSeconds", &out, 3);
    localModule->clientTimeout.it_interval.tv_sec = out;
    vinbero_common_Config_getInt(module->config, module, "vinbero_tcp_mt_epoll.clientTimeoutNanoSeconds", &out, 0);
    localModule->clientTimeout.it_value.tv_nsec = out;
    vinbero_common_Config_getInt(module->config, module, "vinbero_tcp_mt_epoll.clientTimeoutNanoSeconds", &out, 0);
    localModule->clientTimeout.it_interval.tv_nsec = out;
/*
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_tcp_mt_epoll_interface childinterface;
        VINBERO_INTERFACE_TLOCAL_DLSYM(&childinterface, &childModule->dlHandle, &ret);
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ITLOCAL interface", childModule->id);
            return ret;
        }
        VINBERO_ICLOCAL_DLSYM(&childinterface, &childModule->dlHandle, &ret);
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ICLOCAL interface", childModule->id);
            return ret;
        }
        VINBERO_ICLSERVICE_DLSYM(&childinterface, &childModule->dlHandle, &ret);
        if(ret < 0) {
            VINBERO_COMMON_LOG_ERROR("module %s doesn't satisfy ICLSERVICE interface", childModule->id);
            return ret;
        }
    }
*/
    return 0;
}

int vinbero_interface_MODULE_rInit(struct vinbero_common_Module* module) {
    return 0;
}

int vinbero_interface_TLOCAL_init(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    struct vinbero_tcp_mt_epoll_TlModule* tlModule = malloc(1 * sizeof(struct vinbero_tcp_mt_epoll_TlModule));
    int workerCount;
    int workerMaxClients;
    if((ret = vinbero_common_Config_getRequiredInt(module->config, module, "vinbero_mt.workerCount", &workerCount)) < 0)
        return ret;
    vinbero_common_Config_getInt(module->config, module, "vinbero_tcp_mt_epoll.workerMaxClients", &workerMaxClients, 1024);
    tlModule->epollEventArraySize = workerMaxClients * 2 + 1; // '* 2': socket, timerfd; '+ 1': serverSocket; 
    tlModule->epollEventArray = malloc(tlModule->epollEventArraySize * sizeof(struct epoll_event));
    tlModule->clientArraySize = workerMaxClients * 2 * workerCount + 1 + 1 + 3; //'+ 1': serverSocket; '+ 1': epollFd; '+ 3': stdin, stdout, stderr; multipliying workerCount because file descriptors are shared among threads;
    tlModule->clientSocketArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientSocketArray, -1, tlModule->clientArraySize * sizeof(int));
    tlModule->clientTimerFdArray = malloc(tlModule->clientArraySize * sizeof(int));
    memset(tlModule->clientTimerFdArray, -1, tlModule->clientArraySize * sizeof(int));
    tlModule->clDataArray = calloc(tlModule->clientArraySize, sizeof(struct vinbero_common_ClData*));
    pthread_setspecific(*module->tlModuleKey, tlModule);

    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        VINBERO_COMMON_CALL(TLOCAL, init, childModule, &ret, childModule);
        if(ret < 0)
            return ret;
    }

    return 0;
}

int vinbero_interface_TLOCAL_rInit(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    return 0;
}

int vinbero_tcp_mt_epoll_preInitClData(struct vinbero_common_Module* module, struct vinbero_common_ClData* clData) {
    VINBERO_COMMON_LOG_TRACE2();
    GENC_TREE_NODE_INIT3(clData, GENC_TREE_NODE_GET_CHILD_COUNT(module));
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_common_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        GENC_TREE_NODE_INIT(childClData);
        GENC_TREE_NODE_SET_PARENT(childClData, clData);
        vinbero_tcp_mt_epoll_preInitClData(childModule, childClData);
    }
}

int vinbero_tcp_mt_epoll_initClData(struct vinbero_common_Module* module, struct vinbero_common_ClData* clData, struct gaio_Io* clientIo) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_common_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        VINBERO_COMMON_CALL(CLOCAL, init, childModule, &ret, childModule, childClData, GENC_ARGS(clientIo));
        if(ret < 0)
            return ret;
    }
    return 0;
}

int vinbero_tcp_mt_epoll_destroyClData(struct vinbero_common_Module* module, struct vinbero_common_ClData* clData) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(clData, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_common_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(clData, index);
        VINBERO_COMMON_CALL(CLOCAL, destroy, childModule, &ret, childModule, childClData);
        if(ret < 0) {
            GENC_TREE_NODE_FREE(clData);
            return ret;
        }
    }
    GENC_TREE_NODE_FREE(clData);
    return 0;
}

static void vinbero_tcp_mt_epoll_handleConnection(struct vinbero_common_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int epollFd, int* serverSocket) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    int clientSocket;
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event));

    if((clientSocket = accept(*serverSocket, NULL, NULL)) == -1) {
        if(errno != EAGAIN)
            VINBERO_COMMON_LOG_ERROR("accept() failed");
        return;
    }
    if(clientSocket > (tlModule->clientArraySize - 1) - 1) { // '-1': room for timerfd
        VINBERO_COMMON_LOG_ERROR("unable to accept more clients");
        close(clientSocket);
        return; 
    }
    if(fcntl(clientSocket, F_SETFL, fcntl(clientSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        VINBERO_COMMON_LOG_ERROR("fcntl() failed");
        close(clientSocket);
        return;
    }
    epollEvent.events = EPOLLET | EPOLLIN | EPOLLRDHUP | EPOLLHUP;
    epollEvent.data.fd = clientSocket;
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, clientSocket, &epollEvent) == -1) {
        VINBERO_COMMON_LOG_ERROR("epoll_ctl failed()");
        close(clientSocket);
        return;
    }
    if((tlModule->clientTimerFdArray[clientSocket] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) == -1) {
        VINBERO_COMMON_LOG_ERROR("timerfd_create() failed");
        close(clientSocket);
        return;
    }
    if(fcntl(tlModule->clientTimerFdArray[clientSocket], F_SETFL, fcntl(tlModule->clientTimerFdArray[clientSocket], F_GETFL, 0) | O_NONBLOCK) == -1) {
        VINBERO_COMMON_LOG_ERROR("fcntl() failed");
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
    if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &localModule->clientTimeout, NULL) == -1) {
        VINBERO_COMMON_LOG_ERROR("timerfd_settime() failed");
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clientTimerFdArray[clientSocket] = -1;
        return;
    }
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = tlModule->clientTimerFdArray[clientSocket];
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, tlModule->clientTimerFdArray[clientSocket], &epollEvent) == -1) {
        VINBERO_COMMON_LOG_ERROR("epoll_ctl() failed");
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

    tlModule->clDataArray[clientSocket] = malloc(1 * sizeof(struct vinbero_common_ClData));
    GENC_TREE_NODE_INIT(tlModule->clDataArray[clientSocket]);
    if((ret = vinbero_tcp_mt_epoll_preInitClData(module, tlModule->clDataArray[clientSocket])) < 0) {
        vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]); // what if this also failed? (FATAL)
        free(tlModule->clDataArray[clientSocket]);
        close(clientSocket);
        close(tlModule->clientTimerFdArray[clientSocket]);
        tlModule->clDataArray[clientSocket] = NULL;
        tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
        tlModule->clientTimerFdArray[clientSocket] = -1;
    }
    if((ret = vinbero_tcp_mt_epoll_initClData(module, tlModule->clDataArray[clientSocket], clientIo)) < 0) {
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
    struct vinbero_common_Module* module,
    struct vinbero_tcp_mt_epoll_Module* localModule,
    struct vinbero_tcp_mt_epoll_TlModule* tlModule,
    int* serverSocket,
    int clientSocket //
) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    if(timerfd_settime(tlModule->clientTimerFdArray[clientSocket], 0, &localModule->clientTimeout, NULL) == -1) {
        VINBERO_COMMON_LOG_ERROR("timerfd_settime() failed");
    }
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_common_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(tlModule->clDataArray[clientSocket], index);
        VINBERO_COMMON_CALL(CLSERVICE, call, childModule, &ret, childModule, childClData, GENC_ARGS(NULL));
        if(ret < 0)
            return ret;
    }
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static void vinbero_tcp_mt_epoll_handleDisconnection(struct vinbero_common_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int clientSocket) {
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static int vinbero_tcp_mt_epoll_handleError(struct vinbero_common_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int clientSocket) {
    VINBERO_COMMON_LOG_ERROR("Error occured on a socket");
    vinbero_tcp_mt_epoll_destroyClData(module, tlModule->clDataArray[clientSocket]);
    free(tlModule->clDataArray[clientSocket]);
    close(tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]]); // to prevent double close
    close(tlModule->clientTimerFdArray[clientSocket]);
    tlModule->clDataArray[clientSocket] = NULL;
    tlModule->clientSocketArray[tlModule->clientTimerFdArray[clientSocket]] = -1;
    tlModule->clientTimerFdArray[clientSocket] = -1;
}

static int vinbero_tcp_mt_epoll_handleTimeout(struct vinbero_common_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int timerFd) {
    int ret;
    uint64_t clientTimerFdValue;
    read(timerFd, &clientTimerFdValue, sizeof(uint64_t));
    int clientSocket = tlModule->clientSocketArray[timerFd];
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        struct vinbero_common_ClData* childClData = &GENC_TREE_NODE_GET_CHILD(tlModule->clDataArray[clientSocket], index);
        VINBERO_COMMON_CALL(CLOCAL, destroy, childModule, &ret, childModule, childClData);
    }
    close(timerFd);
    close(tlModule->clientSocketArray[timerFd]);
    tlModule->clientTimerFdArray[tlModule->clientSocketArray[timerFd]] = -1;
    tlModule->clientSocketArray[timerFd] = -1;
}

static void vinbero_tcp_mt_epoll_handleUnexpected(struct vinbero_common_Module* module, struct vinbero_tcp_mt_epoll_TlModule* tlModule, int* serverSocket, int fd) {
    VINBERO_COMMON_LOG_TRACE2();
    VINBERO_COMMON_LOG_FATAL("Unexpected file descriptor %d", fd); // This shouldn't happen at all
    VINBERO_COMMON_LOG_FATAL("%d %d", tlModule->clientTimerFdArray[fd], tlModule->clientSocketArray[fd]);
}

int vinbero_interface_TLSERVICE_call(struct vinbero_common_Module* module, void* args[]) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    int* serverSocket = (int*)args[0];
    struct vinbero_tcp_mt_epoll_TlModule* tlModule = pthread_getspecific(*module->tlModuleKey);    
    if(fcntl(*serverSocket, F_SETFL, fcntl(*serverSocket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        VINBERO_COMMON_LOG_ERROR("%s: %u", __FILE__, __LINE__);
        return -1;
    }
    int epollFd = epoll_create1(0);
    struct epoll_event epollEvent;
    memset(&epollEvent, 0, 1 * sizeof(struct epoll_event)); // to avoid valgrind VINBERO_COMMON_LOG_ERRORing: syscall param epoll_ctl(event) points to uninitialised byte(s)
    epollEvent.events = EPOLLIN | EPOLLET;
    epollEvent.data.fd = *serverSocket;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, *serverSocket, &epollEvent);
    for(int epollEventCount;;) {
        if((epollEventCount = epoll_wait(epollFd, tlModule->epollEventArray, tlModule->epollEventArraySize, -1)) == -1) {
            VINBERO_COMMON_LOG_ERROR("%s: %u: %s", __FILE__, __LINE__, __FUNCTION__);
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

int vinbero_interface_TLOCAL_destroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    int ret;
    GENC_TREE_NODE_FOR_EACH_CHILD(module, index) {
        struct vinbero_common_Module* childModule = &GENC_TREE_NODE_GET_CHILD(module, index);
        VINBERO_COMMON_CALL(TLOCAL, destroy, childModule, &ret, childModule);
    }
    return 0;
}

int vinbero_interface_TLOCAL_rDestroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
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

int vinbero_interface_MODULE_destroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    return 0;
}

int vinbero_interface_MODULE_rDestroy(struct vinbero_common_Module* module) {
    VINBERO_COMMON_LOG_TRACE2();
    struct vinbero_tcp_mt_epoll_Module* localModule = module->localModule.pointer;
    pthread_key_delete(*module->tlModuleKey);
    free(module->tlModuleKey);
    free(module->localModule.pointer);
    free(module);
    return 0;
}
