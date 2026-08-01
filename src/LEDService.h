#ifndef PERSE_COMMON_LEDSERVICE_H
#define PERSE_COMMON_LEDSERVICE_H

#include <cstdint>
#include <map>
#include <memory>
#include "Threaded.h"
#include "Queue.h"
#include "LEDConfig.h"

class SingleLED;
class LEDFunction;

class LEDService : private Threaded {
public:
	LEDService();
	virtual ~LEDService();

	void begin();

	void on(LED led);
	void off(LED led);
	void blink(LED led, uint32_t count = 1, uint32_t period = 1000);
	void breathe(LED led, uint32_t period = 1000);
	void set(LED led, float percent);
	void breatheTo(LED led, float targetPercent, uint32_t duration = 250);

	void registerLED(LED led, SingleLED* device);

protected:
	virtual void loop() override;

private:
	enum LEDInstruction {
		On,
		Off,
		Blink,
		Breathe,
		Set,
		BreatheTo
	};

	struct LEDInstructionInfo {
		LED led;
		LEDInstruction instruction;
		uint32_t count;
		uint32_t period;
		float targetPercent;
	};

	std::map<LED, SingleLED*> ledDevices;
	std::map<LED, std::unique_ptr<LEDFunction>> ledFunctions;
	Queue<LEDInstructionInfo> instructionQueue;

private:
	void onInternal(LED led);
	void offInternal(LED led);
	void blinkInternal(LED led, uint32_t count, uint32_t period);
	void breatheInternal(LED led, uint32_t period);
	void setInternal(LED led, float percent);
	void breatheToInternal(LED led, float targetPercent, uint32_t duration);
};

#endif //PERSE_COMMON_LEDSERVICE_H
