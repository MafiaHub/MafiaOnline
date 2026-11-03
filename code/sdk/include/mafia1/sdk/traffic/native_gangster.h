#pragma once

#include <cstdint>

namespace Mafia1Online::SDK::Traffic {
    // A gangster walker (type 4) that is hit becomes an armed C_entity:
    // C_traffic_element::Hit (0x458914) hands it to CreateMafianos instead
    // of taking the damage itself. See docs/npc_multiplayer_audit.md.
    //
    // void __thiscall CreateMafianos(E_hit_type, const S_vector &, unsigned
    // bodyType, C_actor *attacker, float damage), ret 14h. It duplicates the
    // walker model into a new C_entity (health 200, Thompson with 50 + 300
    // rounds), adds it as a temporary actor, deactivates the walker with
    // generation disabled, appends the entity to the police manager's
    // retirement list (AddDoRiciActor at 0x459d9d) and finally hits it as
    // the attacker did, which turns it on that attacker.
    inline constexpr uintptr_t kCreateMafianos = 0x459B90;
    inline constexpr uint8_t kGangsterWeapon   = 10;
} // namespace Mafia1Online::SDK::Traffic
