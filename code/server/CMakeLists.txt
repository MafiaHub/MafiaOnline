file(GLOB_RECURSE MAFIA1ONLINE_SERVER_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")

add_executable(Mafia1OnlineServer
    src/main.cpp
    src/core/server.cpp
    ../shared/register_entities.cpp
    ${MAFIA1ONLINE_SERVER_FEATURES}
    ${CMAKE_BINARY_DIR}/mafia1online_version.cpp
)
target_include_directories(Mafia1OnlineServer PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineServer PRIVATE Framework FrameworkServer Framework)

if(WIN32)
    target_compile_definitions(Mafia1OnlineServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

if(UNIX AND NOT APPLE)
    set_target_properties(Mafia1OnlineServer PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    if(TARGET libnode)
        add_custom_command(TARGET Mafia1OnlineServer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:libnode>" "${CMAKE_BINARY_DIR}/bin/")
    endif()
endif()
