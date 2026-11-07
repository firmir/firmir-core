add_library(firmir-core SHARED
    src/bytearray.cpp
    src/datastream.cpp
    src/dir.cpp
    src/file.cpp
    src/fileinfo.cpp
    src/log.cpp
    src/object.cpp
    src/regexp.cpp
    src/settings.cpp
    src/socketnotifier.cpp
    src/string.cpp
    src/textstream.cpp
    src/thread.cpp
    src/timer.cpp
    src/variant.cpp
)

target_include_directories(firmir-core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(firmir-core PUBLIC Threads::Threads)

firmir_install_library(firmir-core MODULE core)
