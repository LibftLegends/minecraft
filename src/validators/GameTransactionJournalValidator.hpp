#ifndef GAME_TRANSACTION_JOURNAL_VALIDATOR_HPP
# define GAME_TRANSACTION_JOURNAL_VALIDATOR_HPP

# include "../../src/validators/IValidator.hpp"

class GameTransactionJournalValidator : public IValidator
{
  public:
	GameTransactionJournalValidator();
	GameTransactionJournalValidator(const GameTransactionJournalValidator &other);
	~GameTransactionJournalValidator();
	GameTransactionJournalValidator &operator=(
		const GameTransactionJournalValidator &other);
	int validate() const override;
};

#endif
