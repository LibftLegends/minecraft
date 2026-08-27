#include "../../src/meshes/MeshClipper.hpp"

MeshClipper::MeshClipper()
{
}

MeshClipper::MeshClipper(const MeshClipper &other)
{
	*this = other;
}

MeshClipper::~MeshClipper()
{
}

MeshClipper &MeshClipper::operator=(const MeshClipper &other)
{
	(void)other;
	return (*this);
}

ClipVertex MeshClipper::lerp(const ClipVertex &from, const ClipVertex &to,
	double amount)
{
	ClipVertex	result;

	result.view_x = from.view_x + ((to.view_x - from.view_x) * amount);
	result.view_y = from.view_y + ((to.view_y - from.view_y) * amount);
	result.view_z = from.view_z + ((to.view_z - from.view_z) * amount);
	result.texture_u = from.texture_u + ((to.texture_u - from.texture_u)
			* amount);
	result.texture_v = from.texture_v + ((to.texture_v - from.texture_v)
			* amount);
	return (result);
}

size_t MeshClipper::clip_near(const ClipVertex input[3], ClipVertex output[4])
{
	ClipVertex	previous;
	ClipVertex	current;
	bool		previous_inside;
	bool		current_inside;
	double		amount;
	size_t		input_index;
	size_t		output_count;

	previous = input[2];
	previous_inside = previous.view_z >= NEAR_PLANE;
	output_count = 0U;
	input_index = 0U;
	while (input_index < 3U)
	{
		current = input[input_index];
		current_inside = current.view_z >= NEAR_PLANE;
		if (current_inside != previous_inside)
		{
			amount = (NEAR_PLANE - previous.view_z) / (current.view_z
					- previous.view_z);
			if (output_count < 4U)
				output[output_count++] = lerp(previous, current, amount);
		}
		if (current_inside == true && output_count < 4U)
			output[output_count++] = current;
		previous = current;
		previous_inside = current_inside;
		input_index = input_index + 1U;
	}
	return (output_count);
}
