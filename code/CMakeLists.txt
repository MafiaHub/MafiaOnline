include(UpdateGitHash)
update_git_version(Mafia1Online "${CMAKE_CURRENT_SOURCE_DIR}/shared/version.cpp.in" "${CMAKE_BINARY_DIR}/mafia1online_version.cpp")

add_subdirectory(sdk)
add_subdirectory(tests)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_subdirectory(client)
    add_subdirectory(launcher)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_subdirectory(server)
endif()
