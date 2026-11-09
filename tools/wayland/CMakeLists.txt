# firmir-wayland-generator — Wayland protocol XML -> C++ wrapper generator.
# Build-time host tool for libs/wayland; reimplements the former Python script
# in C++ on top of the XML parser in firmir-core (no Python needed at build
# time).
add_executable(firmir-wayland-generator firmir-wayland-generator.cpp)
target_link_libraries(firmir-wayland-generator PRIVATE firmir-core)
