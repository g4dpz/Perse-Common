#include "LEDBlinkFunction.h"
#include <esp_timer.h>

static uint64_t millis(){
	return esp_timer_get_time() / 1000;
}

LEDBlinkFunction::LEDBlinkFunction(SingleLED& led, uint32_t count, uint32_t period) : LEDFunction(led), count(count),
																					  period(period), startTime(millis()){
	led.setValue(0);
}

LEDBlinkFunction::~LEDBlinkFunction(){
	led.setValue(0);
}

void LEDBlinkFunction::loop(){
	if(count != 0 && elapsedCount >= count){
		if(led.getValue() != 0){
			led.setValue(0);
		}

		return;
	}

	const uint64_t elapsedTime = millis() - startTime;

	bool ledState = false;
	if(elapsedTime % period <= period / 2){
		ledState = true;
	}

	if((led.getValue() > 0 && ledState) || (led.getValue() == 0 && !ledState)){
		return;
	}

	if(ledState){
		led.setValue(0xFF);
	}else{
		++elapsedCount;
		led.setValue(0);
	}
}
