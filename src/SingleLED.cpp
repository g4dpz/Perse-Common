#include <algorithm>
#include <esp_timer.h>
#include "SingleLED.h"

template<typename T> constexpr
T mapValue(T val, T fromLow, T fromHigh, T toLow, T toHigh){
	return (val - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}

SingleLED::SingleLED(uint8_t limit) : limit(limit), value(0){}

void SingleLED::setValue(uint8_t val){
	if(val == value){
		return;
	}

	value = val;

	const float percent = std::clamp(1.0f * mapValue((int)value, 0, 0xFF, 0, (int)limit) / limit, 0.0f, 1.0f);

	write((uint8_t) (percent * percent * limit));
}

uint8_t SingleLED::getValue() const{
	return value;
}
