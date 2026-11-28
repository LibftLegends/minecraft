#ifndef ITEM_CATALOG_VALIDATOR_HPP
# define ITEM_CATALOG_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"

class ItemCatalogValidator : public IValidator
{
  public:
	ItemCatalogValidator();
	ItemCatalogValidator(const ItemCatalogValidator &other);
	~ItemCatalogValidator();
	ItemCatalogValidator &operator=(const ItemCatalogValidator &other);

	int validate() const override;
};

#endif
