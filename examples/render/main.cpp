// firmir-render example: records one indexed triangle draw through the
// selected backend plugin (default: the first loadable of vulkan/opengl/soft).

#include <firmir/render/render.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Firmir::Render;

namespace {

struct Vertex {
    float pos[3];
    std::uint8_t color[4];
};

std::vector<std::uint8_t> code(const char* text) {
    std::vector<std::uint8_t> out;
    while (*text)
        out.push_back(static_cast<std::uint8_t>(*text++));
    return out;
}

} // namespace

int main() {
    auto device = Render::createDevice();
    if (!device) {
        std::fprintf(stderr, "no render backend — set FIRMIR_RENDER_BACKEND / "
                             "FIRMIR_RENDER_PLUGIN_PATH\n");
        return 1;
    }

    const std::vector<Vertex> vertices = {
        {{0.0f, 0.5f, 0.0f}, {255, 0, 0, 255}},
        {{-0.5f, -0.5f, 0.0f}, {0, 255, 0, 255}},
        {{0.5f, -0.5f, 0.0f}, {0, 0, 255, 255}},
    };
    const std::vector<std::uint16_t> indices = {0, 1, 2};

    BufferDesc vertexDesc{BufferUsage::Vertex,
                          sizeof(Vertex) * vertices.size()};
    std::unique_ptr<RenderBuffer> vertexBuffer(
        device->createBuffer(vertexDesc));
    BufferDesc indexDesc{BufferUsage::Index,
                         sizeof(std::uint16_t) * indices.size()};
    std::unique_ptr<RenderBuffer> indexBuffer(
        device->createBuffer(indexDesc));
    if (!vertexBuffer || !indexBuffer ||
        !vertexBuffer->write(vertices.data(), vertexDesc.size) ||
        !indexBuffer->write(indices.data(), indexDesc.size))
        return 1;

    ShaderDesc vs{ShaderStage::Vertex, ShaderLanguage::Glsl, "main",
                  code("vertex")};
    ShaderDesc fs{ShaderStage::Fragment, ShaderLanguage::Glsl, "main",
                  code("fragment")};
    std::unique_ptr<RenderShader> vertexShader(device->createShader(vs));
    std::unique_ptr<RenderShader> fragmentShader(device->createShader(fs));

    PipelineDesc pipelineDesc;
    pipelineDesc.vertexLayout.stride = sizeof(Vertex);
    pipelineDesc.vertexLayout.attributes = {
        {0, "in_pos", VertexFormat::Float3, offsetof(Vertex, pos)},
        {1, "in_color", VertexFormat::UByte4Norm, offsetof(Vertex, color)},
    };
    std::unique_ptr<RenderPipeline> pipeline(device->createPipeline(
        pipelineDesc, vertexShader.get(), fragmentShader.get()));
    if (!pipeline)
        return 1;

    std::unique_ptr<RenderTarget> target(
        device->createRenderTarget({64, 64, TextureFormat::Rgba8, false}));
    std::unique_ptr<RenderCommandBuffer> commands(
        device->createCommandBuffer());
    if (!target || !commands)
        return 1;

    commands->begin();
    commands->beginRenderPass(target.get(), ClearValue{0, 0, 0, 1});
    commands->setViewport(Viewport{0, 0, 64, 64, 0, 1});
    commands->bindPipeline(pipeline.get());
    commands->bindVertexBuffer(0, vertexBuffer.get());
    commands->bindIndexBuffer(indexBuffer.get(), IndexFormat::UInt16);
    commands->drawIndexed(static_cast<std::uint32_t>(indices.size()));
    commands->endRenderPass();
    commands->end();
    device->submit(commands.get());
    device->waitIdle();

    std::printf("recorded one indexed triangle via %s\n",
                device->backendName().c_str());
    return 0;
}
