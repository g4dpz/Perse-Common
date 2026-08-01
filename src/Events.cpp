#include "Events.h"
#include <cstring>
#include <esp_log.h>

static const char* TAG = "Events";

std::unordered_map<Facility, std::unordered_set<EventQueue*>> Events::queues;
std::mutex Events::mut;
EventPool* Events::pool = nullptr;

void Events::init(size_t poolBlockSize, size_t poolBlockCount){
	pool = new EventPool(poolBlockSize, poolBlockCount);
}

void Events::listen(Facility facility, EventQueue* queue){
	std::lock_guard lock(mut);
	queues[facility].insert(queue);
}

void Events::unlisten(EventQueue* queue){
	std::lock_guard lock(mut);

	for(auto& pair : queues){
		pair.second.erase(queue);
	}
}

bool Events::post(Facility facility, const void* data, size_t size){
	// Copy subscriber set under lock, then release before delivering
	std::unique_lock lock(mut);

	auto it = queues.find(facility);
	if(it == queues.end()) return true;

	const std::unordered_set<EventQueue*> subs = it->second;
	lock.unlock();

	if(subs.empty()) return true;

	bool allOk = true;

	for(auto queue : subs){
		void* block = nullptr;

		if(size != 0){
			block = pool->allocate();
			if(block == nullptr){
				ESP_LOGW(TAG, "Pool exhausted, dropping event for facility %d", static_cast<int>(facility));
				allOk = false;
				continue;
			}
			memcpy(block, data, size);
		}

		if(!queue->post(facility, block, size)){
			allOk = false;
		}
	}

	return allOk;
}

void Events::free(Event& event){
	if(event.data != nullptr){
		pool->deallocate(event.data);
		event.data = nullptr;
	}
}

// --- EventQueue implementation ---

EventQueue::EventQueue(size_t count){
	queue = xQueueCreate(count, sizeof(InternalEvent));
}

EventQueue::~EventQueue(){
	reset();
	vQueueDelete(queue);
}

bool EventQueue::get(Event& event, TickType_t timeout){
	InternalEvent internal{};

	if(!xQueueReceive(queue, &internal, timeout)) return false;

	if(internal.killPill) return false;

	event = internal.evt;
	return true;
}

bool EventQueue::post(Facility facility, void* data, size_t dataSize){
	InternalEvent event = {
		.evt = {
			.facility = facility,
			.data = data,
			.dataSize = dataSize
		},
		.killPill = false
	};

	if(xQueueSend(queue, &event, 0) != pdTRUE){
		// Queue full: return block to pool
		if(data != nullptr){
			Events::pool->deallocate(data);
		}
		return false;
	}
	return true;
}

void EventQueue::reset(){
	while(uxQueueMessagesWaiting(queue) > 0){
		Event evt = {};
		get(evt, 0);
		Events::free(evt);
	}
}

void EventQueue::unblock(){
	InternalEvent event = {
		.evt = {
			.facility = {},
			.data = nullptr,
			.dataSize = 0
		},
		.killPill = true
	};

	xQueueSend(queue, &event, 0);
}
