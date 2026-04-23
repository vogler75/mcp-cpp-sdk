cmake_minimum_required(VERSION 3.20)
project(mcp-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(MCP_BUILD_TESTS "Build tests" ON)
option(MCP_BUILD_EXAMPLES "Build examples" ON)
option(MCP_BUILD_HTTP_TRANSPORT "Build HTTP transport (requires Boost.Beast)" ON)
option(MCP_BUILD_CHILD_PROCESS "Build child process transport (POSIX fork/pipe)" OFF)

# Fetch dependencies
include(cmake/FetchDependencies.cmake)

# Library sources
set(MCP_MODEL_SOURCES
    src/model/types.cpp
    src/model/error.cpp
    src/model/meta.cpp
    src/model/jsonrpc.cpp
    src/model/content.cpp
    src/model/tool.cpp
    src/model/prompt.cpp
    src/model/resource.cpp
    src/model/capabilities.cpp
    src/model/sampling.cpp
    src/model/elicitation.cpp
    src/model/task.cpp
    src/model/logging.cpp
    src/model/completion.cpp
    src/model/roots.cpp
    src/model/pagination.cpp
    src/model/notifications.cpp
    src/model/init.cpp
    src/model/unions.cpp
)

set(MCP_TRANSPORT_SOURCES
    src/transport/stdio_transport.cpp
)

if(MCP_BUILD_CHILD_PROCESS)
    list(APPEND MCP_TRANSPORT_SOURCES src/transport/child_process_transport.cpp)
endif()

set(MCP_SERVICE_SOURCES
    src/service/peer.cpp
    src/service/running_service.cpp
    src/service/client_init.cpp
    src/service/server_init.cpp
)

set(MCP_HANDLER_SOURCES
    src/handler/server_handler.cpp
    src/handler/client_handler.cpp
    src/handler/tool_router.cpp
    src/handler/prompt_router.cpp
)

set(MCP_TASK_MANAGER_SOURCES
    src/task_manager/operation_processor.cpp
)

set(MCP_AUTH_SOURCES
    src/auth/auth.cpp
)

set(MCP_ALL_SOURCES
    ${MCP_MODEL_SOURCES}
    ${MCP_TRANSPORT_SOURCES}
    ${MCP_SERVICE_SOURCES}
    ${MCP_HANDLER_SOURCES}
    ${MCP_TASK_MANAGER_SOURCES}
    ${MCP_AUTH_SOURCES}
)

if(MCP_BUILD_HTTP_TRANSPORT)
    list(APPEND MCP_ALL_SOURCES
        src/transport/streamable_http_client.cpp
        src/transport/streamable_http_server.cpp
        src/transport/websocket_transport.cpp
        src/transport/sse_message.cpp
        src/transport/oneshot_transport.cpp
        src/transport/local_session_manager.cpp
        src/transport/never_session_manager.cpp
    )
endif()

add_library(mcp ${MCP_ALL_SOURCES})
add_library(mcp::mcp ALIAS mcp)

target_include_directories(mcp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcp
    PUBLIC
        nlohmann_json::nlohmann_json
        Boost::headers
    PRIVATE
        spdlog::spdlog
)

if(MCP_BUILD_HTTP_TRANSPORT)
    target_compile_definitions(mcp PUBLIC MCP_HTTP_TRANSPORT=1)
endif()

if(MCP_BUILD_CHILD_PROCESS)
    target_compile_definitions(mcp PUBLIC MCP_CHILD_PROCESS_TRANSPORT=1)
endif()

# Tests
if(MCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(MCP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
