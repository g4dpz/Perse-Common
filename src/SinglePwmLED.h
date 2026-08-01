#ifndef PERSE_COMMON_SINGLEPWMLED_H
#define PERSE_COMMON_SINGLEPWMLED_H

#include "SingleLED.h"
#include "PWM.h"

class SinglePwmLED : public SingleLED {
public:
	SinglePwmLED(uint8_t pin, ledc_channel_t channel, uint8_t limit = 100, bool invert = false);
	virtual ~SinglePwmLED() override;

protected:
	virtual void write(uint8_t val) override;

private:
	PWM pwm;
};

#endif //PERSE_COMMON_SINGLEPWMLED_H
