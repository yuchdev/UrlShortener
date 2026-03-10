install(
    TARGETS simple-http
    RUNTIME COMPONENT simple-http_Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
