# The MafiaHub build patcher (as KCDC's launcher runs it), off by default:
# only a release build is given the credentials. M1O_PATCHER_CHANNEL is never
# compiled in -- it seeds `.mafiahub/channel` beside a locally built launcher,
# and the release package carries its own stamp (tools/package_client.py).
option(M1O_PATCHER_ENABLED "Enable MafiaHub build patching before launching Mafia1Online" OFF)
set(M1O_PATCHER_API_URL "" CACHE STRING "MafiaHub API base URL used by the Mafia1Online build patcher")
set(M1O_PATCHER_MOD_ID "" CACHE STRING "MafiaHub mod UUID used by the Mafia1Online build patcher")
set(M1O_PATCHER_CHANNEL "stable" CACHE STRING "Initial MafiaHub build channel stored next to the Mafia1Online launcher")
set(M1O_PATCHER_DOWNLOAD_TOKEN "" CACHE STRING "MafiaHub build download token embedded into the Mafia1Online launcher")
set(M1O_PATCHER_DOWNLOAD_CONCURRENCY "4" CACHE STRING "MafiaHub build patcher download concurrency")

if(M1O_PATCHER_ENABLED)
    if("${M1O_PATCHER_API_URL}" STREQUAL "")
        message(FATAL_ERROR "M1O_PATCHER_ENABLED requires M1O_PATCHER_API_URL")
    endif()
    if("${M1O_PATCHER_MOD_ID}" STREQUAL "" OR "${M1O_PATCHER_MOD_ID}" STREQUAL "00000000-0000-0000-0000-000000000000")
        message(FATAL_ERROR "M1O_PATCHER_ENABLED requires M1O_PATCHER_MOD_ID")
    endif()
    string(LENGTH "${M1O_PATCHER_CHANNEL}" M1O_PATCHER_CHANNEL_LENGTH)
    if(NOT M1O_PATCHER_CHANNEL MATCHES "^[a-z0-9._-]+$" OR M1O_PATCHER_CHANNEL_LENGTH GREATER 32)
        message(FATAL_ERROR "M1O_PATCHER_CHANNEL must contain 1-32 lowercase letters, numbers, dots, underscores, or dashes")
    endif()
    if("${M1O_PATCHER_DOWNLOAD_TOKEN}" STREQUAL "")
        message(FATAL_ERROR "M1O_PATCHER_ENABLED requires M1O_PATCHER_DOWNLOAD_TOKEN")
    endif()
    if(NOT M1O_PATCHER_DOWNLOAD_CONCURRENCY MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "M1O_PATCHER_DOWNLOAD_CONCURRENCY must be a positive integer")
    endif()
    if(NOT TARGET MafiaHubUpdater)
        message(FATAL_ERROR "M1O_PATCHER_ENABLED requires the MafiaHubUpdater target")
    endif()
    set(M1O_PATCHER_ENABLED_CPP true)
else()
    set(M1O_PATCHER_ENABLED_CPP false)
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/patcher_config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mafia1online_patcher_config.h"
    @ONLY
)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/mafia1online-channel"
    CONTENT "${M1O_PATCHER_CHANNEL}"
)

add_executable(Mafia1OnlineLauncher src/main.cpp src/patcher.cpp)
target_include_directories(Mafia1OnlineLauncher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(Mafia1OnlineLauncher PRIVATE Framework::Framework Framework::Loader Mafia1OnlineClient Mafia1OnlineSDK Shell32)
# Game.exe has absolute 0x400000 references and no usable relocation table.
target_link_options(Mafia1OnlineLauncher PRIVATE /BASE:0x400000 /DYNAMICBASE:NO)

if(M1O_PATCHER_ENABLED)
    target_link_libraries(Mafia1OnlineLauncher PRIVATE MafiaHubServices)
    add_custom_command(TARGET Mafia1OnlineLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Mafia1OnlineLauncher>/.mafiahub"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/generated/mafia1online-channel"
            "$<TARGET_FILE_DIR:Mafia1OnlineLauncher>/.mafiahub/channel"
        COMMENT "Writing the installation-local Mafia1Online build channel"
    )
endif()

# A separate process because it replaces files this launcher holds open.
if(WIN32 AND TARGET MafiaHubUpdater)
    add_custom_command(TARGET Mafia1OnlineLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:MafiaHubUpdater>"
            "$<TARGET_FILE_DIR:Mafia1OnlineLauncher>"
        COMMENT "Bundling MafiaHubUpdater next to Mafia1OnlineLauncher"
    )
endif()
