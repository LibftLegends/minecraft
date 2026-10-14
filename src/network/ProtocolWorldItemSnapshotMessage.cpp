#include "../../src/network/ProtocolWorldItemSnapshotMessage.hpp"
#include <cmath>
#include <cstring>

ProtocolWorldItemSnapshotMessage::ProtocolWorldItemSnapshotMessage()
	: session_id(0U), world_id(0U), chunk_x(0), chunk_z(0), items()
{
}

int32_t ProtocolWorldItemSnapshotMessage::serialize(
	ft_byte_buffer &buffer) const noexcept
{
	uint64_t bits;
	int32_t error;

	if (this->session_id == 0U || this->world_id == 0U
		|| this->items.size() > 256U)
		return (FT_ERR_INVALID_ARGUMENT);
	error = buffer.append_u64_le(this->session_id);
	if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(this->world_id);
	if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(
		static_cast<uint32_t>(this->chunk_x));
	if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(
		static_cast<uint32_t>(this->chunk_z));
	if (error == FT_ERR_SUCCESS) error = buffer.append_u16_le(
		static_cast<uint16_t>(this->items.size()));
	for (std::size_t i = 0U; error == FT_ERR_SUCCESS && i < this->items.size(); ++i)
	{
		const WorldItemSnapshot &item = this->items[i];
		if (item.entity_id == 0U || item.entity_revision == 0U
			|| item.item_id <= 0 || item.quantity <= 0
			|| item.max_stack < item.quantity
			|| (item.item_instance_id != 0U && item.quantity != 1)
			|| !std::isfinite(item.x) || !std::isfinite(item.y)
			|| !std::isfinite(item.z))
			return (FT_ERR_INVALID_ARGUMENT);
		error = buffer.append_u64_le(item.entity_id);
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(item.entity_revision);
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(item.spawned_tick);
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.item_id));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.quantity));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.max_stack));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(item.item_instance_id);
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.rarity));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.width));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.height));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier1_id));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier1_value));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier2_id));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier2_value));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier3_id));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier3_value));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier4_id));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u32_le(static_cast<uint32_t>(item.modifier4_value));
		std::memcpy(&bits, &item.x, sizeof(bits));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(bits);
		std::memcpy(&bits, &item.y, sizeof(bits));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(bits);
		std::memcpy(&bits, &item.z, sizeof(bits));
		if (error == FT_ERR_SUCCESS) error = buffer.append_u64_le(bits);
	}
	return (error);
}

int32_t ProtocolWorldItemSnapshotMessage::deserialize(ft_byte_buffer &buffer) noexcept
{
	ProtocolWorldItemSnapshotMessage parsed;
	uint32_t cx, cz, raw;
	uint16_t count;
	int32_t error;

	error = buffer.read_u64_le(&parsed.session_id);
	if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&parsed.world_id);
	if (error == FT_ERR_SUCCESS) error = buffer.read_u32_le(&cx);
	if (error == FT_ERR_SUCCESS) error = buffer.read_u32_le(&cz);
	if (error == FT_ERR_SUCCESS) error = buffer.read_u16_le(&count);
	if (error != FT_ERR_SUCCESS) return (error);
	if (parsed.session_id == 0U || parsed.world_id == 0U || count > 256U)
		return (FT_ERR_INVALID_ARGUMENT);
	parsed.chunk_x = static_cast<int32_t>(cx);
	parsed.chunk_z = static_cast<int32_t>(cz);
	try { parsed.items.reserve(count); }
	catch (...) { return (FT_ERR_NO_MEMORY); }
	for (uint16_t i = 0U; i < count; ++i)
	{
		WorldItemSnapshot v;
		uint64_t bits;
		raw = 0U;
		error = buffer.read_u64_le(&v.entity_id);
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&v.entity_revision);
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&v.spawned_tick);
		v.owner_id = 0U;
		v.owner_lock_until_tick = 0U;
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.item_id = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.quantity = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.max_stack = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&v.item_instance_id);
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.rarity = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.width = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.height = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier1_id = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier1_value = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier2_id = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier2_value = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier3_id = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier3_value = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier4_id = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) { error = buffer.read_u32_le(&raw); if (error == FT_ERR_SUCCESS) v.modifier4_value = static_cast<int32_t>(raw); }
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&bits);
		if (error == FT_ERR_SUCCESS) std::memcpy(&v.x, &bits, sizeof(bits));
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&bits);
		if (error == FT_ERR_SUCCESS) std::memcpy(&v.y, &bits, sizeof(bits));
		if (error == FT_ERR_SUCCESS) error = buffer.read_u64_le(&bits);
		if (error == FT_ERR_SUCCESS) std::memcpy(&v.z, &bits, sizeof(bits));
		if (error != FT_ERR_SUCCESS) return (error);
		if (v.entity_id == 0U || v.entity_revision == 0U || v.item_id <= 0
			|| v.quantity <= 0 || v.max_stack < v.quantity
			|| (v.item_instance_id != 0U && v.quantity != 1)
			|| !std::isfinite(v.x) || !std::isfinite(v.y)
			|| !std::isfinite(v.z))
			return (FT_ERR_INVALID_ARGUMENT);
		v.chunk_x = parsed.chunk_x; v.chunk_z = parsed.chunk_z;
		parsed.items.push_back(v);
	}
	*this = parsed;
	return (FT_ERR_SUCCESS);
}
