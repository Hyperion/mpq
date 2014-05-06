project(mpq)
cmake_minimum_required(VERSION 2.8)

add_executable(mpq main.cpp)
target_link_libraries(mpq storm)

install(TARGETS mpq RUNTIME DESTINATION bin)
