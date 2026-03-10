set(URL_SHORTENER_CPP
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/url_shortener.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
)

set(URL_SHORTENER_H
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/url_shortener.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/uri_map_singleton.h
)

set(URL_SHORTENER_SOURCES
        ${URL_SHORTENER_CPP}
        ${URL_SHORTENER_H}
)
