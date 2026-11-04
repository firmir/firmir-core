add_library(firmir-xml SHARED src/xml.cpp)
target_include_directories(firmir-xml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(firmir-xml PUBLIC firmir-core)
