cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

# The Framework: the pinned prebuilt SDK (cmake/FrameworkPin.cmake), a local install tree
# (-DFW_SDK_ROOT) or a source checkout built alongside (-DFW_SOURCE_DIR). Must precede project():
# it selects the vcpkg toolchain.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FrameworkBootstrap.cmake)

project(Mafia1Online CXX)
fw_bootstrap_finish()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
add_subdirectory(code)

# Deployment key for every RPC identifier, handed to the Framework at runtime through
# InstanceOptions::rpcIdentifierSalt. Client and server must carry the same value or no RPC
# dispatches (the build token gate refuses the connection). Empty keeps the Framework default.
set(FW_RPC_IDENTIFIER_SALT "" CACHE STRING "64-bit salt keying every RPC identifier; must match on client and server")
if(NOT FW_RPC_IDENTIFIER_SALT STREQUAL "")
    string(REGEX REPLACE "[uUlL]+$" "" _m1o_rpc_salt "${FW_RPC_IDENTIFIER_SALT}")
    if(NOT _m1o_rpc_salt MATCHES "^(0[xX][0-9a-fA-F]+|[0-9]+)$")
        message(FATAL_ERROR "FW_RPC_IDENTIFIER_SALT must be a hexadecimal (0x...) or decimal integer")
    endif()
    foreach(_m1o_target Mafia1OnlineClient Mafia1OnlineServer)
        if(TARGET ${_m1o_target})
            target_compile_definitions(${_m1o_target} PRIVATE MAFIA1ONLINE_RPC_IDENTIFIER_SALT=${_m1o_rpc_salt}ULL)
        endif()
    endforeach()
    message(STATUS "Mafia1Online: RPC identifier salt set")
endif()
