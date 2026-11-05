#include "test_framework.hpp"

#include <firmir/render/render.hpp>
#include <null_control.hpp>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Firmir::Render;

namespace {

std::vector<std::uint8_t> shaderCode(const char* text) {
    std::vector<std::uint8_t> out;
    while (*text)
        out.push_back(static_cast<std::uint8_t>(*text++));
    return out;
}

} // namespace

TEST_CASE(render_null_records_primitive_draws) {
#ifdef FIRMIR_RENDER_TEST_PLUGIN_DIR
    ::setenv("FIRMIR_RENDER_PLUGIN_PATH", FIRMIR_RENDER_TEST_PLUGIN_DIR, 1);
#endif
    Null::reset();

    auto device = Render::createDevice("null");
    CHECK(device != nullptr);
    CHECK_EQ(device->backendName(), "null");

    const std::uint32_t vertices[] = {0, 1, 2, 3};
    const std::uint16_t indices[] = {0, 1, 2, 2, 3, 0};
    BufferDesc vertexDesc{BufferUsage::Vertex, sizeof(vertices)};
    BufferDesc indexDesc{BufferUsage::Index, sizeof(indices)};
    BufferDesc uniformDesc{BufferUsage::Uniform, 16};
    std::unique_ptr<RenderBuffer> vertexBuffer(
        device->createBuffer(vertexDesc));
    std::unique_ptr<RenderBuffer> indexBuffer(device->createBuffer(indexDesc));
    std::unique_ptr<RenderBuffer> uniformBuffer(
        device->createBuffer(uniformDesc));
    CHECK(vertexBuffer && indexBuffer && uniformBuffer);
    CHECK(vertexBuffer->write(vertices, sizeof(vertices)));
    CHECK(indexBuffer->write(indices, sizeof(indices)));
    CHECK(!vertexBuffer->write(vertices, sizeof(vertices), 4));

    TextureDesc textureDesc;
    textureDesc.width = 2;
    textureDesc.height = 2;
    textureDesc.format = TextureFormat::Rgba8;
    std::unique_ptr<RenderTexture> texture(device->createTexture(textureDesc));
    const std::uint32_t pixels[4] = {0, 1, 2, 3};
    CHECK(texture && texture->write(pixels, sizeof(pixels)));

    ShaderDesc vs{ShaderStage::Vertex, ShaderLanguage::Glsl, "main",
                  shaderCode("vs")};
    ShaderDesc fs{ShaderStage::Fragment, ShaderLanguage::Glsl, "main",
                  shaderCode("fs")};
    std::unique_ptr<RenderShader> vertexShader(device->createShader(vs));
    std::unique_ptr<RenderShader> fragmentShader(device->createShader(fs));
    CHECK(vertexShader && fragmentShader);

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexLayout.stride = sizeof(std::uint32_t);
    pipelineDesc.vertexLayout.attributes = {
        {0, "in_pos", VertexFormat::Float, 0},
    };
    std::unique_ptr<RenderPipeline> pipeline(device->createPipeline(
        pipelineDesc, vertexShader.get(), fragmentShader.get()));
    CHECK(pipeline != nullptr);
    CHECK(device->createPipeline(pipelineDesc, fragmentShader.get(),
                                 vertexShader.get()) == nullptr);

    std::unique_ptr<RenderTarget> target(
        device->createRenderTarget({8, 8, TextureFormat::Rgba8, false}));
    std::unique_ptr<RenderCommandBuffer> commands(
        device->createCommandBuffer());
    CHECK(target && commands);

    commands->begin();
    commands->beginRenderPass(target.get(), ClearValue{});
    commands->setViewport(Viewport{0, 0, 8, 8, 0, 1});
    commands->setScissor(Rect{0, 0, 8, 8});
    commands->bindPipeline(pipeline.get());
    commands->bindVertexBuffer(0, vertexBuffer.get());
    commands->bindIndexBuffer(indexBuffer.get(), IndexFormat::UInt16);
    commands->bindUniformBuffer(0, uniformBuffer.get());
    commands->bindTexture(0, texture.get());
    commands->draw(3);
    commands->drawIndexed(6);
    commands->endRenderPass();
    commands->end();
    device->submit(commands.get());
    device->waitIdle();

    auto swapchain = std::unique_ptr<RenderSwapchain>(
        device->createSwapchain(SwapchainDesc{nullptr, 16, 16, true}));
    CHECK(swapchain != nullptr);
    CHECK(swapchain->acquire());
    CHECK(swapchain->target() != nullptr);
    CHECK(swapchain->present());

    const Null::Stats& s = Null::stats();
    CHECK_EQ(s.devices, 1);
    CHECK_EQ(s.buffers, 3);
    CHECK_EQ(s.textures, 1);
    CHECK_EQ(s.shaders, 2);
    CHECK_EQ(s.pipelines, 1);
    CHECK_EQ(s.commandBuffers, 1);
    CHECK_EQ(s.submissions, 1);
    CHECK_EQ(s.renderPasses, 1);
    CHECK_EQ(s.draws, 1);
    CHECK_EQ(s.indexedDraws, 1);
    CHECK_EQ(s.vertices, 3u);
    CHECK_EQ(s.indices, 6u);
    CHECK_EQ(s.swapchains, 1);
    CHECK_EQ(s.presents, 1);
    CHECK(s.bytesWritten >= sizeof(vertices) + sizeof(indices) + sizeof(pixels));
}

#ifdef FIRMIR_RENDER_TEST_SOFT
namespace {

struct SoftVertex {
    float x, y;
    std::uint8_t rgba[4];
    float u, v;
};

struct SoftScene {
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<RenderShader> vs, fs;
    std::unique_ptr<RenderPipeline> pipeline;
    std::unique_ptr<RenderBuffer> vertices;
    std::unique_ptr<RenderTarget> target;
    std::unique_ptr<RenderCommandBuffer> commands;
};

/// Creates a soft device with a solid pipeline, an 8x8 target and a vertex
/// buffer; returns an empty scene when the soft plugin is unavailable.
SoftScene makeSoftScene(bool blending, const char* backend = "soft") {
    SoftScene scene;
    scene.device = Render::createDevice(backend);
    if (!scene.device)
        return scene;
    ShaderDesc vsDesc{ShaderStage::Vertex, ShaderLanguage::Glsl, "main",
                      shaderCode("vs")};
    ShaderDesc fsDesc{ShaderStage::Fragment, ShaderLanguage::Glsl, "main",
                      shaderCode("fs")};
    scene.vs.reset(scene.device->createShader(vsDesc));
    scene.fs.reset(scene.device->createShader(fsDesc));
    PipelineDesc pipelineDesc;
    pipelineDesc.vertexLayout.stride = sizeof(SoftVertex);
    pipelineDesc.vertexLayout.attributes = {
        {0, "pos", VertexFormat::Float2, 0},
        {1, "color", VertexFormat::UByte4Norm, 8},
        {2, "uv", VertexFormat::Float2, 12},
    };
    pipelineDesc.blend.enabled = blending;
    pipelineDesc.rasterizer.scissorEnabled = true;
    scene.pipeline.reset(
        scene.device->createPipeline(pipelineDesc, scene.vs.get(), scene.fs.get()));
    BufferDesc bufferDesc{BufferUsage::Vertex, sizeof(SoftVertex) * 6};
    scene.vertices.reset(scene.device->createBuffer(bufferDesc));
    scene.target.reset(
        scene.device->createRenderTarget({8, 8, TextureFormat::Rgba8, false}));
    scene.commands.reset(scene.device->createCommandBuffer());
    return scene;
}

/// Two triangles covering (0,0)-(x1,y1), flat color, uv = pos/8.
void writeQuad(SoftVertex* out, float x1, float y1, std::uint8_t r,
               std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const float x0 = 0, y0 = 0;
    const SoftVertex v00{x0, y0, {r, g, b, a}, 0, 0};
    const SoftVertex v10{x1, y0, {r, g, b, a}, 1, 0};
    const SoftVertex v11{x1, y1, {r, g, b, a}, 1, 1};
    const SoftVertex v01{x0, y1, {r, g, b, a}, 0, 1};
    out[0] = v00;
    out[1] = v10;
    out[2] = v11;
    out[3] = v00;
    out[4] = v11;
    out[5] = v01;
}

void runQuad(SoftScene& scene, const ClearValue& clear,
             RenderTexture* texture = nullptr) {
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), clear);
    scene.commands->bindPipeline(scene.pipeline.get());
    scene.commands->setScissor(Rect{0, 0, 8, 8});
    scene.commands->bindTexture(0, texture);
    scene.commands->bindVertexBuffer(0, scene.vertices.get());
    scene.commands->draw(6);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
}

} // namespace

TEST_CASE(render_soft_clear_fills_target) {
    SoftScene scene = makeSoftScene(true);
    CHECK(scene.device != nullptr);
    if (!scene.device)
        return;
    ClearValue clear;
    clear.r = 1;
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), clear);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
    const auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->pixels()[0], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[63], 0xFFFF0000u);
}

TEST_CASE(render_soft_solid_quad_blend) {
    SoftScene scene = makeSoftScene(true);
    CHECK(scene.device != nullptr);
    if (!scene.device)
        return;
    // Opaque red quad covering the left half over a black clear.
    SoftVertex verts[6];
    writeQuad(verts, 4, 8, 255, 0, 0, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    ClearValue black;
    runQuad(scene, black);
    const auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->pixels()[0], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[3], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[4], 0xFF000000u);
    CHECK_EQ(cpu->pixels()[63], 0xFF000000u);

    // Semi-transparent blue over the result: legacy integer source-over.
    writeQuad(verts, 4, 8, 0, 0, 255, 127);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), black);
    scene.commands->bindPipeline(scene.pipeline.get());
    scene.commands->setScissor(Rect{0, 0, 8, 8});
    scene.commands->bindVertexBuffer(0, scene.vertices.get());
    scene.commands->draw(6);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
    CHECK_EQ(cpu->pixels()[0], 0xFF00007Fu);
}

TEST_CASE(render_soft_diagonal_not_blended_twice) {
    SoftScene scene = makeSoftScene(true);
    CHECK(scene.device != nullptr);
    if (!scene.device)
        return;
    // Full-target quad: the two triangles share the diagonal; the top-left
    // rule must assign each diagonal pixel to exactly one triangle.
    SoftVertex verts[6];
    writeQuad(verts, 8, 8, 0, 0, 255, 128);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    ClearValue black;
    runQuad(scene, black);
    const auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    // Single blend: (255*128 + 0)/255 = 128; a double blend would give
    // (255*128 + 128*127)/255 = 191.
    for (int i = 0; i < 8; ++i)
        CHECK_EQ(cpu->pixels()[i * 8 + i], 0xFF000080u);
}

TEST_CASE(render_soft_texture_modulates_color) {
    SoftScene scene = makeSoftScene(true);
    CHECK(scene.device != nullptr);
    if (!scene.device)
        return;
    TextureDesc texDesc;
    texDesc.width = 1;
    texDesc.height = 1;
    std::unique_ptr<RenderTexture> texture(scene.device->createTexture(texDesc));
    const std::uint8_t gray[4] = {255, 255, 255, 128}; // white at 50% alpha
    CHECK(texture && texture->write(gray, sizeof(gray)));
    SoftVertex verts[6];
    writeQuad(verts, 8, 8, 255, 255, 255, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    ClearValue black;
    runQuad(scene, black, texture.get());
    const auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->pixels()[0], 0xFF808080u);
}
#endif // FIRMIR_RENDER_TEST_SOFT


#ifdef FIRMIR_RENDER_TEST_OPENGL
TEST_CASE(render_opengl_clear_quad_and_texture) {
    SoftScene scene = makeSoftScene(true, "opengl");
    if (!scene.device) {
        std::fprintf(stderr, "render_opengl: no EGL context; test skipped\n");
        return;
    }
    CHECK_EQ(scene.device->backendName(), "opengl");
    CHECK(scene.pipeline && scene.vertices && scene.target && scene.commands);
    SoftVertex verts[6];
    writeQuad(verts, 4, 8, 255, 0, 0, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    ClearValue black;
    runQuad(scene, black);
    const auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->pixels()[0], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[3], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[4], 0xFF000000u);
    CHECK_EQ(cpu->pixels()[63], 0xFF000000u);

    // Scissor uses top-left API coordinates and must be flipped for GL.
    writeQuad(verts, 8, 8, 0, 255, 0, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), black);
    scene.commands->setViewport(Viewport{0, 0, 8, 8, 0, 1});
    scene.commands->setScissor(Rect{2, 2, 2, 2});
    scene.commands->bindPipeline(scene.pipeline.get());
    scene.commands->bindVertexBuffer(0, scene.vertices.get());
    scene.commands->draw(6);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
    CHECK_EQ(cpu->pixels()[0], 0xFF000000u);
    CHECK_EQ(cpu->pixels()[2 * 8 + 2], 0xFF00FF00u);
    CHECK_EQ(cpu->pixels()[3 * 8 + 3], 0xFF00FF00u);
    CHECK_EQ(cpu->pixels()[4 * 8 + 4], 0xFF000000u);

    // A textured quad multiplies vertex color by the texel on the GPU.
    TextureDesc texDesc;
    texDesc.width = 1;
    texDesc.height = 1;
    std::unique_ptr<RenderTexture> texture(scene.device->createTexture(texDesc));
    const std::uint8_t whiteHalfAlpha[4] = {255, 255, 255, 128};
    CHECK(texture && texture->write(whiteHalfAlpha, sizeof(whiteHalfAlpha)));
    writeQuad(verts, 8, 8, 255, 255, 255, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    runQuad(scene, black, texture.get());
    const std::uint32_t pixel = cpu->pixels()[0];
    CHECK_EQ(pixel >> 24, 0xFFu);
    CHECK(((pixel >> 16) & 0xff) >= 0x7f);
    CHECK(((pixel >> 16) & 0xff) <= 0x81);
}
#endif // FIRMIR_RENDER_TEST_OPENGL

#ifdef FIRMIR_RENDER_TEST_VULKAN
TEST_CASE(render_vulkan_clear_quad_scissor_texture_and_indices) {
    SoftScene scene = makeSoftScene(true, "vulkan");
    if (!scene.device) {
        std::fprintf(stderr, "render_vulkan: no Vulkan device; test skipped\n");
        return;
    }
    CHECK_EQ(scene.device->backendName(), "vulkan");
    CHECK(scene.pipeline && scene.vertices && scene.target && scene.commands);
    SoftVertex verts[6];
    writeQuad(verts, 4, 8, 255, 0, 0, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    ClearValue black;
    runQuad(scene, black);
    auto* cpu = dynamic_cast<CpuRenderTarget*>(scene.target.get());
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->pixels()[0], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[3], 0xFFFF0000u);
    CHECK_EQ(cpu->pixels()[4], 0xFF000000u);

    writeQuad(verts, 8, 8, 0, 255, 0, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), black);
    scene.commands->setScissor(Rect{2, 2, 2, 2});
    scene.commands->bindPipeline(scene.pipeline.get());
    scene.commands->bindVertexBuffer(0, scene.vertices.get());
    scene.commands->draw(6);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
    CHECK_EQ(cpu->pixels()[0], 0xFF000000u);
    CHECK_EQ(cpu->pixels()[2 * 8 + 2], 0xFF00FF00u);
    CHECK_EQ(cpu->pixels()[4 * 8 + 4], 0xFF000000u);

    TextureDesc textureDesc;
    std::unique_ptr<RenderTexture> texture(scene.device->createTexture(textureDesc));
    const std::uint8_t whiteHalfAlpha[4] = {255, 255, 255, 128};
    CHECK(texture && texture->write(whiteHalfAlpha, sizeof(whiteHalfAlpha)));
    writeQuad(verts, 8, 8, 255, 255, 255, 255);
    CHECK(scene.vertices->write(verts, sizeof(verts)));
    runQuad(scene, black, texture.get());
    const std::uint32_t pixel = cpu->pixels()[0];
    CHECK_EQ(pixel >> 24, 0xFFu);
    CHECK(((pixel >> 16) & 0xff) >= 0x7f);
    CHECK(((pixel >> 16) & 0xff) <= 0x81);

    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::unique_ptr<RenderBuffer> indexBuffer(scene.device->createBuffer(
        BufferDesc{BufferUsage::Index, sizeof(indices)}));
    CHECK(indexBuffer && indexBuffer->write(indices, sizeof(indices)));
    scene.commands->begin();
    scene.commands->beginRenderPass(scene.target.get(), black);
    scene.commands->setScissor(Rect{0, 0, 8, 8});
    scene.commands->bindPipeline(scene.pipeline.get());
    scene.commands->bindVertexBuffer(0, scene.vertices.get());
    scene.commands->bindIndexBuffer(indexBuffer.get(), IndexFormat::UInt16);
    scene.commands->drawIndexed(6);
    scene.commands->endRenderPass();
    scene.commands->end();
    scene.device->submit(scene.commands.get());
    CHECK(cpu->pixels() != nullptr);
    indexBuffer.reset();
    texture.reset();
    scene.commands.reset();
    scene.target.reset();
    scene.vertices.reset();
    scene.pipeline.reset();
    scene.fs.reset();
    scene.vs.reset();
    scene.device.reset();
    Render::shutdown();
}
#endif // FIRMIR_RENDER_TEST_VULKAN
