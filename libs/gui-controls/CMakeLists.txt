add_library(firmir-gui-controls SHARED
    src/abstractbutton.cpp
    src/containers.cpp
    src/indicators.cpp)
target_include_directories(firmir-gui-controls PUBLIC include)
target_link_libraries(firmir-gui-controls PUBLIC firmir-gui)

firmir_install_library(firmir-gui-controls MODULE gui-controls REQUIRES firmir-gui)
