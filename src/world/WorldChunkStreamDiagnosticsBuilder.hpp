#ifndef WORLD_CHUNK_STREAM_DIAGNOSTICS_BUILDER_HPP
# define WORLD_CHUNK_STREAM_DIAGNOSTICS_BUILDER_HPP

# include "../../src/world/WorldChunkStreamer.hpp"

class WorldChunkStreamDiagnosticsBuilder
{
  public:
	WorldChunkStreamDiagnosticsBuilder();
	WorldChunkStreamDiagnosticsBuilder(const WorldChunkStreamDiagnosticsBuilder &other);
	~WorldChunkStreamDiagnosticsBuilder();
	WorldChunkStreamDiagnosticsBuilder &operator=(const WorldChunkStreamDiagnosticsBuilder &other);

	static WorldChunkStreamer::Diagnostics build(const WorldChunkStreamer &streamer) noexcept;

  private:
	static void accumulate_candidate(const WorldChunkStreamer::StreamCandidate &candidate,
		uint64_t current_frame,
		WorldChunkStreamer::Diagnostics &diagnostics) noexcept;
};

#endif
