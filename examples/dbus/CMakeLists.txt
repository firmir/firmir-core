add_executable(firmir-example-dbus-echo main.cpp)
target_link_libraries(firmir-example-dbus-echo PRIVATE firmir-dbus)
firmir_install_example(firmir-example-dbus-echo)
