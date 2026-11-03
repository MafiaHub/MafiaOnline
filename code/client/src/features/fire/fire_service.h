#pragma once

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::SDK::Sound {
    struct NativeSound;
}
namespace Mafia1Online::Shared::Entities {
    class FireEntity;
    class WaterJetEntity;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Fire {
    // Draws the server's fires and water jets near the camera with the
    // game's own effects (tables\effects.tbl): each fire is a set of retail
    // "Fire" emitters on an anchor frame we own, with the retail fire light
    // and loop sound; each jet is the retail hose effect "hadice" on a
    // nozzle frame turned and scaled so its drops fly the ballistic arc that
    // lands on the target, with the hydrant's loop. The server decides
    // everything; this only follows the replicated state.
    class FireService final {
      public:
        void Update(World::WorldService &world);
        // Every particle, frame and sound goes while the scene still exists.
        void OnMissionClosing();
        void Reset();

      private:
        struct Flame {
            glm::vec3 offset {0.0f};
            int32_t particle = -1;
        };
        struct FireVisual {
            SDK::Scene::NativeFrame *anchor = nullptr;
            SDK::Sound::NativeSound *loop = nullptr;
            std::vector<Flame> flames;
            int32_t smoke = -1;
            int32_t steam = -1;
            int light = -1;
            glm::vec3 lightPosition {0.0f};
            glm::vec3 position {0.0f};
            uint32_t seed = 0;
            float radius = 0.0f;
            float height = 0.0f;
            float volume = -1.0f;
            size_t burning = 0;
            std::chrono::steady_clock::time_point outSince {};
            std::chrono::steady_clock::time_point lightAt {};
        };
        struct JetVisual {
            SDK::Scene::NativeFrame *nozzle = nullptr;
            SDK::Sound::NativeSound *loop = nullptr;
            int32_t particle = -1;
        };

        bool Build(FireVisual &visual, const Shared::Entities::FireEntity &fire);
        void Layout(FireVisual &visual, const Shared::Entities::FireEntity &fire);
        void Apply(FireVisual &visual, const Shared::Entities::FireEntity &fire);
        void Release(FireVisual &visual);
        void Aim(World::WorldService &world, JetVisual &visual, const Shared::Entities::WaterJetEntity &jet, const glm::vec3 &camera);
        void Release(JetVisual &visual);

        std::unordered_map<uint64_t, FireVisual> _fires;
        std::unordered_map<uint64_t, JetVisual> _jets;
    };
} // namespace Mafia1Online::Features::Fire
