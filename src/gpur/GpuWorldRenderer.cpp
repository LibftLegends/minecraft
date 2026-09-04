#include "../../src/gpur/GpuWorldRenderer.hpp"
#include "../../src/diagnostics/RuntimeAnalytics.hpp"
#include <cstdio>

GpuWorldRenderer::GpuWorldRenderer() : _sky_vao(0), _crosshair_vao(0),
	_crosshair_vbo(0), _shadow_vao(0), _shadow_vbo(0), _width(0), _height(0), _u_mvp(-1), _u_chunk_offset(-1),
	_u_atlas(-1), _u_atlas_loaded(-1), _u_tile_uvs(-1), _u_fallback(-1),
	_u_sky_darkening(-1),
	_u_sky_size(-1), _u_crosshair_color(-1), _u_shadow_mvp(-1),
	_u_shadow_center(-1), _u_shadow_radius(-1), _u_shadow_y(-1),
	_u_shadow_alpha(-1)
{
	std::memset(_tile_uvs, 0, sizeof(_tile_uvs));
	std::memset(_fallback_colors, 0, sizeof(_fallback_colors));
}

GpuWorldRenderer::GpuWorldRenderer(const GpuWorldRenderer &other) : _sky_vao(0),
	_crosshair_vao(0), _crosshair_vbo(0), _shadow_vao(0), _shadow_vbo(0), _width(0), _height(0), _u_mvp(-1),
	_u_chunk_offset(-1), _u_atlas(-1), _u_atlas_loaded(-1), _u_tile_uvs(-1),
	_u_fallback(-1), _u_sky_darkening(-1), _u_sky_size(-1),
	_u_crosshair_color(-1), _u_shadow_mvp(-1), _u_shadow_center(-1),
	_u_shadow_radius(-1), _u_shadow_y(-1), _u_shadow_alpha(-1)
{
	(void)other;
	std::memset(_tile_uvs, 0, sizeof(_tile_uvs));
	std::memset(_fallback_colors, 0, sizeof(_fallback_colors));
}

GpuWorldRenderer::~GpuWorldRenderer()
{
	destroy();
}

GpuWorldRenderer &GpuWorldRenderer::operator=(const GpuWorldRenderer &other)
{
	(void)other;
	return (*this);
}

bool GpuWorldRenderer::compile_shader_from_file(ft_gpu_shader &shader,
	const char *vert_path, const char *frag_path)
{
	std::string vert = GlslLoader::load(vert_path);
	std::string frag = GlslLoader::load(frag_path);
	if (vert.empty() || frag.empty())
		return (false);
	return (shader.initialize(vert.c_str(), frag.c_str()) == FT_ERR_SUCCESS);
}

bool GpuWorldRenderer::compile_shaders(const std::string &d)
{
	const std::string world_vert = d + "world.vert.glsl";
	const std::string world_frag = d + "world.frag.glsl";
	const std::string sky_vert = d + "sky.vert.glsl";
	const std::string sky_frag = d + "sky.frag.glsl";
	const std::string cross_vert = d + "overlay.vert.glsl";
	const std::string cross_frag = d + "overlay.frag.glsl";
	const std::string shadow_vert = d + "shadow.vert.glsl";
	const std::string shadow_frag = d + "shadow.frag.glsl";
	if (!compile_shader_from_file(_world_shader, world_vert.c_str(),
			world_frag.c_str()))
		return (false);
	if (!compile_shader_from_file(_sky_shader, sky_vert.c_str(),
			sky_frag.c_str()))
		return (false);
	if (!compile_shader_from_file(_crosshair_shader, cross_vert.c_str(),
			cross_frag.c_str()))
		return (false);
	if (!compile_shader_from_file(_shadow_shader, shadow_vert.c_str(),
			shadow_frag.c_str()))
		return (false);
	return (true);
}

void GpuWorldRenderer::cache_uniforms()
{
	_u_mvp = _world_shader.uniform("u_mvp");
	_u_chunk_offset = _world_shader.uniform("u_chunk_offset");
	_u_atlas = _world_shader.uniform("u_atlas");
	_u_atlas_loaded = _world_shader.uniform("u_atlas_loaded");
	_u_tile_uvs = _world_shader.uniform("u_tile_uvs");
	_u_fallback = _world_shader.uniform("u_fallback_colors");
	_u_sky_darkening = _world_shader.uniform("u_sky_darkening");
	_u_sky_size = _sky_shader.uniform("u_sky_size");
	_u_crosshair_color = _crosshair_shader.uniform("u_overlay_color");
	_u_shadow_mvp = _shadow_shader.uniform("u_mvp");
	_u_shadow_center = _shadow_shader.uniform("u_shadow_center");
	_u_shadow_radius = _shadow_shader.uniform("u_shadow_radius");
	_u_shadow_y = _shadow_shader.uniform("u_shadow_y");
	_u_shadow_alpha = _shadow_shader.uniform("u_shadow_alpha");
}

void GpuWorldRenderer::upload_atlas_uniforms()
{
	_world_shader.use();
	glUniform1i(_u_atlas, 0);
	glUniform1i(_u_atlas_loaded, _atlas.is_loaded() ? 1 : 0);
	glUniform4fv(_u_tile_uvs, 384, _tile_uvs);
	glUniform3fv(_u_fallback, 64, _fallback_colors);
	glUniform1i(_u_sky_darkening, 0);
}

bool GpuWorldRenderer::initialize(int width, int height, GLuint sky_vao,
	GLuint crosshair_vao, GLuint crosshair_vbo, const std::string &shader_dir)
{
	destroy();
	_sky_vao = sky_vao;
	_crosshair_vao = crosshair_vao;
	_crosshair_vbo = crosshair_vbo;
	glGenVertexArrays(1, &_shadow_vao);
	glGenBuffers(1, &_shadow_vbo);
	const float shadow_vertices[] = {-1.0f, -1.0f, 1.0f, -1.0f,
		1.0f, 1.0f, -1.0f, 1.0f};
	glBindVertexArray(_shadow_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _shadow_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(shadow_vertices)),
		shadow_vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
		static_cast<GLsizei>(2 * sizeof(float)), nullptr);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);
	if (!compile_shaders(shader_dir))
		return (false);
	cache_uniforms();
	if (!_batch.initialize())
		return (false);
	const TextureAtlas &cpu_atlas = TextureAtlas::instance();
	_atlas.upload(cpu_atlas);
	_atlas.fill_tile_uvs(cpu_atlas, _tile_uvs);
	_atlas.fill_fallback_colors(_fallback_colors);
	upload_atlas_uniforms();
	_width = width;
	_height = height;
	update_crosshair_geometry();
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	return (true);
}

void GpuWorldRenderer::destroy()
{
	_atlas.destroy();
	_batch.destroy();
	(void)_world_shader.destroy();
	(void)_sky_shader.destroy();
	(void)_crosshair_shader.destroy();
	(void)_shadow_shader.destroy();
	if (_shadow_vao != 0)
	{
		glDeleteVertexArrays(1, &_shadow_vao);
		_shadow_vao = 0;
	}
	if (_shadow_vbo != 0)
	{
		glDeleteBuffers(1, &_shadow_vbo);
		_shadow_vbo = 0;
	}
}

void GpuWorldRenderer::resize(int width, int height)
{
	if (_width == width && _height == height)
		return ;
	_width = width;
	_height = height;
	update_crosshair_geometry();
}

void GpuWorldRenderer::update_crosshair_geometry()
{
	const float	sx = 8.0f / static_cast<float>(_width);
	const float	sy = 8.0f / static_cast<float>(_height);
	const float	verts[] = {-sx, 0.0f, sx, 0.0f, 0.0f, -sy, 0.0f, sy};

	if (_width <= 0 || _height <= 0)
		return ;
	glBindVertexArray(_crosshair_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _crosshair_vbo);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(verts)), verts,
		GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(2
			* sizeof(float)), nullptr);
	glEnableVertexAttribArray(0);
	glBindVertexArray(0);
}

void GpuWorldRenderer::draw_sky() const
{
	_sky_shader.use();
	glUniform2f(_u_sky_size, static_cast<float>(_width),
		static_cast<float>(_height));
	glBindVertexArray(_sky_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void GpuWorldRenderer::draw_crosshair() const
{
	int32_t analytics_error;

	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_DRAW_CROSSHAIR);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: crosshair scope start failed (%d)\n",
			analytics_error);
	glBindVertexArray(_crosshair_vao);
	const float color[4] = {0.96f, 0.96f, 0.92f, 1.0f};
	_crosshair_shader.use();
	glUniform4fv(_u_crosshair_color, 1, color);
	glDisable(GL_DEPTH_TEST);
	glDrawArrays(GL_LINES, 0, 4);
	glEnable(GL_DEPTH_TEST);
	glBindVertexArray(0);
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: crosshair scope end failed (%d)\n",
			analytics_error);
}

static ft_bool gpu_shadow_solid_lookup(void *user_data, int32_t world_x,
	int32_t world_y, int32_t world_z) noexcept
{
	const World *world = static_cast<const World *>(user_data);
	if (world == nullptr)
		return (FT_FALSE);
	return (world->solid_block_at(world_x, world_y, world_z) ? FT_TRUE : FT_FALSE);
}

void GpuWorldRenderer::draw_player_shadow(const Camera &camera,
	const World &world, const float *mvp)
{
	const double entity_bottom_y = camera.y - 1.5;
	const int32_t world_x = static_cast<int32_t>(std::floor(camera.x));
	const int32_t world_z = static_cast<int32_t>(std::floor(camera.z));
	const int32_t start_y = static_cast<int32_t>(std::floor(entity_bottom_y));
	int32_t receiver_y = 0;
	if (voxel_shadow_find_receiver(world_x, start_y, world_z, 8,
			gpu_shadow_solid_lookup, const_cast<World *>(&world), &receiver_y)
		!= FT_ERR_SUCCESS)
		return ;
	const double alpha = voxel_shadow_height_fade(entity_bottom_y, receiver_y,
		8.0) * 0.42;
	if (alpha <= 0.0)
		return ;
	_shadow_shader.use();
	glUniformMatrix4fv(_u_shadow_mvp, 1, GL_FALSE, mvp);
	glUniform2f(_u_shadow_center, static_cast<float>(camera.x),
		static_cast<float>(camera.z));
	glUniform1f(_u_shadow_radius, 0.38f);
	glUniform1f(_u_shadow_y, static_cast<float>(receiver_y + 1) + 0.012f);
	glUniform1f(_u_shadow_alpha, static_cast<float>(alpha));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glBindVertexArray(_shadow_vao);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glBindVertexArray(0);
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
}

void GpuWorldRenderer::render(const Camera &camera, const World &world)
{
	float	mvp[16];
	float	far_z;
	int32_t analytics_error;
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	static bool startup_render_reported = false;
#endif

	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_CLEAR_SKY);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU clear scope start failed (%d)\n",
			analytics_error);
	glViewport(0, 0, _width, _height);
	glClear(GL_DEPTH_BUFFER_BIT);
	draw_sky();
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU clear scope end failed (%d)\n",
			analytics_error);
	far_z = static_cast<float>(world.active_render_distance) + 64.0f;
	GpuMvpBuilder::build(mvp, camera, _width, _height, 0.08f, far_z);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_BATCH_COLLECT);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU collect scope start failed (%d)\n",
			analytics_error);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_render_reported)
		std::fprintf(stderr, "[Startup] GPU world: before batch collect\n");
#endif
	_batch.collect(camera, world, _width, _height);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_render_reported)
		std::fprintf(stderr, "[Startup] GPU world: after batch collect\n");
#endif
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU collect scope end failed (%d)\n",
			analytics_error);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_SOLID_FLUSH);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU solid scope start failed (%d)\n",
			analytics_error);
	_world_shader.use();
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_render_reported)
		std::fprintf(stderr, "[Startup] GPU world: before solid flush\n");
#endif
	_batch.flush_solid(0, _u_mvp, _u_chunk_offset, mvp, _atlas);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_render_reported)
		std::fprintf(stderr, "[Startup] GPU world: after solid flush\n");
#endif
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU solid scope end failed (%d)\n",
			analytics_error);
	draw_player_shadow(camera, world, mvp);
	analytics_error = RuntimeAnalytics::begin_scope(
		RuntimeAnalyticsScope::GPU_WATER_FLUSH);
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU water scope start failed (%d)\n",
			analytics_error);
	_batch.flush_water(0, _u_mvp, _u_chunk_offset, mvp, _atlas);
#if defined(DEBUG) || defined(LIBFT_ENABLE_ANALYTICS)
	if (!startup_render_reported)
		std::fprintf(stderr, "[Startup] GPU world: after water flush\n");
	startup_render_reported = true;
#endif
	analytics_error = RuntimeAnalytics::end_scope();
	if (analytics_error != FT_ERR_SUCCESS)
		std::fprintf(stderr, "Analytics: GPU water scope end failed (%d)\n",
			analytics_error);
	draw_crosshair();
}

size_t GpuWorldRenderer::gpu_bytes() const
{
	size_t b = _batch.gpu_bytes();
	b += static_cast<size_t>(_atlas.is_loaded() ? 384 * 384 * 4 : 0);
	return (b);
}
