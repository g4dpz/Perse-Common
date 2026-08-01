#ifndef PERSE_COMMON_EVENTS_H
#define PERSE_COMMON_EVENTS_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include "EventPool.h"
#include "Facility.h" // Project provides this on its include path

struct Event {
	Facility facility;
	void* data;
	size_t dataSize;
};

class EventQueue;

class Events {
public:
	static void init(size_t poolBlockSize, size_t poolBlockCount);

	static void listen(Facility facility, EventQueue* queue);
	static void unlisten(EventQueue* queue);

	static bool post(Facility facility, const void* data, size_t size);

	template<typename T>
	static bool post(Facility facility, const T& data){
		return post(facility, &data, sizeof(T));
	}

	/// Free event data back to the pool. Callers MUST call this after processing.
	static void free(Event& event);

private:
	static std::unordered_map<Facility, std::unordered_set<EventQueue*>> queues;
	static std::mutex mut;
	static EventPool* pool;

	friend class EventQueue;
};

class EventQueue {
public:
	explicit EventQueue(size_t count);
	virtual ~EventQueue();

	bool get(Event& item, TickType_t timeout);
	void reset();
	void unblock();

private:
	QueueHandle_t queue;

	struct InternalEvent {
		Event evt;
		bool killPill;
	};

	bool post(Facility facility, void* data, size_t dataSize);
	friend class Events;
};

#endif //PERSE_COMMON_EVENTS_H
