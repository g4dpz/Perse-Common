#include "SinglePwmLED.h"
#include <algorithm>

SinglePwmLED::SinglePwmLED(uint8_t pin, ledc_channel_t channel, uint8_t limit, bool invert) : SingleLED(
		std::min(limit, (uint8_t) 100)), pwm(pin, channel, invert){
	pwm.setDuty(0);
}

SinglePwmLED::~SinglePwmLED(){
	pwm.setDuty(0);
}

void SinglePwmLED::write(uint8_t val){
	pwm.setDuty(val);
}
