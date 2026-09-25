cmake_minimum_required(VERSION 3.10)
project(MyBatch VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_library(SQLITE3_LIBRARY sqlite3)
if(NOT SQLITE3_LIBRARY)
    message(FATAL_ERROR "sqlite3 library not found")
endif()

include_directories(include)

add_library(common src/db.cpp src/utils.cpp)
target_link_libraries(common ${SQLITE3_LIBRARY})

add_executable(mybatchd src/mybatchd.cpp)
target_link_libraries(mybatchd common)

add_executable(mybatch_cli src/cli.cpp)
target_link_libraries(mybatch_cli common)

add_custom_command(TARGET mybatch_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink mybatch_cli mybatch
    COMMAND ${CMAKE_COMMAND} -E create_symlink mybatch_cli mystatus
    COMMAND ${CMAKE_COMMAND} -E create_symlink mybatch_cli mycancel
    COMMAND ${CMAKE_COMMAND} -E create_symlink mybatch_cli mylogs
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

install(TARGETS mybatchd mybatch_cli DESTINATION bin)
install(FILES 
    ${CMAKE_BINARY_DIR}/mybatch
    ${CMAKE_BINARY_DIR}/mystatus
    ${CMAKE_BINARY_DIR}/mycancel
    ${CMAKE_BINARY_DIR}/mylogs
    DESTINATION bin
)
install(FILES completions/mybatch.bash DESTINATION share/bash-completion/completions RENAME mybatch)

