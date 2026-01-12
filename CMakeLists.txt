cmake_minimum_required(VERSION 3.10)
# 设置项目名称
project(io_uring_test)

 set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
# 设置 C++ 标准为必需
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加协程支持 (针对 GCC)
# add_compile_options(-fcoroutines)

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

# ==========================================
# 项目构建配置
# ==========================================

# 1. 收集源文件 (排除入口文件)
file(GLOB SRC_FILES "src/*.cpp")
file(GLOB MEMORY_FILES "memory/*.cpp")
list(APPEND SRC_FILES ${MEMORY_FILES})
# 排除 test.cpp 作为库的一部分
list(REMOVE_ITEM SRC_FILES "${CMAKE_SOURCE_DIR}/src/test.cpp")
# 如果存在 main.cpp 也排除
list(REMOVE_ITEM SRC_FILES "${CMAKE_SOURCE_DIR}/src/main.cpp")

# 2. 生成静态库 (libproactor.a)
add_library(proactor_static STATIC ${SRC_FILES})
set_target_properties(proactor_static PROPERTIES OUTPUT_NAME "proactor")
# 静态库通常不需要链接依赖，但在某些构建系统中显式声明有助于传递依赖
target_link_libraries(proactor_static ${LIBURING_LIBRARIES} pthread)

# 3. 生成动态库 (libproactor.so)
add_library(proactor_shared SHARED ${SRC_FILES})
set_target_properties(proactor_shared PROPERTIES OUTPUT_NAME "proactor")
target_link_libraries(proactor_shared ${LIBURING_LIBRARIES} pthread)

# 4. 生成测试可执行文件
add_executable(proactor_test src/test.cpp)
# 链接动态库
target_link_libraries(proactor_test proactor_shared ${LIBURING_LIBRARIES} pthread)

# 5. 应用层压测工具
add_executable(echo_bench bench/echo_bench.cpp)
target_link_libraries(echo_bench pthread)

