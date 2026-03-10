# Deprecated in Stage 0.
# Ubuntu dependency baseline uses apt-provided Boost packages and does not build Boost from source.
function(fetch_boost)
  message(FATAL_ERROR "fetch_boost() is no longer supported. Install Boost via apt (see scripts/setup_ubuntu_dependencies.sh).")
endfunction()
