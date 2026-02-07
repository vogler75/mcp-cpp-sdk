# Test executables

add_executable(test_model_serialization test_model_serialization.cpp)
target_link_libraries(test_model_serialization PRIVATE mcp GTest::gtest_main)
add_test(NAME test_model_serialization COMMAND test_model_serialization)

add_executable(test_jsonrpc test_jsonrpc.cpp)
target_link_libraries(test_jsonrpc PRIVATE mcp GTest::gtest_main)
add_test(NAME test_jsonrpc COMMAND test_jsonrpc)

add_executable(test_tool_router test_tool_router.cpp)
target_link_libraries(test_tool_router PRIVATE mcp GTest::gtest_main)
add_test(NAME test_tool_router COMMAND test_tool_router)

add_executable(test_service_loop test_service_loop.cpp)
target_link_libraries(test_service_loop PRIVATE mcp GTest::gtest_main)
add_test(NAME test_service_loop COMMAND test_service_loop)

add_executable(test_transports test_transports.cpp)
target_link_libraries(test_transports PRIVATE mcp GTest::gtest_main)
add_test(NAME test_transports COMMAND test_transports)
