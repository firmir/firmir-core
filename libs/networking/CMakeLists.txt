find_package(OpenSSL REQUIRED)

add_library(firmir-networking SHARED
    src/networkaccessmanager.cpp
    src/networkreply.cpp
)

target_include_directories(firmir-networking
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(firmir-networking
    PUBLIC firmir-core
    PRIVATE OpenSSL::SSL OpenSSL::Crypto
)
if(WIN32)
    target_link_libraries(firmir-networking PRIVATE ws2_32)
endif()

firmir_install_library(firmir-networking MODULE networking REQUIRES firmir-core)
