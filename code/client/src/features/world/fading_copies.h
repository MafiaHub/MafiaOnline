#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Mafia1Online::Features::World {
    // A copy of a streamed body (an NPC's or a walker's replica) fading out
    // after its entity left this client's stream, whose entity streams back
    // before the fade ends: taken out of `fading` by the entity's network
    // id (`key`), so that copy goes on instead of a second one made beside
    // it. Nothing when no copy of that entity is fading.
    template <typename Copy>
    std::optional<Copy> TakeFading(std::vector<Copy> &fading, uint64_t Copy::*key, uint64_t id) {
        const auto found = std::find_if(fading.begin(), fading.end(), [key, id](const Copy &copy) { return copy.*key == id; });
        if (found == fading.end()) {
            return std::nullopt;
        }
        std::optional<Copy> copy {std::move(*found)};
        fading.erase(found);
        return copy;
    }
} // namespace Mafia1Online::Features::World
