#ifndef PERSE_COMMON_SERVICELOCATOR_H
#define PERSE_COMMON_SERVICELOCATOR_H

#include <cstdint>
#include <array>

// Project provides this header defining: enum class Service : uint8_t { ..., COUNT };
#include "ServiceEnum.h"

class ServiceLocator {
public:
	template<typename T>
	void set(Service service, T* ptr) {
		services[static_cast<uint8_t>(service)] = static_cast<void*>(ptr);
	}

	template<typename T>
	T* get(Service service) {
		return static_cast<T*>(services[static_cast<uint8_t>(service)]);
	}

private:
	std::array<void*, static_cast<size_t>(Service::COUNT)> services{};
};

extern ServiceLocator Services;

#endif //PERSE_COMMON_SERVICELOCATOR_H
