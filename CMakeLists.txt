# CMakeLists for ESP-IDF

set(COMPONENT_SRCS
    src/CommData.cpp
    src/DriveInfo.cpp
    src/RingBuffer.cpp
    src/RoverStateUtil.cpp
    src/Threaded.cpp
    src/Hysteresis.cpp
    src/EventPool.cpp
    src/Events.cpp
    src/ADCReader.cpp
    src/LEDService.cpp
    src/LEDFunction.cpp
    src/LEDBlinkFunction.cpp
    src/LEDBreatheFunction.cpp
    src/LEDBreatheToFunction.cpp
    src/SingleLED.cpp
    src/SingleExpanderLED.cpp
    src/SinglePwmLED.cpp
    src/ServiceLocator.cpp
)

set(COMPONENT_ADD_INCLUDEDIRS src)

set(COMPONENT_REQUIRES lwip freertos)

register_component()
