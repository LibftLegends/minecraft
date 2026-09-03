#ifndef WORLD_CHUNK_ASYNC_SUBMITTER_HPP
# define WORLD_CHUNK_ASYNC_SUBMITTER_HPP

# include "../../src/world/WorldChunkCandidateScanner.hpp"

class WorldChunkAsyncSubmitter
{
  public:
	WorldChunkAsyncSubmitter();
	WorldChunkAsyncSubmitter(const WorldChunkAsyncSubmitter &other);
	~WorldChunkAsyncSubmitter();
	WorldChunkAsyncSubmitter &operator=(const WorldChunkAsyncSubmitter &other);

	static int32_t stream_chunks_async(WorldChunkStreamer &streamer,
		int32_t stream_radius, int32_t budget, int32_t *generated) noexcept;

  private:
	static bool submit_async_candidate(WorldChunkStreamer &streamer,
		WorldChunkStreamer::StreamCandidate &candidate, int32_t chunk_x,
		int32_t chunk_z, int32_t *submitted, int32_t budget) noexcept;
	static bool process_async_candidate(WorldChunkStreamer &streamer,
		WorldChunkStreamer::StreamCandidate &candidate, int32_t *submitted,
		int32_t budget) noexcept;
	static int32_t submit_dirty_remeshes(WorldChunkStreamer &streamer) noexcept;
};

#endif
