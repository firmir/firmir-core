# wl_shm client backend. No GPU: frames travel through shared memory, which is
# what the base library used to do inline.
add_library(firmir-wayland-client-shm SHARED shm.cpp)
set_target_properties(firmir-wayland-client-shm PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_CLIENT_PLUGIN_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${FIRMIR_WAYLAND_CLIENT_PLUGIN_DIR})
target_link_libraries(firmir-wayland-client-shm PRIVATE firmir-wayland-client)

# shm_open needs -lrt on glibc < 2.34.
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    target_link_libraries(firmir-wayland-client-shm PRIVATE ${RT_LIBRARY})
endif()
