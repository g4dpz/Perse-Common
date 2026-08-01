# CMakeLists for ESP-IDF

set(COMPONENT_SRCS
    src/CommData.cpp
    src/DriveInfo.cpp
    src/RingBuffer.cpp
    src/RoverStateUtil.cpp
    src/Threaded.cpp
    src/Hysteresis.cpp
)

set(COMPONENT_ADD_INCLUDEDIRS src)

set(COMPONENT_REQUIRES lwip freertos)

register_component()
