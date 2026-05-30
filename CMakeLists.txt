cmake_minimum_required(VERSION 3.10)
project(EventLoopExample)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Определяем ОС
if(UNIX AND NOT APPLE)
    set(OS_LINUX TRUE)
    message(STATUS "Обнаружена Linux система")
elseif(APPLE)
    set(OS_MAC TRUE)
    message(STATUS "Обнаружена macOS система")
elseif(WIN32)
    set(OS_WINDOWS TRUE)
    message(STATUS "Обнаружена Windows система")
endif()

# Поиск libevent
if(OS_WINDOWS)
    # Для Windows: предполагаем, что libevent установлена в стандартные пути
    find_path(LIBEVENT_INCLUDE_DIR event2/event.h)
    find_library(LIBEVENT_LIBRARY event)
    
    if(NOT LIBEVENT_INCLUDE_DIR OR NOT LIBEVENT_LIBRARY)
        message(FATAL_ERROR "libevent не найдена в системе. Установите через vcpkg или вручную")
    endif()
    
    set(LIBEVENT_INCLUDE_DIRS ${LIBEVENT_INCLUDE_DIR})
    set(LIBEVENT_LIBRARIES ${LIBEVENT_LIBRARY})
else()
    # Для Linux/macOS используем pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBEVENT REQUIRED libevent)
endif()

# Создаем исполняемый файл
add_executable(event_loop main.cpp)

target_include_directories(event_loop PRIVATE ${LIBEVENT_INCLUDE_DIRS})
target_link_libraries(event_loop ${LIBEVENT_LIBRARIES})

# Дополнительные линковки для разных ОС
if(OS_LINUX)
    target_link_libraries(event_loop pthread)
endif()

if(OS_MAC)
    target_link_libraries(event_loop)
endif()

if(OS_WINDOWS)
    target_link_libraries(event_loop ws2_32)
endif()
