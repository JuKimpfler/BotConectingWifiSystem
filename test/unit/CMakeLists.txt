cmake_minimum_required(VERSION 3.10)
project(unit_tests CXX)

set(CMAKE_CXX_STANDARD 17)

# Shared include path
include_directories(
    ${CMAKE_SOURCE_DIR}/../../shared
    ${CMAKE_SOURCE_DIR}/../../ESP_Satellite/include
)

# Stub SAT_ID for test build
add_compile_definitions(SAT_ID=1)

# ── test_crc16 ────────────────────────────────────────────────
add_executable(test_crc16 test_crc16.cpp)

# ── test_messages ─────────────────────────────────────────────
add_executable(test_messages test_messages.cpp)

# ── test_command_parser ───────────────────────────────────────
add_executable(test_command_parser
    test_command_parser.cpp
    ../../ESP_Satellite/src/CommandParser.cpp
)

enable_testing()
add_test(NAME test_crc16          COMMAND test_crc16)
add_test(NAME test_messages       COMMAND test_messages)
add_test(NAME test_command_parser COMMAND test_command_parser)
