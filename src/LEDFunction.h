#ifndef PERSE_COMMON_LEDFUNCTION_H
#define PERSE_COMMON_LEDFUNCTION_H

#include "SingleLED.h"

class LEDFunction {
public:
	explicit LEDFunction(SingleLED& led);

	virtual ~LEDFunction() = default;

	virtual void loop() = 0;

protected:
	SingleLED& led;
};

#endif //PERSE_COMMON_LEDFUNCTION_H
