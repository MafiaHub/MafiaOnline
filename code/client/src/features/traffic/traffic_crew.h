#pragma once

#include "shared/features/car/car_entity.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::SDK::Car {
    struct NativeCar;
}
namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Traffic {
    // Players: the traffic driver and passenger of a network traffic car, as
    // the host's pool shows them. Like retail's, they are I3D models: the
    // driver linked to the car frame at the seat's in-point and posed on the
    // steering wheel by C_game::AddDriverMan, the passenger in the seated
    // pose, both in the in-car death pose once shot. A parking driver plays
    // the door animation out of its seat, a pedestrian taking a parked car
    // the one in from where retail starts it. A bullet that hits a crew
    // model is reported as a hit on the car; the host's re-ray finds its own.
    class TrafficCrewService final {
      public:
        using CrewState = Shared::Entities::CarEntity::CrewState;

        // After CarService::Update, which has already unbound removed cars.
        void Update(World::WorldService &world);
        // Mission closing or disconnect: before C_game and the cars go.
        void Reset();
        // A car being destroyed takes its frame, and so its crew, with it.
        void OnNativeCarDestroyed(void *car);
        [[nodiscard]] bool IsCrewFrame(const void *car, SDK::Scene::NativeFrame *frame) const;
        // The local player started a steal: the driver leaves at once here,
        // before the server's answer.
        void HideDriverForSteal(uint64_t carId);
        // The server refused that steal: the driver is back at once.
        void CancelStealHide(uint64_t carId);

      private:
        struct Member {
            SDK::Scene::NativeFrame *model = nullptr;
            std::string modelName;
            CrewState shown = CrewState::None;
            bool driverMan = false;
            // Made in the world at the driver door for the entry animation.
            bool atEntry = false;
            // A door animation is running; the model is ticked by hand, as
            // C_traffic_car::AI ticks its animation machine.
            bool animating = false;
            // A model that would not load is not retried for this name.
            std::string failedName;
        };
        struct Crew {
            SDK::Car::NativeCar *car = nullptr;
            Member members[2];
            std::chrono::steady_clock::time_point stealUntil;
        };

        void Apply(Crew &crew, Member &member, int seat, const std::string &model, CrewState state);
        // A model in the seat (in-point, car frame), or at the driver door's
        // entry placement in the world for the entry animation.
        bool Create(Crew &crew, Member &member, int seat, const std::string &model, bool atEntry);
        void Release(Crew &crew);
        void Release(Member &member);

        std::unordered_map<uint64_t, Crew> _crews;
        std::chrono::steady_clock::time_point _lastUpdate;
    };
} // namespace Mafia1Online::Features::Traffic
