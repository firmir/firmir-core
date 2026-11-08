#include <firmir/multimedia/videotexture.hpp>

#include <firmir/render/device.hpp>
#include <firmir/render/types.hpp>

namespace Firmir {
namespace Multimedia {

namespace {

/// Builds the render-level import descriptor from a GPU frame handle.
Render::DmaBufTextureDesc dmaBufDescFrom(const GpuFrameHandle& gpu) {
    Render::DmaBufTextureDesc desc;
    desc.width = gpu.width;
    desc.height = gpu.height;
    desc.fourcc = gpu.drmFormat();
    desc.modifier = gpu.drmModifier();
    desc.planeCount = gpu.planeCount();
    for (int i = 0; i < desc.planeCount && i < 4; ++i) {
        desc.planes[i].fd = gpu.planeFd(i);
        desc.planes[i].offset = gpu.planeOffset(i);
        desc.planes[i].stride = gpu.planeStride(i);
    }
    switch (gpu.colorSpace) {
    case VideoColorSpace::Bt601:
        desc.ycbcrModel = Render::YcbcrModel::Bt601;
        break;
    case VideoColorSpace::Bt2020:
        desc.ycbcrModel = Render::YcbcrModel::Bt2020;
        break;
    default:
        desc.ycbcrModel = Render::YcbcrModel::Bt709;
        break;
    }
    desc.ycbcrRange = gpu.colorRange == VideoColorRange::Full
                          ? Render::YcbcrRange::Full
                          : Render::YcbcrRange::Limited;
    return desc;
}

} // namespace

bool VideoTextureCache::draw(Gui::Painter& painter, const Gui::RectF& target,
                             const VideoFrame& frame,
                             std::uint64_t generation) {
    if (m_unsupported || !frame.gpu)
        return false;
    Render::RenderDevice* device = painter.device();
    if (!device || !device->supportsDmaBufTextures()) {
        m_unsupported = true; // don't re-probe on every frame
        return false;
    }
    const void* key = reinterpret_cast<const void*>(frame.gpu->poolKey());
    if (!key)
        return false;
    Entry& entry = m_entries[key];
    if (!entry.texture || entry.generation != generation) {
        entry.texture.reset(device->importDmaBufTexture(dmaBufDescFrom(*frame.gpu)));
        entry.generation = generation;
        // Bound the cache: a handful of decoder pool surfaces at a time.
        if (m_entries.size() > 16)
            m_entries.clear(); // simple reset; survivors re-import
        if (!entry.texture)
            return false;
    }
    return painter.drawTexture(target, entry.texture.get());
}

void VideoTextureCache::clear() {
    m_entries.clear();
}

} // namespace Multimedia
} // namespace Firmir
