# Vulkan offscreen backend. Built-in GLSL is compiled to SPIR-V at build time;
# no native window or swapchain is needed by the existing display backends.
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(VERT_SPV ${GENERATED_DIR}/builtin_vert.hpp)
set(FRAG_SPV ${GENERATED_DIR}/builtin_frag.hpp)
add_custom_command(
    OUTPUT ${VERT_SPV} ${FRAG_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${GLSLANG_VALIDATOR} -V --vn firmirVulkanVertSpirv
            ${CMAKE_CURRENT_SOURCE_DIR}/builtin.vert -o ${VERT_SPV}
    COMMAND ${GLSLANG_VALIDATOR} -V --vn firmirVulkanFragSpirv
            ${CMAKE_CURRENT_SOURCE_DIR}/builtin.frag -o ${FRAG_SPV}
    DEPENDS builtin.vert builtin.frag
    VERBATIM)

add_library(firmir-render-vulkan SHARED vulkan.cpp ${VERT_SPV} ${FRAG_SPV})
set_target_properties(firmir-render-vulkan PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${FIRMIR_RENDER_PLUGIN_DIR})
target_include_directories(firmir-render-vulkan PRIVATE ${GENERATED_DIR})
target_link_libraries(firmir-render-vulkan PRIVATE firmir-render Vulkan::Vulkan)
# Vulkan C structures are intentionally initialized field-by-field/partially.
target_compile_options(firmir-render-vulkan PRIVATE -Wno-missing-field-initializers)
