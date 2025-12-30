cmake_minimum_required(VERSION 3.16)
project(MycelFTExplorer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MycelFTExplorer MycelFTExplorer.cpp)
target_compile_definitions(MycelFTExplorer PRIVATE UNICODE _UNICODE)
target_link_libraries(MycelFTExplorer PRIVATE comctl32)
