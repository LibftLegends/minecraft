#ifndef REGENERATION_CARD_VALIDATOR_HPP
# define REGENERATION_CARD_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"
# include <cstdint>

class RegenerationCardValidator : public IValidator
{
  private:
	static int validate_opening_hand(uint32_t opening_hand_size,
		uint64_t seed) noexcept;
	static int validate_sequenced_choice_expiry() noexcept;
	static int32_t validate_stabilizer_roll(uint32_t risk_basis_points,
		uint32_t expected_result, uint64_t seed) noexcept;

  public:
	RegenerationCardValidator();
	RegenerationCardValidator(const RegenerationCardValidator &other);
	~RegenerationCardValidator();
	RegenerationCardValidator &operator=(
		const RegenerationCardValidator &other);

	int validate() const override;
};

#endif
