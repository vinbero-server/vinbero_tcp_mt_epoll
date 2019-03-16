find_package(fastdl REQUIRED)
find_package(gaio REQUIRED)
find_package(vinbero_com REQUIRED)

add_library(vinbero_strm_mt_epoll MODULE vinbero_strm_mt_epoll.c)
target_sources(vinbero_strm_mt_epoll PRIVATE vinbero_strm_mt_epoll.c)
target_compile_options(vinbero_strm_mt_epoll PRIVATE -Wall)
set_target_properties(
    vinbero_strm_mt_epoll
    PROPERTIES
    C_STANDARD 11
    PREFIX ""
    VERSION ${version}
    SOVERSION ${soversion}
    PUBLIC_HEADER ""
)

target_include_directories(
    vinbero_strm_mt_epoll
    PRIVATE
    ${PROJECT_BINARY_DIR}
    ${PROJECT_BINARY_DIR}/iface
)

target_link_libraries(
    vinbero_strm_mt_epoll
    vinbero_com::vinbero_com
    fastdl::fastdl
    gaio::gaio
)

configure_file(
    "${PROJECT_SOURCE_DIR}/src/config.h.in"
    "${PROJECT_BINARY_DIR}/config.h"
    @ONLY
)

install(
    TARGETS vinbero_strm_mt_epoll
    LIBRARY DESTINATION lib/vinbero
    PUBLIC_HEADER DESTINATION include/vinbero
)
