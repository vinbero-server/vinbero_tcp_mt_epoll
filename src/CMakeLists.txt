add_library(tucube_tcp_epoll SHARED src/tucube_tcp_epoll.c)
set_target_properties(tucube_tcp_epoll PROPERTIES PREFIX "")
install(TARGETS tucube_tcp_epoll DESTINATION /usr/local/lib/tucube/)
