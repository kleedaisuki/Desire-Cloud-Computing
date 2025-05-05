cmake_minimum_required(VERSION 3.16)

project(a VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
-Wall
-Wextra
-Wpedantic 
-Wshadow 
-Wconversion
-Wsign-conversion
-Wfloat-equal
)

# add_compile_options(-O2 -flto)
# add_compile_options(-march=native)

add_compile_options(-Og -g)

add_executable(back.exe
    backend/main.cpp
    backend/compile-thread.cpp
    backend/global-constant.cpp
    backend/write-log.cpp

    backend/network/class.TcpConnection.cpp
    backend/network/class.Channel.cpp
    backend/network/class.Buffer.cpp
    backend/network/class.Accepter.cpp
    backend/network/class.EventLoop.cpp
    backend/network/class.TcpServer.cpp
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(front.exe
    frontend/main.cpp                 
    frontend/global-constant.cpp
    frontend/write-log.cpp

    frontend/network/class.ClientSocket.cpp
    frontend/network/class.ConnectionManager.cpp
    frontend/network/class.MessageHandler.cpp
    frontend/network/class.Receiver.cpp
    frontend/network/class.Sender.cpp

    frontend/MainWindow.cpp
)

target_link_libraries(front.exe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
