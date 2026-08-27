#ifndef I_VALIDATOR_HPP
# define I_VALIDATOR_HPP

class IValidator
{
  public:
	IValidator();
	IValidator(const IValidator &other);
	virtual ~IValidator();
	IValidator &operator=(const IValidator &other);

	virtual int validate() const = 0;
};

#endif
