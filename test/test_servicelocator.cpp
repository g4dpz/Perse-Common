// Feature: cross-project-optimisations, Property 6: Service Locator Get Identity
// **Validates: Requirements 19.1**
//
// Register random pointers under random service keys, retrieve them, assert equality
// 100+ iterations

#include "freertos_mock.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <array>
#include <vector>

// --- Inline ServiceEnum.h for test ---
enum class Service : uint8_t {
    Svc0 = 0,
    Svc1,
    Svc2,
    Svc3,
    Svc4,
    Svc5,
    Svc6,
    Svc7,
    COUNT
};

// --- Inline ServiceLocator (matching Perse-Common/src/ServiceLocator.h) ---
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

    void clear() {
        services.fill(nullptr);
    }

private:
    std::array<void*, static_cast<size_t>(Service::COUNT)> services{};
};

// --- Test types ---
struct ServiceA { int x; };
struct ServiceB { float y; };
struct ServiceC { char z[16]; };

// --- Property test ---

static unsigned int seed;

static size_t randRange(size_t min, size_t max) {
    return min + (rand_r(&seed) % (max - min + 1));
}

void test_get_identity() {
    const int iterations = 200;
    const size_t serviceCount = static_cast<size_t>(Service::COUNT);

    for (int iter = 0; iter < iterations; iter++) {
        ServiceLocator locator;

        // Generate random pointers for random service keys
        std::vector<void*> registeredPtrs(serviceCount, nullptr);
        size_t numToRegister = randRange(1, serviceCount);

        // Create some real objects to register
        std::vector<ServiceA> aObjs(serviceCount);
        std::vector<ServiceB> bObjs(serviceCount);
        std::vector<ServiceC> cObjs(serviceCount);

        for (size_t i = 0; i < numToRegister; i++) {
            size_t svcIdx = randRange(0, serviceCount - 1);
            auto svc = static_cast<Service>(svcIdx);

            // Choose a random type to register
            int typeChoice = rand_r(&seed) % 3;
            void* ptr = nullptr;
            switch (typeChoice) {
                case 0: ptr = &aObjs[svcIdx]; locator.set(svc, &aObjs[svcIdx]); break;
                case 1: ptr = &bObjs[svcIdx]; locator.set(svc, &bObjs[svcIdx]); break;
                case 2: ptr = &cObjs[svcIdx]; locator.set(svc, &cObjs[svcIdx]); break;
            }
            registeredPtrs[svcIdx] = ptr;
        }

        // Verify all registered pointers can be retrieved
        for (size_t i = 0; i < serviceCount; i++) {
            auto svc = static_cast<Service>(i);
            void* retrieved = locator.get<void>(svc);

            if (registeredPtrs[i] != nullptr) {
                ASSERT(retrieved == registeredPtrs[i]);
            } else {
                ASSERT(retrieved == nullptr);
            }
        }
    }
}

void test_overwrite_returns_latest() {
    const int iterations = 150;

    for (int iter = 0; iter < iterations; iter++) {
        ServiceLocator locator;

        ServiceA a1{1};
        ServiceA a2{2};
        ServiceA a3{3};

        size_t svcIdx = randRange(0, static_cast<size_t>(Service::COUNT) - 1);
        auto svc = static_cast<Service>(svcIdx);

        // Register multiple times, last one wins
        locator.set(svc, &a1);
        ASSERT(locator.get<ServiceA>(svc) == &a1);

        locator.set(svc, &a2);
        ASSERT(locator.get<ServiceA>(svc) == &a2);

        locator.set(svc, &a3);
        ASSERT(locator.get<ServiceA>(svc) == &a3);
    }
}

void test_unregistered_returns_nullptr() {
    const int iterations = 100;

    for (int iter = 0; iter < iterations; iter++) {
        ServiceLocator locator;

        size_t svcIdx = randRange(0, static_cast<size_t>(Service::COUNT) - 1);
        auto svc = static_cast<Service>(svcIdx);

        // Unregistered service should return nullptr
        void* ptr = locator.get<void>(svc);
        ASSERT(ptr == nullptr);
    }
}

int main() {
    seed = static_cast<unsigned int>(time(nullptr));
    printf("Property 6: Service Locator Get Identity (seed=%u)\n", seed);

    RUN_TEST(test_get_identity);
    RUN_TEST(test_overwrite_returns_latest);
    RUN_TEST(test_unregistered_returns_nullptr);

    TEST_SUMMARY();
}
