#pragma once

#include <firmir/gui/events.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Firmir {
namespace Render {
class RenderCommandBuffer;
class RenderDevice;
class RenderTarget;
} // namespace Render

namespace Gui {

/**
 * @brief Per-render-device resources shared by Painters.
 *
 * Owns the compiled pipelines, the dynamic vertex buffer and the glyph atlas
 * texture. A window holds one instance for its render device; it must
 * outlive every command buffer submission recorded through its Painters
 * (the recorded commands reference these resources).
 */
class RenderResources {
public:
    explicit RenderResources(Render::RenderDevice* device);
    ~RenderResources();

    RenderResources(const RenderResources&) = delete;
    RenderResources& operator=(const RenderResources&) = delete;

    Render::RenderDevice* device() const;

private:
    class Impl;
    std::unique_ptr<Impl> d;

    friend class Painter;
};

/// RGBA color, 8 bits per channel, non-premultiplied.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return {r, g, b, 255};
    }
    static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                std::uint8_t a) {
        return {r, g, b, a};
    }

    /// Parses "#RRGGBB" or "#AARRGGBB".
    /// @throws std::invalid_argument on a malformed string.
    static Color fromString(const std::string& text);

    /// Returns the color with the RGB channels multiplied by @p factor
    /// (clamped), QML Qt.lighter() analogue; factor defaults to 1.5.
    Color lighter(double factor = 1.5) const;
    /// Returns the color with the RGB channels divided by @p factor, QML
    /// Qt.darker() analogue; factor defaults to 2.0.
    Color darker(double factor = 2.0) const;

    friend bool operator==(const Color& lhs, const Color& rhs) {
        return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b &&
               lhs.a == rhs.a;
    }
    friend bool operator!=(const Color& lhs, const Color& rhs) {
        return !(lhs == rhs);
    }
};

/**
 * @brief Immediate-mode 2D painter recording into a firmir-render command
 * buffer.
 *
 * The window render thread paints the item scene graph with it: every
 * primitive becomes triangles in a dynamic vertex buffer, executed by the
 * active render backend (the "soft" CPU backend by default); display
 * backends only present the resulting pixels, so any display backend
 * (DRM/KMS, Wayland shm, headless) works without GPU-specific code.
 *
 * Coordinates are target pixels with the origin at the top-left corner.
 * Supports a state stack with translation offset, clip rectangle and
 * multiplicative opacity. All painting is source-over alpha blending.
 *
 * The command buffer must be recording a render pass on @p target
 * (beginRenderPass) for the painter's lifetime.
 */
class Painter {
public:
    Painter(RenderResources& resources, Render::RenderCommandBuffer* commands,
            Render::RenderTarget* target);
    ~Painter();

    Painter(const Painter&) = delete;
    Painter& operator=(const Painter&) = delete;
    Painter(Painter&&) noexcept;
    Painter& operator=(Painter&&) noexcept;

    int width() const;
    int height() const;

    /// Pushes the current state (offset, clip, opacity).
    void save();
    /// Pops the state stack; no-op when the stack has a single entry.
    void restore();

    /// Translates the coordinate system (accumulates with saved states).
    void translate(float dx, float dy);
    /// Rotates the coordinate system clockwise by @p degrees.
    void rotate(float degrees);
    /// Scales both coordinate axes (negative values mirror the axis).
    void scale(float sx, float sy);
    /// Intersects the clip region with @p rect (in current coordinates).
    void clipRect(const RectF& rect);

    /// Current opacity multiplier (0..1], combined down the stack.
    float opacity() const;
    void setOpacity(float opacity);
    void multiplyOpacity(float factor);

    /// Fills @p rect (current coordinates) with @p color, alpha-blended.
    void fillRect(const RectF& rect, Color color);
    /// Fills the whole target with @p color, ignoring the clip and opacity
    /// state (overwrite, not blended).
    void clear(Color color);

    /**
     * @brief Draws UTF-8 @p text with the built-in 8x8 bitmap font.
     *
     * The embedded font is font8x8 by Daniel Hepper (Public Domain, see
     * src/detail/font8x8.h). One glyph cell is (8*@p scale) x (8*@p scale)
     * pixels; '\n' starts a new line; non-ASCII code points become '?'.
     */
    void drawText(float x, float y, const std::string& text, Color color,
                  int scale = 1);
    /// Draws an RGBA8 bitmap. cacheKey identifies the owning item and
    /// generation invalidates the device-lifetime texture after CPU changes.
    void drawRgba(const RectF& target, const void* rgba, int imageWidth,
                  int imageHeight, const void* cacheKey, std::uint64_t generation,
                  Color tint = Color::rgb(255, 255, 255));
    /// Draws a quad with a user fragment main body. Available variables follow
    /// the OpenGL path: v_color, v_uv, u_texture, u_textured, gl_FragColor.
    void drawShader(const RectF& target, const std::string& fragmentMain,
                    const void* cacheKey, Color tint,
                    const void* rgba = nullptr, int imageWidth = 0,
                    int imageHeight = 0, std::uint64_t generation = 0);
    /// Pixel size of the drawText() output for @p text at @p scale.
    static void textSize(const std::string& text, int scale, float& width,
                         float& height);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
