#ifndef PERSE_COMMON_SINGLELED_H
#define PERSE_COMMON_SINGLELED_H

#include <cstdint>

class SingleLED {
public:
	explicit SingleLED(uint8_t limit);

	virtual ~SingleLED() = default;

	void setValue(uint8_t val);

	uint8_t getValue() const;

protected:
	virtual void write(uint8_t val) = 0;

private:
	uint8_t limit;
	uint8_t value;
};

#endif //PERSE_COMMON_SINGLELED_H
