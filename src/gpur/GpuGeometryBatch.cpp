#include "../../src/gpur/GpuGeometryBatch.hpp"

GpuGeometryBatch::GpuGeometryBatch() : _mega_vao(0), _mega_vbo(0), _mega_ebo(0),
	_mega_vbo_cap(0), _mega_ebo_cap(0), _water(), _owns_vbo(true),
	_geometry_signature(0U), _cache_valid(false), _buffers_dirty(false)
{
}

GpuGeometryBatch::GpuGeometryBatch(const GpuGeometryBatch &other) : _mega_vao(0),
	_mega_vbo(0), _mega_ebo(0), _mega_vbo_cap(0), _mega_ebo_cap(0), _water(),
	_owns_vbo(true), _geometry_signature(0U), _cache_valid(false),
	_buffers_dirty(false)
{
	(void)other;
}

GpuGeometryBatch::~GpuGeometryBatch()
{
	destroy();
}

GpuGeometryBatch &GpuGeometryBatch::operator=(const GpuGeometryBatch &other)
{
	(void)other;
	return (*this);
}

void GpuGeometryBatch::bind_vertex_attribs()
{
	constexpr GLsizei	S = static_cast<GLsizei>(sizeof(Vertex));

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, S, nullptr);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(1, 2, GL_UNSIGNED_SHORT, S,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(12)));
	glEnableVertexAttribArray(1);
	glVertexAttribIPointer(2, 1, GL_UNSIGNED_INT, S,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(16)));
	glEnableVertexAttribArray(2);
	glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, S,
		reinterpret_cast<const void *>(static_cast<uintptr_t>(20)));
	glEnableVertexAttribArray(3);
}

void GpuGeometryBatch::setup_world_vao()
{
	glGenVertexArrays(1, &_mega_vao);
	glGenBuffers(1, &_mega_vbo);
	glGenBuffers(1, &_mega_ebo);
	glBindVertexArray(_mega_vao);
	glBindBuffer(GL_ARRAY_BUFFER, _mega_vbo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mega_ebo);
	bind_vertex_attribs();
	glBindVertexArray(0);
}

bool GpuGeometryBatch::initialize(GLuint shared_vbo)
{
	destroy();
	(void)shared_vbo;
	return (true);
}

void GpuGeometryBatch::destroy_mega_buffers()
{
	if (_mega_vao != 0)
	{
		glDeleteVertexArrays(1, &_mega_vao);
		_mega_vao = 0;
	}
	if (_owns_vbo && _mega_vbo != 0)
	{
		glDeleteBuffers(1, &_mega_vbo);
		_mega_vbo = 0;
	}
	if (_mega_ebo != 0)
	{
		glDeleteBuffers(1, &_mega_ebo);
		_mega_ebo = 0;
	}
	_mega_vbo_cap = 0;
	_mega_ebo_cap = 0;
	_mega_verts.clear();
	_mega_idxs.clear();
}

void GpuGeometryBatch::destroy()
{
	destroy_mega_buffers();
	_water.destroy();
	_geometry_signature = 0U;
	_cache_valid = false;
	_buffers_dirty = false;
	_visible_chunk_slots.clear();
	for (int32_t index = 0; index < WorldCoordinates::CHUNK_COUNT; ++index)
		_chunk_meshes[index].destroy();
}

void GpuGeometryBatch::add_chunk(const WorldChunk &wc, uint32_t base)
{
	const float	ox = static_cast<float>(wc.world_x);
	const float	oz = static_cast<float>(wc.world_z);
	Vertex		mv;
	uint32_t	raw;

	for (const chunk_mesh_vertex &v : wc.mesh.vertices)
	{
		mv.x = static_cast<float>(v.coordinate_x) + ox;
		mv.y = static_cast<float>(v.coordinate_y);
		mv.z = static_cast<float>(v.coordinate_z) + oz;
		mv.tu = v.texture_u;
		mv.tv = v.texture_v;
		mv.block_id = v.block_id;
		mv.face = static_cast<uint32_t>(v.face);
		_mega_verts.push_back(mv);
	}
	for (size_t j = 0; j < wc.mesh.indices.size(); ++j)
	{
		raw = wc.mesh.indices[j];
		if (wc.mesh.vertices[raw].block_id == TERRAIN_GENERATOR_WATER_BLOCK)
			_water_idxs.push_back(base + raw);
		else
			_mega_idxs.push_back(base + raw);
	}
}

void GpuGeometryBatch::collect(const Camera &camera, const World &world,
	int width, int height)
{
	RenderCache	cache;

	cache.configure(camera, width, height, world.active_render_distance);
	_visible_chunk_slots.clear();
	for (int i = 0; i < world.chunk_count; ++i)
	{
		const WorldChunk &wc = world.chunks[i];
		if (!wc.initialized || wc.mesh.vertices.empty() == FT_TRUE)
			continue ;
		if (!MeshCuller::chunk_is_visible(camera, wc, cache))
			continue ;
		_chunk_meshes[i].sync(wc.mesh, wc.mesh_revision);
		_chunk_world_x[i] = wc.world_x;
		_chunk_world_z[i] = wc.world_z;
		_visible_chunk_slots.push_back(i);
	}
}

uint64_t GpuGeometryBatch::geometry_signature(const World &world)
{
	uint64_t		hash;
	const int32_t	radius = WorldCoordinates::render_distance_to_chunk_radius(
			world.active_render_distance);
	const int32_t	radius_sq = radius * radius;
	const auto		mix = [&hash](uint64_t value)
	{
		hash ^= value;
		hash *= 1099511628211ULL;
	};

	hash = 1469598103934665603ULL;
	mix(static_cast<uint32_t>(world.center_chunk_x));
	mix(static_cast<uint32_t>(world.center_chunk_z));
	mix(static_cast<uint32_t>(radius));
	for (int32_t i = 0; i < world.chunk_count; ++i)
	{
		const WorldChunk &wc = world.chunks[i];
		if (!wc.initialized)
			continue ;
		if (WorldCoordinates::chunk_distance_squared(wc.chunk_x, wc.chunk_z,
				world.center_chunk_x, world.center_chunk_z) > radius_sq)
			continue ;
		mix(static_cast<uint32_t>(wc.chunk_x));
		mix(static_cast<uint32_t>(wc.chunk_z));
		mix(wc.mesh_revision);
	}
	return (hash);
}

void GpuGeometryBatch::upload_solid_buffers()
{
	const GLsizeiptr	vb = static_cast<GLsizeiptr>(_mega_verts.size()
				* sizeof(Vertex));
	const GLsizeiptr	ib = static_cast<GLsizeiptr>(_mega_idxs.size()
				* sizeof(uint32_t));

	glBindBuffer(GL_ARRAY_BUFFER, _mega_vbo);
	if (static_cast<size_t>(vb) > _mega_vbo_cap)
	{
		glBufferData(GL_ARRAY_BUFFER, vb, nullptr, GL_DYNAMIC_DRAW);
		_mega_vbo_cap = static_cast<size_t>(vb);
	}
	if (vb > 0)
		glBufferSubData(GL_ARRAY_BUFFER, 0, vb, _mega_verts.data());
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mega_ebo);
	if (static_cast<size_t>(ib) > _mega_ebo_cap)
	{
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, nullptr, GL_DYNAMIC_DRAW);
		_mega_ebo_cap = static_cast<size_t>(ib);
	}
	if (ib > 0)
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ib, _mega_idxs.data());
}

void GpuGeometryBatch::upload_buffers_if_dirty()
{
	if (!_buffers_dirty)
		return ;
	upload_solid_buffers();
	_water.upload(_water_idxs);
	_buffers_dirty = false;
}

void GpuGeometryBatch::flush_solid(GLuint, GLint u_mvp, GLint u_chunk_offset,
	const float mvp[16], GpuTextureAtlas &atlas)
{
	float	chunk_offset[3];

	if (_visible_chunk_slots.empty())
		return ;
	glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
	atlas.bind(0);
	for (int32_t slot : _visible_chunk_slots)
	{
		chunk_offset[0] = static_cast<float>(_chunk_world_x[slot]);
		chunk_offset[1] = 0.0f;
		chunk_offset[2] = static_cast<float>(_chunk_world_z[slot]);
		glUniform3fv(u_chunk_offset, 1, chunk_offset);
		_chunk_meshes[slot].draw_solid();
	}
	glBindVertexArray(0);
}

void GpuGeometryBatch::flush_water(GLuint, GLint u_mvp, GLint u_chunk_offset,
	const float mvp[16], GpuTextureAtlas &atlas)
{
	_water.flush(u_mvp, u_chunk_offset, mvp, atlas, _visible_chunk_slots,
		_chunk_meshes, _chunk_world_x, _chunk_world_z);
}

size_t GpuGeometryBatch::gpu_bytes() const
{
	size_t bytes = 0U;
	for (int32_t index = 0; index < WorldCoordinates::CHUNK_COUNT; ++index)
		bytes += _chunk_meshes[index].gpu_bytes();
	return (bytes);
}
