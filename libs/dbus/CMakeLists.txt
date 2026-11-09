add_library(firmir-dbus SHARED
    src/dbuscodegen.cpp
    src/dbusconnection.cpp
    src/dbuserror.cpp
    src/dbusmessage.cpp
    src/dbusvalue.cpp
    src/marshall.cpp
)

target_include_directories(firmir-dbus
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(firmir-dbus PUBLIC firmir-core)

add_executable(firmir-dbus-tool tool/main.cpp)
target_link_libraries(firmir-dbus-tool PRIVATE firmir-dbus)

firmir_install_library(firmir-dbus MODULE dbus REQUIRES firmir-core)
