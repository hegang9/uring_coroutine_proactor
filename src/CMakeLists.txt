# 列出需要编入库的源文件（不包含入口 main.cpp）
file(GLOB SRC_LIB_FILES "*.cpp")
list(REMOVE_ITEM SRC_LIB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

# 创建共享库
add_library(src_lib SHARED ${SRC_LIB_FILES})

# 可执行文件只包含入口
add_executable(main main.cpp)

# 链接必要的库
target_link_libraries(src_lib ${LIBURING_LIBRARIES})
target_link_libraries(main ${LIBURING_LIBRARIES} src_lib)