# Copies the built web UI next to the client, if it has been built.
if(EXISTS "${SOURCE}/index.html")
    file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}")
else()
    message(STATUS "Mafia1Online web UI not built (${SOURCE}); quick join and the native chat stay in use")
endif()
