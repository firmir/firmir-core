add_library(firmir-json SHARED
    src/json.cpp
)

target_include_directories(firmir-json
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(firmir-json PUBLIC firmir-core)