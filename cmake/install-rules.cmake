install(
    TARGETS url_shortener
    RUNTIME COMPONENT url_shortener_Runtime
)

if(PROJECT_IS_TOP_LEVEL)
  include(CPack)
endif()
