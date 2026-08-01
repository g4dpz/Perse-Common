#include <algorithm>
#include <esp_timer.h>
#include "LEDBreatheFunction.h"

static uint64_t millis(){
	return esp_timer_get_time() / 1000;
}

LEDBreatheFunction::LEDBreatheFunction(SingleLED& led, uint32_t period) : LEDFunction(led), period(period){
	startTime = millis();
}

LEDBreatheFunction::~LEDBreatheFunction(){
	led.setValue(0);
}

void LEDBreatheFunction::loop(){
	const uint64_t elapsedTime = millis() - startTime;

	const uint64_t elapsedInPeriod = elapsedTime % period;
	const float periodPercent = 1.0f * elapsedInPeriod / period;

	const float ledPercent = std::clamp(2.0f * (periodPercent <= 0.5 ? periodPercent : (1.0f - periodPercent)), 0.0f,
										1.0f);

	led.setValue((uint8_t) (ledPercent * 0xFF));
}
