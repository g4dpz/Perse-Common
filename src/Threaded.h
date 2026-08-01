#ifndef PERSE_COMMON_THREADED_H
#define PERSE_COMMON_THREADED_H

#include <cstddef>
#include <atomic>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

class Threaded {
public:
	virtual ~Threaded();

	void start();
	void stop(TickType_t wait = portMAX_DELAY);
	bool running();
	void setPriority(uint8_t newPriority);

protected:
	Threaded(const char* name, size_t stackSize = 12000, uint8_t priority = 5, int8_t core = -1);

	virtual bool onStart();
	virtual void onStop();
	virtual void beforeStop();
	virtual void afterStopSignal();
	virtual void loop() = 0;

	TaskHandle_t getTaskHandle() const { return task; }

private:
	const char* name;
	size_t stackSize;
	const uint8_t priority;
	const int8_t core;

	enum State : uint8_t { Stopped, Running, Stopping };
	std::atomic<State> state{Stopped};

	static void threadFunc(void* arg);
	TaskHandle_t task{nullptr};
	SemaphoreHandle_t stopSem;
	SemaphoreHandle_t stopMut;
};

class ThreadedClosure : public Threaded {
public:
	using Lambda = std::function<void()>;

	ThreadedClosure(Lambda loopFn, const char* name, size_t stackSize = 12000, uint8_t priority = 5, int8_t core = -1);

protected:
	void loop() override;
	Lambda fn;
};

class SleepyThreaded : public Threaded {
public:
	virtual ~SleepyThreaded();

	void pause();
	void resume();

protected:
	SleepyThreaded(TickType_t loopInterval, const char* name, size_t stackSize = 12000, uint8_t priority = 5, int8_t core = -1);

	virtual void sleepyLoop() = 0;

	bool onStart() override;

private:
	const TickType_t sleepTime;
	TickType_t lastWakeTime{0};

	SemaphoreHandle_t pauseSem;
	std::atomic<bool> paused{false};

	void loop() final;
};

class SleepyThreadedClosure : public SleepyThreaded {
public:
	using Lambda = std::function<void()>;

	SleepyThreadedClosure(TickType_t loopInterval, Lambda loopFn, const char* name, size_t stackSize = 12000, uint8_t priority = 5, int8_t core = -1);

protected:
	void sleepyLoop() override final;

private:
	Lambda fn;
};

#endif //PERSE_COMMON_THREADED_H
