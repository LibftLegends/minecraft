#ifndef DEBUG_OVERLAY_RENDERER_HPP
#define DEBUG_OVERLAY_RENDERER_HPP

#include "../ft_vox.hpp"
#include "../../src/debug/RenderDebug.hpp"
#include "../../src/font/FontRenderer.hpp"
#include "../../src/frame/RendererColor.hpp"

class DebugOverlayRenderer
{
  public:
    DebugOverlayRenderer();
    DebugOverlayRenderer(const DebugOverlayRenderer &other);
    ~DebugOverlayRenderer();
    DebugOverlayRenderer &operator=(const DebugOverlayRenderer &other);

    static void draw_crosshair(ft_render_framebuffer &framebuffer);
    static void draw_debug_overlay(ft_render_framebuffer &framebuffer, const RenderDebug *debug);

  private:
    static const uint8_t DIGIT_GLYPHS[37][5];
    static const char *BLOCK_NAMES[16];

    static const uint8_t *digit_bitmap(char character);
    static void draw_debug_char(ft_render_framebuffer &framebuffer, int32_t origin_x,
                                int32_t origin_y, char character, int32_t scale);
    static void draw_debug_text(ft_render_framebuffer &framebuffer, int32_t origin_x,
                                int32_t origin_y, const char *text, int32_t scale);

    static void draw_line(ft_render_framebuffer &fb, int32_t x, int32_t &line, int32_t line_height,
                          const char *text, int32_t scale);
    static void draw_overlay_perf(ft_render_framebuffer &fb, const RenderDebug *d, int32_t x,
                                  int32_t &line, int32_t lh, int32_t scale);
    static void draw_overlay_world(ft_render_framebuffer &fb, const RenderDebug *d, int32_t x,
                                   int32_t &line, int32_t lh, int32_t scale);
    static void draw_overlay_camera(ft_render_framebuffer &fb, const RenderDebug *d, int32_t x,
                                    int32_t &line, int32_t lh, int32_t scale);
    static void draw_overlay_info(ft_render_framebuffer &fb, const RenderDebug *d, int32_t x,
                                   int32_t &line, int32_t lh, int32_t scale);
    static void draw_revision_preview(ft_render_framebuffer &fb, const RenderDebug *d);
};

#endif
