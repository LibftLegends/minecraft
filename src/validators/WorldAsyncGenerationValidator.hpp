#ifndef WORLD_ASYNC_GENERATION_VALIDATOR_HPP
#define WORLD_ASYNC_GENERATION_VALIDATOR_HPP

class WorldAsyncGenerationValidator
{
  public:
    WorldAsyncGenerationValidator();
    WorldAsyncGenerationValidator(const WorldAsyncGenerationValidator &other);
    ~WorldAsyncGenerationValidator();
    WorldAsyncGenerationValidator &operator=(
        const WorldAsyncGenerationValidator &other);

    int validate() const;
};

#endif
