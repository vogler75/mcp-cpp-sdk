# Example executables

add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE mcp)

add_executable(calculator_server calculator_server.cpp)
target_link_libraries(calculator_server PRIVATE mcp)

add_executable(simple_client simple_client.cpp)
target_link_libraries(simple_client PRIVATE mcp)

# TCP transport example
add_executable(tcp_transport tcp_transport.cpp)
target_link_libraries(tcp_transport PRIVATE mcp)

# Unix domain socket transport example
add_executable(unix_socket_transport unix_socket_transport.cpp)
target_link_libraries(unix_socket_transport PRIVATE mcp)

# WebSocket transport example (requires HTTP transport / Boost.Beast)
if(MCP_BUILD_HTTP_TRANSPORT)
    add_executable(websocket_transport websocket_transport.cpp)
    target_link_libraries(websocket_transport PRIVATE mcp)

    add_executable(http_upgrade_transport http_upgrade_transport.cpp)
    target_link_libraries(http_upgrade_transport PRIVATE mcp)

    # Comprehensive HTTP test server with auth, tools, prompts, resources
    add_executable(http_test_server http_test_server.cpp)
    target_link_libraries(http_test_server PRIVATE mcp)
endif()
