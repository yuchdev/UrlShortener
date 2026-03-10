set(HTTP_SERVER_CPP
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/http_server.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
)

set(HTTP_SERVER_H
        ${CMAKE_CURRENT_SOURCE_DIR}/include/http_server/http_server.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/http_server/uri_map_singleton.h
)

set(HTTP_SERVER_SOURCES
        ${HTTP_SERVER_CPP}
        ${HTTP_SERVER_H}
)
