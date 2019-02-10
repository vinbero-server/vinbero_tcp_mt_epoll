add_library(vinbero_strm_mt_epoll MODULE vinbero_strm_mt_epoll.c)
set_target_properties(vinbero_strm_mt_epoll PROPERTIES PREFIX "")
set_target_properties(vinbero_strm_mt_epoll PROPERTIES VERSION ${vinbero_strm_mt_epoll_version}
                                           SOVERSION ${vinbero_strm_mt_epoll_soversion}
                                           PUBLIC_HEADER "")

find_library(fastdl libfastdl.so REQUIRED)
find_library(gaio libgaio.so.0.3 REQUIRED)
find_library(vinbero_com libvinbero_com.so REQUIRED)
target_link_libraries(vinbero_strm_mt_epoll ${vinbero_com} ${fastdl} ${gaio})

install(TARGETS vinbero_strm_mt_epoll
        LIBRARY DESTINATION lib/vinbero
        PUBLIC_HEADER DESTINATION include/vinbero)
