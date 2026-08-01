#include "LEDService.h"
#include <algorithm>
#include <esp_log.h>
#include "SingleLED.h"
#include "LEDBlinkFunction.h"
#include "LEDBreatheFunction.h"
#include "LEDBreatheToFunction.h"

static const char* TAG = "LEDService";

LEDService::LEDService() : Threaded("LEDService"), instructionQueue(25){
}

LEDService::~LEDService(){
	ledFunctions.clear();

	for(auto& [led, device] : ledDevices){
		delete device;
	}
}

void LEDService::begin(){
	start();
}

void LEDService::registerLED(LED led, SingleLED* device){
	ledDevices[led] = device;
}

void LEDService::on(LED led){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = On
	};

	instructionQueue.post(instruction);
}

void LEDService::off(LED led){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = Off
	};

	instructionQueue.post(instruction);
}

void LEDService::blink(LED led, uint32_t count /*= 1*/, uint32_t period /*= 1000*/){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = Blink,
			.count = count,
			.period = period
	};

	instructionQueue.post(instruction);
}

void LEDService::breathe(LED led, uint32_t period /*= 1000*/){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = Breathe,
			.count = 0,
			.period = period
	};

	instructionQueue.post(instruction);
}

void LEDService::set(LED led, float percent){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = Set,
			.targetPercent = std::clamp(percent, 0.0f, 100.0f)
	};

	instructionQueue.post(instruction);
}

void LEDService::breatheTo(LED led, float targetPercent, uint32_t duration){
	LEDInstructionInfo instruction{
			.led = led,
			.instruction = BreatheTo,
			.period = duration,
			.targetPercent = std::clamp(targetPercent, 0.0f, 100.0f)
	};

	instructionQueue.post(instruction);
}

void LEDService::loop(){
	TickType_t timeout = ledFunctions.empty() ? portMAX_DELAY : pdMS_TO_TICKS(10);
	LEDInstructionInfo info;
	if(instructionQueue.get(info, timeout)){
		if(info.instruction == On){
			onInternal(info.led);
		}else if(info.instruction == Off){
			offInternal(info.led);
		}else if(info.instruction == Blink){
			blinkInternal(info.led, info.count, info.period);
		}else if(info.instruction == Breathe){
			breatheInternal(info.led, info.period);
		}else if(info.instruction == Set){
			setInternal(info.led, info.targetPercent);
		}else if(info.instruction == BreatheTo){
			breatheToInternal(info.led, info.targetPercent, info.period);
		}
	}

	for(auto& [led, fn] : ledFunctions){
		fn->loop();
	}
}

void LEDService::onInternal(LED led){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		return;
	}

	if(ledDevices[led] == nullptr){
		return;
	}

	ledDevices[led]->setValue(0xFF);
}

void LEDService::offInternal(LED led){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		return;
	}

	if(ledDevices[led] == nullptr){
		return;
	}

	ledDevices[led]->setValue(0);
}

void LEDService::blinkInternal(LED led, uint32_t count, uint32_t period){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		ESP_LOGW(TAG, "LED %d is set to blink, but does not exist.", (uint8_t) led);
		return;
	}

	ledFunctions[led] = std::make_unique<LEDBlinkFunction>(*ledDevices[led], count, period);
}

void LEDService::breatheInternal(LED led, uint32_t period){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		ESP_LOGW(TAG, "LED %d is set to breathe, but does not exist.", (uint8_t) led);
		return;
	}

	ledFunctions[led] = std::make_unique<LEDBreatheFunction>(*ledDevices[led], period);
}

void LEDService::setInternal(LED led, float percent){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		return;
	}

	if(ledDevices[led] == nullptr){
		return;
	}

	ledDevices[led]->setValue((uint8_t)(0xFF * percent / 100.0f));
}

void LEDService::breatheToInternal(LED led, float targetPercent, uint32_t duration){
	if(ledFunctions.contains(led)){
		ledFunctions.erase(led);
	}

	if(!ledDevices.contains(led)){
		ESP_LOGW(TAG, "LED %d is set to breathe to value, but does not exist.", (uint8_t) led);
		return;
	}

	ledFunctions[led] = std::make_unique<LEDBreatheToFunction>(*ledDevices[led], targetPercent, duration);
}
