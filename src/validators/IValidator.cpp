#include "../../src/validators/IValidator.hpp"

IValidator::IValidator()
{
}

IValidator::IValidator(const IValidator &other)
{
	(void)other;
}

IValidator::~IValidator()
{
}

IValidator &IValidator::operator=(const IValidator &other)
{
	(void)other;
	return (*this);
}
