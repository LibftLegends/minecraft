#ifndef WORLD_ITEM_STORE_VALIDATOR_HPP
# define WORLD_ITEM_STORE_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"

class WorldItemStoreValidator : public IValidator
{
  public:
	WorldItemStoreValidator();
	WorldItemStoreValidator(const WorldItemStoreValidator &other);
	~WorldItemStoreValidator();
	WorldItemStoreValidator &operator=(const WorldItemStoreValidator &other);

	int validate() const override;
};

#endif
