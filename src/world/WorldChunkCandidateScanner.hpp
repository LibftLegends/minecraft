#ifndef WORLD_CHUNK_CANDIDATE_SCANNER_HPP
# define WORLD_CHUNK_CANDIDATE_SCANNER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"

class WorldChunkCandidateScanner
{
  public:
	WorldChunkCandidateScanner();
	WorldChunkCandidateScanner(const WorldChunkCandidateScanner &other);
	~WorldChunkCandidateScanner();
	WorldChunkCandidateScanner &operator=(const WorldChunkCandidateScanner &other);

	static void prepare_stream_candidates(WorldChunkStreamer &streamer,
		int32_t stream_radius) noexcept;
	static WorldChunkStreamer::StreamCandidate *find_stream_candidate(WorldChunkStreamer &streamer,
		int32_t chunk_x, int32_t chunk_z) noexcept;
	static int32_t try_load_chunk_at(WorldChunkStreamer &streamer,
		int32_t chunk_x, int32_t chunk_z) noexcept;
	static void refresh_stale_candidate(WorldChunkStreamer &streamer,
		WorldChunkStreamer::StreamCandidate &candidate) noexcept;
	static int32_t stream_chunks_sync(WorldChunkStreamer &streamer,
		int32_t stream_radius, int32_t budget, int32_t *generated) noexcept;

  private:
	static bool candidate_less(const WorldChunkStreamer::StreamCandidate &a,
		const WorldChunkStreamer::StreamCandidate &b) noexcept;
	static void remesh_loaded_neighbor(WorldChunkStreamer &streamer,
		int32_t chunk_x, int32_t chunk_z) noexcept;
	static bool process_sync_candidate(WorldChunkStreamer &streamer,
		WorldChunkStreamer::StreamCandidate &candidate, int32_t budget,
		int32_t *generated) noexcept;
	static bool handle_sync_success(WorldChunkStreamer &streamer,
		WorldChunkStreamer::StreamCandidate &candidate, int32_t budget,
		int32_t *generated) noexcept;
};

# include "../../src/world/WorldChunkAsyncSubmitter.hpp"

#endif
