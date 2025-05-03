cmake_minimum_required(VERSION 3.16)

project(Desire-Cloud-Computing-frontend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# 添加可执行文件
qt_add_executable(hello.exe
    main.cpp                 
    global-constant.cpp
    write-log.cpp

    network/class.ClientSocket.cpp
    network/class.ConnectionManager.cpp
    network/class.MessageHandler.cpp
    network/class.Receiver.cpp
    network/class.Sender.cpp

    graphic-interface/MainWindow.cpp
)

target_link_libraries(hello.exe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
