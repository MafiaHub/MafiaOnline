#include "game/entities/native_object_registry.h"

#include <cstdio>

using Mafia1Online::Game::Entities::NativeObjectRegistry;

int main() {
    int first = 0;
    int second = 0;
    NativeObjectRegistry registry;

    const auto original = registry.Bind(&first, 10);
    if (registry.Resolve(original) != &first) {
        return 1;
    }

    registry.InvalidateNative(&first);
    if (registry.Resolve(original)) {
        return 2;
    }

    const auto reusedAddress = registry.Bind(&first, 20);
    if (reusedAddress.generation == original.generation || registry.Resolve(original) || registry.Resolve(reusedAddress) != &first) {
        return 3;
    }

    const auto movedReplica = registry.Bind(&second, 20);
    if (registry.Resolve(reusedAddress) || registry.Resolve(movedReplica) != &second) {
        return 4;
    }

    registry.InvalidateNetwork(20);
    if (registry.Resolve(movedReplica)) {
        return 5;
    }

    const auto beforeMissionClose = registry.Bind(&first, 30);
    registry.Clear();
    if (registry.Resolve(beforeMissionClose)) {
        return 6;
    }
    const auto afterMissionClose = registry.Bind(&first, 30);
    if (registry.Resolve(beforeMissionClose) || registry.Resolve(afterMissionClose) != &first) {
        return 7;
    }

    std::puts("Native object generations survive address reuse and mission reset");
    return 0;
}
