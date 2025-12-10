cmake_minimum_required(VERSION 3.10)
project(io_uring_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 liburing 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

include_directories(${LIBURING_INCLUDE_DIRS})
link_directories(${LIBURING_LIBRARY_DIRS})

add_executable(io_uring_test main.cpp)
target_link_libraries(io_uring_test ${LIBURING_LIBRARIES})
