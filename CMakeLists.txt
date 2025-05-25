cmake_minimum_required(VERSION 3.10)
project(cpp-url-shortener)

set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system filesystem)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(cpp-url-shortener main.cpp)
target_link_libraries(cpp-url-shortener ${Boost_LIBRARIES})