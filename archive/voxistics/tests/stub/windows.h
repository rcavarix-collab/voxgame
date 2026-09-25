// Native-test stand-in for <windows.h>: just what the pure modules touch.
#pragma once
#include <chrono>
#include <cstdint>
union LARGE_INTEGER { long long QuadPart; };
inline int QueryPerformanceCounter(LARGE_INTEGER* t) {
    t->QuadPart = (long long)std::chrono::steady_clock::now().time_since_epoch().count();
    return 1;
}
