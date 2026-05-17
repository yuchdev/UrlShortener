set(URL_SHORTENER_CPP
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cli_parser.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/url_shortener.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uri_map_singleton.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LogLevel.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LoggingConfig.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LogFields.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/Logger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/observability/LoggerFactory.cpp
)

set(URL_SHORTENER_H
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/url_shortener.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/uri_map_singleton.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/cli/cli_parser.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/observability/LogLevel.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/observability/LoggingConfig.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/observability/LogFields.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/observability/Logger.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/url_shortener/observability/LoggerFactory.h
)

set(URL_SHORTENER_SOURCES
        ${URL_SHORTENER_CPP}
        ${URL_SHORTENER_H}
)
