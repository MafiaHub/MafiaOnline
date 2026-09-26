#include "x87_nearbyint.h"

#include <cstdint>

namespace Mafia1Online::Features::Compat {
    namespace {
        struct X87Environment {
            uint16_t control, reserved1;
            uint16_t status, reserved2;
            uint16_t tag, reserved3;
            uint32_t instruction;
            uint16_t codeSegment, opcode;
            uint32_t operand;
            uint16_t dataSegment, reserved4;
        };
        static_assert(sizeof(X87Environment) == 28);
    }

    double __cdecl X87NearbyInt(double value) {
        uint16_t originalControl = 0;
        uint16_t originalStatus = 0;
        __asm {
            fnstcw originalControl
            fnstsw originalStatus
        }
        // FRNDINT respects the x87 rounding mode. Suppress its precision trap;
        // nearbyint must preserve the caller's inexact flag rather than set it.
        const uint16_t roundingControl = originalControl | 0x20;
        X87Environment environment {};
        double rounded = 0;
        __asm {
            fldcw roundingControl
            fld value
            frndint
            fstp rounded
            fnstenv environment
        }
        environment.control = originalControl;
        environment.status = (environment.status & ~0x80a0u) | (originalStatus & 0x20u);
        // Keep all other exception flags, including any invalid operation from
        // a signaling NaN. ES and B reflect exceptions left unmasked by CW.
        if (environment.status & ~originalControl & 0x3f) {
            environment.status |= 0x8080;
        }
        __asm fldenv environment
        return rounded;
    }
}
