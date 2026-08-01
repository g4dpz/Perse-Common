#include "Threaded.h"
#include <esp_log.h>

Threaded::Threaded(const char* name, size_t stackSize, uint8_t priority, int8_t core)
	: name(name), stackSize(stackSize), priority(priority), core(core) {
	stopSem = xSemaphoreCreateBinary();
	stopMut = xSemaphoreCreateMutex();
}

Threaded::~Threaded() {
	if(state.load(std::memory_order_acquire) != Stopped) {
		ESP_LOGE("Threaded", "Threaded %s destructing while still running", name);
		abort();
	}

	vSemaphoreDelete(stopSem);
	vSemaphoreDelete(stopMut);
}

void Threaded::start() {
	if(state.load(std::memory_order_acquire) != Stopped) return;

	if(!onStart()) return;

	state.store(Running, std::memory_order_release);

	if(core == -1) {
		xTaskCreate(Threaded::threadFunc, name, stackSize, this, priority, &task);
	} else {
		xTaskCreatePinnedToCore(Threaded::threadFunc, name, stackSize, this, priority, &task, core);
	}
}

void Threaded::stop(TickType_t wait) {
	if(xSemaphoreTake(stopMut, wait) == pdFALSE) return;

	if(state.load(std::memory_order_acquire) != Running) {
		xSemaphoreGive(stopMut);
		return;
	}

	beforeStop();
	state.store(Stopping, std::memory_order_release);
	afterStopSignal();

	xSemaphoreTake(stopSem, wait);

	xSemaphoreGive(stopMut);
}

void Threaded::threadFunc(void* arg) {
	auto thr = static_cast<Threaded*>(arg);

	while(thr->state.load(std::memory_order_acquire) == Running) {
		thr->loop();
	}

	thr->onStop();

	thr->state.store(Stopped, std::memory_order_release);
	xSemaphoreGive(thr->stopSem);

	vTaskDelete(nullptr);
}

bool Threaded::onStart() {
	return true;
}

void Threaded::onStop() { }

void Threaded::beforeStop() { }

void Threaded::afterStopSignal() { }

bool Threaded::running() {
	auto s = state.load(std::memory_order_acquire);
	return s == Running || s == Stopping;
}

void Threaded::setPriority(uint8_t newPriority) {
	vTaskPrioritySet(task, newPriority);
}

// --- ThreadedClosure ---

ThreadedClosure::ThreadedClosure(Lambda loopFn, const char* name, size_t stackSize, uint8_t priority, int8_t core)
	: Threaded(name, stackSize, priority, core), fn(std::move(loopFn)) { }

void ThreadedClosure::loop() {
	fn();
}

// --- SleepyThreaded ---

SleepyThreaded::SleepyThreaded(TickType_t loopInterval, const char* name, size_t stackSize, uint8_t priority, int8_t core)
	: Threaded(name, stackSize, priority, core), sleepTime(loopInterval) {
	pauseSem = xSemaphoreCreateBinary();
}

SleepyThreaded::~SleepyThreaded() {
	vSemaphoreDelete(pauseSem);
}

bool SleepyThreaded::onStart() {
	lastWakeTime = xTaskGetTickCount();
	paused.store(false, std::memory_order_release);
	return true;
}

void SleepyThreaded::pause() {
	if(paused.load(std::memory_order_acquire)) return;
	xSemaphoreGive(pauseSem);
	while(!paused.load(std::memory_order_acquire)) {
		vTaskDelay(1);
	}
}

void SleepyThreaded::resume() {
	paused.store(false, std::memory_order_release);
	start();
}

void SleepyThreaded::loop() {
	if(xSemaphoreTake(pauseSem, 0) == pdTRUE) {
		stop(0);
		paused.store(true, std::memory_order_release);
		return;
	}

	sleepyLoop();
	vTaskDelayUntil(&lastWakeTime, sleepTime);
}

// --- SleepyThreadedClosure ---

SleepyThreadedClosure::SleepyThreadedClosure(TickType_t loopInterval, Lambda loopFn, const char* name, size_t stackSize, uint8_t priority, int8_t core)
	: SleepyThreaded(loopInterval, name, stackSize, priority, core), fn(std::move(loopFn)) { }

void SleepyThreadedClosure::sleepyLoop() {
	fn();
}
