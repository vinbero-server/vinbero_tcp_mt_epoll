add_library(vinbero_tcp_mt_epoll MODULE vinbero_tcp_mt_epoll.c)
set_target_properties(vinbero_tcp_mt_epoll PROPERTIES PREFIX "")
install(TARGETS vinbero_tcp_mt_epoll DESTINATION lib/vinbero)
