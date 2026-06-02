cmake_minimum_required(VERSION 3.20)

project(HivetonUsbRecoveryQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets SerialPort)

add_executable(hiveton-usb-recovery MACOSX_BUNDLE
    main.cpp
    resources.qrc
)

set_target_properties(hiveton-usb-recovery PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Hiveton USB Recovery"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.hiveton.usb-recovery"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.2.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.2.0"
)

target_link_libraries(hiveton-usb-recovery
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::SerialPort
)
