add_library(vinbero_tcp_mt_epoll MODULE vinbero_tcp_mt_epoll.c)
set_target_properties(vinbero_tcp_mt_epoll PROPERTIES PREFIX "")
set_target_properties(vinbero_tcp_mt_epoll PROPERTIES VERSION ${vinbero_tcp_mt_epoll_version}
                                           SOVERSION ${vinbero_tcp_mt_epoll_soversion}
                                           PUBLIC_HEADER "")

find_library(fastdl libfastdl.so REQUIRED)
find_library(gaio libgaio.so.0.3 REQUIRED)
find_library(vinbero_common libvinbero_common.so REQUIRED)
target_link_libraries(vinbero_tcp_mt_epoll ${vinbero_common} ${fastdl} ${gaio})

install(TARGETS vinbero_tcp_mt_epoll
        LIBRARY DESTINATION lib/vinbero
        PUBLIC_HEADER DESTINATION include/vinbero)
