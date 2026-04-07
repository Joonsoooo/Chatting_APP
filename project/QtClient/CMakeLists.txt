cmake_minimum_required(VERSION 3.16)

project(QtClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../../binary")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../../binary")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../../binary")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets)

qt_standard_project_setup()

qt_add_executable(QtClient
    main.cpp
    chatwindow.cpp
    chatwindow.h
    protocol.h
)

target_link_libraries(QtClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
)
