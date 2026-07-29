cmake_minimum_required(VERSION 3.17)

project(TreadPoolPro)


add_executable(ThreadPool)


target_sources(ThreadPool PUBLIC
    ${CMAKE_SOURCE_DIR}/tools/ThreadPool/main.cpp
    )

target_include_directories(ThreadPool PUBLIC
    ${CMAKE_SOURCE_DIR}/tools/ThreadPool/
    )   