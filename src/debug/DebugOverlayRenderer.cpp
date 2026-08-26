#include "../../src/debug/DebugOverlayRenderer.hpp"

const char *DebugOverlayRenderer::BLOCK_NAMES[16] = {"AIR", "GRASS", "DIRT",
	"STONE", "SHRUB", "OAK LOG", "LEAVES", "CACTUS", "WATER", "BEDROCK", "SAND",
	"SNOW", "PERMAFROST", "CANYON ROCK", "SLATE", "MOSS ROCK"};

DebugOverlayRenderer::DebugOverlayRenderer()
{
}
DebugOverlayRenderer::DebugOverlayRenderer(const DebugOverlayRenderer &other)
{
	(void)other;
}
DebugOverlayRenderer::~DebugOverlayRenderer()
{
}
DebugOverlayRenderer &DebugOverlayRenderer::operator=(const DebugOverlayRenderer &other)
{
	(void)other;
	return (*this);
}

void DebugOverlayRenderer::draw_crosshair(ft_render_framebuffer &framebuffer)
{
	int32_t	cx;
	int32_t	cy;

	cx = framebuffer.width / 2;
	cy = framebuffer.height / 2;
	for (int32_t o = -8; o <= 8; ++o)
	{
		if (cx + o >= 0 && cx + o < framebuffer.width)
			framebuffer.pixels[static_cast<size_t>(cy * framebuffer.width + cx
					+ o)] = RendererColor::rgba(245U, 245U, 235U);
		if (cy + o >= 0 && cy + o < framebuffer.height)
			framebuffer.pixels[static_cast<size_t>((cy + o) * framebuffer.width
					+ cx)] = RendererColor::rgba(245U, 245U, 235U);
	}
}

void DebugOverlayRenderer::draw_debug_text(ft_render_framebuffer &framebuffer,
	int32_t origin_x, int32_t origin_y, const char *text, int32_t scale)
{
	(void)scale;
	if (!text || !framebuffer.pixels)
		return ;
	FontRenderer &fr = FontRenderer::instance();
	if (fr.is_loaded())
		fr.draw_text(framebuffer.pixels, framebuffer.width, framebuffer.height,
			origin_x, origin_y, text, 0xEEEEFFU);
}

void DebugOverlayRenderer::draw_line(ft_render_framebuffer &fb, int32_t x,
	int32_t &line, int32_t line_height, const char *text, int32_t scale)
{
	draw_debug_text(fb, x, 12 + line_height * line, text, scale);
	++line;
}

void DebugOverlayRenderer::draw_overlay_perf(ft_render_framebuffer &fb,
	const RenderDebug *d, int32_t x, int32_t &line, int32_t lh, int32_t scale)
{
	char	text[64];

	std::snprintf(text, sizeof(text), "FPS  %.1f", d->fps);
	draw_line(fb, x, line, lh, text, scale);
	std::snprintf(text, sizeof(text), "FRAME MS  %.1f", d->frame_ms);
	draw_line(fb, x, line, lh, text, scale);
}

void DebugOverlayRenderer::draw_overlay_world(ft_render_framebuffer &fb,
	const RenderDebug *d, int32_t x, int32_t &line, int32_t lh, int32_t scale)
{
	char		text[64];
	const char	*bname;

	bname = d->selected_block_id < 16U
		? BLOCK_NAMES[d->selected_block_id] : "UNKNOWN";
	draw_line(fb, x, line, lh, "FOV  80 DEG", scale);
	draw_line(fb, x, line, lh, "WORLD  16384 X 256 X 16384", scale);
	std::snprintf(text, sizeof(text), "VISIBLE CHUNKS  %d", d->visible_chunks);
	draw_line(fb, x, line, lh, text, scale);
	std::snprintf(text, sizeof(text), "LOADED CHUNKS  %d", d->loaded_chunks);
	draw_line(fb, x, line, lh, text, scale);
	std::snprintf(text, sizeof(text), "RENDER DIST  %d  FLOOR 14",
		d->render_distance);
	draw_line(fb, x, line, lh, text, scale);
	std::snprintf(text, sizeof(text), "BLOCK  %s", bname);
	draw_line(fb, x, line, lh, text, scale);
}

void DebugOverlayRenderer::draw_overlay_camera(ft_render_framebuffer &fb,
	const RenderDebug *d, int32_t x, int32_t &line, int32_t lh, int32_t scale)
{
	char	text[64];

	std::snprintf(text, sizeof(text), "X %.1f  Y %.1f  Z %.1f", d->camera_x,
		d->camera_y, d->camera_z);
	draw_line(fb, x, line, lh, text, scale);
	if (d->camera_speed > 0.0)
	{
		std::snprintf(text, sizeof(text), "SPEED  %.0f  BOOST X20  %.0f",
			d->camera_speed, d->boost_speed);
		draw_line(fb, x, line, lh, text, scale);
	}
}

void DebugOverlayRenderer::draw_overlay_info(ft_render_framebuffer &fb,
	const RenderDebug *d, int32_t x, int32_t &line, int32_t lh, int32_t scale)
{
	char	text[64];

	if (d->biome_name[0] != '\0')
	{
		std::snprintf(text, sizeof(text), "BIOME  %s", d->biome_name);
		draw_line(fb, x, line, lh, text, scale);
	}
	if (d->seed[0] != '\0')
	{
		std::snprintf(text, sizeof(text), "SEED  %.16s", d->seed);
		draw_line(fb, x, line, lh, text, scale);
	}
	if (d->ram_mb > 0U)
	{
		std::snprintf(text, sizeof(text), "RAM  %u MB", d->ram_mb);
		draw_line(fb, x, line, lh, text, scale);
	}
	if (d->vram_approx_mb > 0U)
	{
		std::snprintf(text, sizeof(text), "VRAM  %u MB", d->vram_approx_mb);
		draw_line(fb, x, line, lh, text, scale);
	}
}

void DebugOverlayRenderer::draw_revision_cell(ft_render_framebuffer &fb,
	int32_t x, int32_t z, int32_t radius, int32_t cell, int32_t origin_x,
	int32_t origin_y, uint8_t state)
{
	uint32_t	color;
	int32_t		px;
	int32_t		py;

	color = RendererColor::rgba(70U, 70U, 70U);
	if (state == 1U)
		color = RendererColor::rgba(210U, 75U, 75U);
	else if (state == 2U)
		color = RendererColor::rgba(75U, 190U, 85U);
	else if (state == 3U)
		color = RendererColor::rgba(220U, 185U, 70U);
	px = origin_x + (x + radius) * cell;
	py = origin_y + (z + radius) * cell;
	for (int32_t sy = 0; sy < cell - 1; ++sy)
		for (int32_t sx = 0; sx < cell - 1; ++sx)
			if (px + sx >= 0 && px + sx < fb.width && py + sy >= 0 && py
				+ sy < fb.height)
				fb.pixels[(py + sy) * fb.width + px + sx] = color;
}

void DebugOverlayRenderer::draw_revision_grid(ft_render_framebuffer &fb,
	const RenderDebug *d, int32_t radius, int32_t cell, int32_t origin_x,
	int32_t origin_y)
{
	int32_t	map_index;
	uint8_t	state;

	for (int32_t z = -radius; z <= radius; ++z)
	{
		for (int32_t x = -radius; x <= radius; ++x)
		{
			map_index = (z + radius) * (radius * 2 + 1) + x + radius;
			state = d->revision_map[map_index];
			draw_revision_cell(fb, x, z, radius, cell, origin_x, origin_y,
				state);
		}
	}
}

void DebugOverlayRenderer::draw_revision_preview(ft_render_framebuffer &fb,
	const RenderDebug *d)
{
	int32_t	radius;
	int32_t	cell;
	int32_t	origin_x;
	int32_t	origin_y;
	char	text[128];

	if (d == nullptr || !d->revision_preview_visible
		|| d->revision_map_radius == 0)
		return ;
	radius = d->revision_map_radius;
	cell = 12;
	origin_x = 285;
	origin_y = 18;
	draw_revision_grid(fb, d, radius, cell, origin_x, origin_y);
	std::snprintf(text, sizeof(text), "%s  P:%u S:%u T:%u U:%u",
		d->revision_pending ? "REVISION PENDING" : "REVISION PREVIEW",
		d->revision_protected_count, d->revision_selected_count,
		d->revision_transition_count, d->revision_unchanged_count);
	draw_debug_text(fb, origin_x, origin_y + (radius * 2 + 1) * cell + 8, text,
		2);
	draw_debug_text(fb, origin_x, origin_y + (radius * 2 + 1) * cell + 28,
		"RED PROTECTED  GREEN SELECTED  GOLD TRANSITION", 2);
}

void DebugOverlayRenderer::draw_debug_overlay(ft_render_framebuffer &framebuffer,
	const RenderDebug *debug)
{
	int32_t	scale;
	int32_t	lh;
	int32_t	x;
	int32_t	line;

	if (!debug)
		return ;
	scale = (framebuffer.width < 900 || framebuffer.height < 520) ? 2 : 3;
	lh = 8 * scale + 8;
	/* +~2mm extra spacing between each overlay line */
	x = 12;
	line = 0;
	draw_overlay_perf(framebuffer, debug, x, line, lh, scale);
	draw_overlay_world(framebuffer, debug, x, line, lh, scale);
	draw_overlay_camera(framebuffer, debug, x, line, lh, scale);
	draw_overlay_info(framebuffer, debug, x, line, lh, scale);
	draw_revision_preview(framebuffer, debug);
}
