cmake_minimum_required(VERSION 3.10)
# 设置项目名称
project(io_uring_test)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
# 设置 C++ 标准为必需
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加协程支持 (针对 GCC)
add_compile_options(-fcoroutines)

# 查找 liburing 库
find_package(PkgConfig REQUIRED)
# 使用 pkg-config 查找 liburing
pkg_check_modules(LIBURING REQUIRED liburing)

# 包含 liburing 头文件和链接库
include_directories(${LIBURING_INCLUDE_DIRS})
link_directories(${LIBURING_LIBRARY_DIRS})

# 添加头文件搜索路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
# 设置动态库输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
# 设置静态库输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

# 递归添加 src 目录下的子目录
add_subdirectory(src)