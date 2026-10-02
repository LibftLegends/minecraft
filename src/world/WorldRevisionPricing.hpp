#ifndef WORLD_REVISION_PRICING_HPP
# define WORLD_REVISION_PRICING_HPP

# include <cstdint>

struct WorldRevisionCostPolicy
{
	uint32_t maximum_selected_chunks;
	uint32_t currency_item_id;
	uint64_t currency_units_per_chunk;
	uint64_t four_card_opening_cost;
	uint64_t maximum_total_currency_cost;
};

struct WorldRevisionCostQuote
{
	uint32_t revision_identifier;
	uint64_t selection_revision;
	uint32_t selected_paid_chunks;
	uint32_t opening_card_count;
	uint32_t currency_item_id;
	uint64_t chunk_currency_cost;
	uint64_t opening_card_currency_cost;
	uint64_t total_currency_cost;
};

#endif
