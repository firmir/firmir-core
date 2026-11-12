add_library(firmir-core SHARED
    src/bytearray.cpp
    src/datastream.cpp
    src/dir.cpp
    src/file.cpp
    src/fileinfo.cpp
    src/json.cpp
    src/locale.cpp
    src/log.cpp
    src/object.cpp
    src/regexp.cpp
    src/settings.cpp
    src/socketnotifier.cpp
    src/string.cpp
    src/textstream.cpp
    src/thread.cpp
    src/timer.cpp
    src/translator.cpp
    src/variant.cpp
    src/xml.cpp
)

target_include_directories(firmir-core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(firmir-core PUBLIC Threads::Threads
    PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
    # SocketNotifier waits on sockets via WSAEventSelect.
    target_link_libraries(firmir-core PRIVATE ws2_32)
endif()
# Locale backend plugins are dlopen()ed from here as a last resort.
target_compile_definitions(firmir-core PRIVATE
    FIRMIR_SYSTEM_PLUGIN_DIR="${FIRMIR_SYSTEM_PLUGIN_DIR}/core")

firmir_install_library(firmir-core MODULE core)
