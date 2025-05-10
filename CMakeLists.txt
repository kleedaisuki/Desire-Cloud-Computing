cmake_minimum_required(VERSION 3.16)

project(Simple-K-Executor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Build type not specified, defaulting to Release.")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type (Debug, Release, RelWithDebInfo, MinSizeRel, NativeOptimizationRelease)" FORCE)
else()
  set(KNOWN_BUILD_TYPES Debug Release RelWithDebInfo MinSizeRel NativeOptimizationRelease)
  list(FIND KNOWN_BUILD_TYPES "${CMAKE_BUILD_TYPE}" IS_KNOWN)
  if(IS_KNOWN EQUAL -1)
    message(WARNING "Specified build type '${CMAKE_BUILD_TYPE}' is not a standard or predefined type. Ensure relevant compiler flags are set.")
  endif()
endif()
message(STATUS "Current build type: ${CMAKE_BUILD_TYPE}")

set(DEBUG_EXCLUSIVE_WARNING_FLAGS "-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wfloat-equal")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og -g ${DEBUG_EXCLUSIVE_WARNING_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -flto -DNDEBUG")
set(CMAKE_CXX_FLAGS_NATIVEOPTIMIZATIONRELEASE "${CMAKE_CXX_FLAGS_NATIVEOPTIMIZATIONRELEASE} -O2 -flto -march=native -DNDEBUG")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
message(STATUS "Qt6 libraries found. Components: ${Qt6_FOUND_COMPONENTS}")

add_executable(back.exe
    backend/main.cpp
    backend/compile-thread.cpp
    backend/write-log.cpp

    backend/network/class.TcpConnection.cpp
    backend/network/class.Channel.cpp
    backend/network/class.Buffer.cpp
    backend/network/class.Accepter.cpp
    backend/network/class.EventLoop.cpp
    backend/network/class.TcpServer.cpp
)
message(STATUS "Backend target 'back.exe' configured.")

qt_add_executable(front.exe
    frontend/main.cpp
    frontend/write-log.cpp

    frontend/network/class.ClientSocket.cpp
    frontend/network/class.ConnectionManager.cpp
    frontend/network/class.MessageHandler.cpp
    frontend/network/class.Receiver.cpp
    frontend/network/class.Sender.cpp

    frontend/graphic-interface/MainWindow.cpp
    frontend/graphic-interface/class.TaskManager.cpp
    frontend/graphic-interface/class.MainWindow.cpp
    frontend/graphic-interface/graphic-interface.hpp
    frontend/graphic-interface/class.SendFileTask.cpp
    frontend/graphic-interface/base.ActionTask.hpp
)

target_link_libraries(front.exe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
message(STATUS "Frontend target 'front.exe' configured and linked with Qt6 libraries.")

message(STATUS "CMake configuration finished!")
