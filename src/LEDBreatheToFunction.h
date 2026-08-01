#ifndef PERSE_COMMON_LEDBREATHETOFUNCTION_H
#define PERSE_COMMON_LEDBREATHETOFUNCTION_H

#include "LEDFunction.h"

class LEDBreatheToFunction : public LEDFunction {
public:
	LEDBreatheToFunction(SingleLED& led, float targetPercent, uint32_t duration);

protected:
	virtual void loop() override;

private:
	uint32_t duration;
	uint64_t startTime;
	uint8_t targetValue;
	uint8_t startValue;
};

#endif //PERSE_COMMON_LEDBREATHETOFUNCTION_H
