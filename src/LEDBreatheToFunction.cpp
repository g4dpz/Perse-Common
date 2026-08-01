#include "LEDBreatheToFunction.h"
#include <esp_timer.h>

static uint64_t millis(){
	return esp_timer_get_time() / 1000;
}

LEDBreatheToFunction::LEDBreatheToFunction(SingleLED& led, float targetPercent, uint32_t duration) : LEDFunction(led), duration(duration), startTime(millis()), targetValue(targetPercent * 0xFF / 100.0f){
	startValue = led.getValue();
}

void LEDBreatheToFunction::loop(){
	const uint64_t elapsedTime = millis() - startTime;

	if(elapsedTime > duration){
		return;
	}

	const float percent = 1.0f * elapsedTime / duration;

	if(startValue > targetValue){
		led.setValue(startValue - percent * (startValue - targetValue));
	}else{
		led.setValue(startValue + percent * (targetValue - startValue));
	}
}
