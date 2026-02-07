# Example executables

add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE mcp)

add_executable(calculator_server calculator_server.cpp)
target_link_libraries(calculator_server PRIVATE mcp)

add_executable(simple_client simple_client.cpp)
target_link_libraries(simple_client PRIVATE mcp)
