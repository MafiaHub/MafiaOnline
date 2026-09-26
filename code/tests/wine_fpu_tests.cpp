#include "features/compat/x87_nearbyint.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <xmmintrin.h>

namespace {
    uint64_t Bits(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    void SetEnvironment(uint16_t control, uint16_t flags) {
        // Seed sticky flags without relying on the CRT routines being repaired.
        uint32_t environment[7] {};
        __asm {
            fninit
            fnstenv environment
        }
        environment[0] = (environment[0] & 0xffff0000u) | control;
        environment[1] = (environment[1] & 0xffff0000u) | flags;
        __asm fldenv environment
    }

    struct Case {
        double input;
        double expected[4]; // nearest/even, downward, upward, toward zero
        uint16_t newFlags = 0;
    };
}

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--reproduce-wine") == 0) {
        const auto crt = GetModuleHandleW(L"ucrtbase.dll");
        const auto original = reinterpret_cast<double(__cdecl *)(double)>(GetProcAddress(crt, "nearbyint"));
        if (!original) return 2;
        std::puts("Calling Wine nearbyint(1.25) with invalid set and inexact clear");
        std::fflush(stdout);
        SetEnvironment(0x037f, 0x01);
        volatile double result = original(1.25);
        return result == 1.0 ? 0 : 3;
    }

    const double infinity = std::numeric_limits<double>::infinity();
    const double maximum = (std::numeric_limits<double>::max)();
    const double tiny = std::numeric_limits<double>::denorm_min();
    const Case cases[] {
        {0.0, {0.0, 0.0, 0.0, 0.0}},
        {-0.0, {-0.0, -0.0, -0.0, -0.0}},
        {0.25, {0.0, 0.0, 1.0, 0.0}},
        {-0.25, {-0.0, -1.0, -0.0, -0.0}},
        {0.5, {0.0, 0.0, 1.0, 0.0}},
        {-0.5, {-0.0, -1.0, -0.0, -0.0}},
        {1.25, {1.0, 1.0, 2.0, 1.0}},
        {-1.25, {-1.0, -2.0, -1.0, -1.0}},
        {1.5, {2.0, 1.0, 2.0, 1.0}},
        {-1.5, {-2.0, -2.0, -1.0, -1.0}},
        {2.5, {2.0, 2.0, 3.0, 2.0}},
        {-2.5, {-2.0, -3.0, -2.0, -2.0}},
        {4503599627370495.5, {4503599627370496.0, 4503599627370495.0, 4503599627370496.0, 4503599627370495.0}},
        {maximum, {maximum, maximum, maximum, maximum}},
        {-maximum, {-maximum, -maximum, -maximum, -maximum}},
        {infinity, {infinity, infinity, infinity, infinity}},
        {-infinity, {-infinity, -infinity, -infinity, -infinity}},
        {tiny, {0.0, 0.0, 1.0, 0.0}, 0x02},
        {-tiny, {-0.0, -1.0, -0.0, -0.0}, 0x02},
    };
    const unsigned originalMxcsr = _mm_getcsr();
    unsigned checks = 0;
    for (uint16_t precision : {uint16_t(0), uint16_t(0x200), uint16_t(0x300)}) {
        for (uint16_t rounding = 0; rounding < 4; ++rounding) {
            for (uint16_t flags = 0; flags < 64; ++flags) {
                for (unsigned precisionTrap = 0; precisionTrap < 2; ++precisionTrap) {
                    if (precisionTrap && (flags & 0x20)) continue;
                    const uint16_t control = 0x007f | precision | (rounding << 10);
                    const uint16_t expectedControl = precisionTrap ? control & ~0x20 : control;
                    for (const auto &test : cases) {
                        SetEnvironment(expectedControl, flags);
                        // Deliberately use a different SSE rounding mode.
                        const unsigned mxcsr = 0x1f95 | (((rounding + 1) % 4) << 13);
                        _mm_setcsr(mxcsr);
                        const double result = Mafia1Online::Features::Compat::X87NearbyInt(test.input);
                        uint16_t statusAfter, controlAfter;
                        __asm {
                            fnstsw statusAfter
                            fnstcw controlAfter
                        }
                        const unsigned mxcsrAfter = _mm_getcsr();
                        __asm fninit
                        _mm_setcsr(originalMxcsr);
                        if (Bits(result) != Bits(test.expected[rounding]) || controlAfter != expectedControl
                            || (statusAfter & 0x3f) != (flags | test.newFlags) || (statusAfter & 0x8080)
                            || mxcsrAfter != mxcsr) {
                            std::printf("FAIL input=%a mode=%u flags=%x control=%x: result=%a status=%x control=%x\n",
                                test.input, rounding, flags, expectedControl, result, statusAfter, controlAfter);
                            return 1;
                        }
                        ++checks;
                    }
                }
            }
        }
    }
    // Pass the raw NaN bits on the stack so the test caller cannot quiet a
    // signaling NaN before it reaches the function under test.
    auto round = &Mafia1Online::Features::Compat::X87NearbyInt;
    for (uint64_t inputBits : {uint64_t(0x7ff8000000000123), uint64_t(0xfff8000000000456), uint64_t(0x7ff0000000000001)}) {
        SetEnvironment(0x037f, 0x04);
        const double sentinel = 9.125;
        double result = 0, sentinelAfter = 0;
        uint16_t statusAfter, controlAfter;
        __asm {
            fld sentinel
            push dword ptr [inputBits + 4]
            push dword ptr [inputBits]
            call round
            add esp, 8
            fstp result
            fstp sentinelAfter
            fnstsw statusAfter
            fnstcw controlAfter
            fninit
        }
        const bool signaling = (inputBits & 0x0008000000000000) == 0;
        const uint16_t expectedFlags = 0x04 | (signaling ? 0x01 : 0);
        if (Bits(result) != (inputBits | 0x0008000000000000) || Bits(sentinelAfter) != Bits(sentinel)
            || (statusAfter & 0x3f) != expectedFlags || (statusAfter & 0x8080) || controlAfter != 0x037f) {
            std::puts("FAIL NaN handling or x87 stack preservation");
            return 1;
        }
        ++checks;
    }
    std::printf("Passed %u nearbyint cases: rounding modes, precision, sticky flags, signed zero, subnormals, infinity, NaNs, x87 stack, SSE state\n", checks);
    return 0;
}
