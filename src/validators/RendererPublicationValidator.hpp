#ifndef RENDERER_PUBLICATION_VALIDATOR_HPP
# define RENDERER_PUBLICATION_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"

class RendererPublicationValidator : public IValidator
{
  private:
	static int validate_software_lighting_contract() noexcept;

  public:
	RendererPublicationValidator();
	RendererPublicationValidator(const RendererPublicationValidator &other);
	~RendererPublicationValidator();
	RendererPublicationValidator &operator=(
		const RendererPublicationValidator &other);

	int validate() const override;
};

#endif
