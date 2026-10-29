file(GLOB_RECURSE MAFIA1ONLINE_SERVER_FEATURES CONFIGURE_DEPENDS "src/features/*/*.cpp")

add_executable(Mafia1OnlineServer
    src/main.cpp
    src/core/server.cpp
    ../shared/register_entities.cpp
    ${MAFIA1ONLINE_SERVER_FEATURES}
    ${CMAKE_BINARY_DIR}/mafia1online_version.cpp
)
target_include_directories(Mafia1OnlineServer PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(Mafia1OnlineServer PRIVATE Framework::Framework Framework::Server)
# libnode and the crash handler next to the server.
fw_stage_runtime(Mafia1OnlineServer SERVER)

if(WIN32)
    target_compile_definitions(Mafia1OnlineServer PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
