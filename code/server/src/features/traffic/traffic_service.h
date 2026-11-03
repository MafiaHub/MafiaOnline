#pragma once

#include "shared/features/car/car_entity.h"
#include "shared/features/traffic/traffic_jam.h"
#include "shared/features/traffic/traffic_report.h"

#include <mafianet/types.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::Shared::Car {
    struct SeatIntent;
    struct SeatEvent;
}
namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
}
namespace Mafia1Online::Shared::Npc {
    struct Seat;
}
namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}

namespace Mafia1Online::Features::Traffic {
    // Cars the simulation host drives: its retail traffic and the cars NPC
    // crews take. Each is an ordinary CarEntity with the host as simulation
    // controller and hostDriven set, so damage, deformation, explosion and
    // terminal outcomes use the car sync unchanged. The host alone seats its
    // NPCs in them; a player gets one only through a takeover (steal of a
    // traffic car with its driver, or entering one nobody sits in), after
    // which it is an ordinary player car.
    class TrafficService final {
      public:
        static constexpr size_t kMaxCars = Shared::Traffic::kMaxHostCars;
        // C_shvestky::SetActive fills an 8-barrier pool.
        static constexpr size_t kMaxBarriers = 32;
        // Players see traffic this far; the host spawns it out of sight
        // (150-170) and despawns it past 180 from every player.
        static constexpr float kStreamRange = 260.0f;
        // A player starts a steal from beside the driver's door.
        static constexpr float kStealReach = Shared::Traffic::kStealReach;
        static constexpr float kStealMaxSpeed = Shared::Traffic::kStealMaxSpeed;
        // A player's steal intents closer together than this are refused
        // unexamined (and raise no script event).
        static constexpr std::chrono::milliseconds kStealIntentInterval {500};
        // A car that exploded stays while its explosion replicates.
        static constexpr std::chrono::seconds kWreckLifetime {20};
        // A former traffic car nobody sits in and no player is near goes.
        static constexpr std::chrono::seconds kAbandonedAfter {120};
        static constexpr float kAbandonedDistance = 400.0f;

        // The jam watch looks at the host's cars this often.
        static constexpr std::chrono::seconds kJamCheckInterval {1};
        // The host retires a pool car it was asked to reset within a frame or
        // two; a police or mafia car goes with the next police retirement
        // pass, once no case runs through it. One still here after this was
        // kept, and the jam watch leaves it alone.
        static constexpr std::chrono::seconds kResetWait {10};

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars);
        // A jam the host was asked to clear: where, how many cars and how
        // long the most recently stopped of them had stood.
        struct JamReset {
            glm::vec3 center {0.0f};
            size_t cars = 0;
            double stuckSeconds = 0.0;
        };
        // A player took a car the host drives (stolen: its driver was thrown
        // out), or the server refused a steal; carId, playerId. A jam reset.
        struct Events {
            std::function<void(uint64_t, uint64_t, bool)> taken;
            std::function<void(uint64_t, uint64_t)> stealRefused;
            std::function<void(const JamReset &)> jamReset;
            // Whether an NPC (network id) is a scripted one (NpcService).
            std::function<bool(uint64_t)> scriptedNpc;
        };
        void SetEvents(Events events) {
            _events = std::move(events);
        }
        void SetMissionGeneration(uint64_t generation);
        void Update();
        // Host gone or mission changed: every car the host drives goes.
        void Clear();

        // False for settings outside Shared::Traffic::ValidJamSettings.
        bool SetJamSettings(const Shared::Traffic::JamSettings &settings);
        [[nodiscard]] const Shared::Traffic::JamSettings &GetJamSettings() const {
            return _jam;
        }
        // Asks the host to take the traffic cars within radius of center off
        // the road (JamScope::Manual: also parked ones); a car somebody sits
        // in or stands at stays. Returns how many cars it asked for.
        size_t ResetArea(const glm::vec3 &center, float radius);
        // Asks the host to take every car of its traffic pool off the road
        // and refill it. False without a host.
        bool ResetAll();

        // Server seat rules for a car the host drives. An entry into a seat
        // nobody holds hands the car over once the seat intent is accepted.
        enum class SeatGate : uint8_t {
            Allow,
            Reject,
            Takeover,
            // A player pulls the host's NPC out of its seat (StealBegin);
            // the host replays the throw natively on its own NPC.
            NpcThrow,
        };
        SeatGate CheckSeatIntent(const Shared::Car::SeatIntent &intent, Shared::Entities::PlayerEntity &player);
        void CompleteTakeover(uint64_t carId, Shared::Entities::PlayerEntity &player);
        // From NpcService: the host seated or unseated one of its NPCs.
        // Returns the event players replay, if the change is accepted.
        bool ApplyNpcSeat(uint64_t npcId, const Shared::Npc::Seat &seat, Shared::Car::SeatEvent &event);
        // The NPC is gone; any seat it holds is freed.
        void OnNpcGone(uint64_t npcId);
        // A player's accepted StealBegin threw the NPC in this seat out.
        void OnNpcThrown(uint64_t carId, uint8_t seat);

      private:
        // A car the current host drives, by the host's key.
        struct Record {
            uint32_t key = 0;
            uint64_t networkId = 0;
            // Despawn at this time (exploded wreck); zero for none.
            std::chrono::steady_clock::time_point despawnAt;
            // Where and since when it stands, for the jam watch.
            Shared::Traffic::StuckTrack stuck;
            // When the host was asked to take it off the road; zero for never.
            std::chrono::steady_clock::time_point resetAskedAt;
            // Still here kResetWait after that: the host kept it. Only the
            // jam watch passes it over; a script's reset or a restart asks
            // again.
            bool resetKept = false;
        };

        bool FromHost(MafiaNet::PeerGuid sender, uint64_t missionGeneration) const;
        // The attached host is the one the records belong to.
        bool HostCurrent() const;
        Shared::Entities::CarEntity *HostCar(uint32_t key) const;
        // NPC network ids seated in this car, if any.
        bool HasNpcOccupant(Shared::Entities::CarEntity &car) const;
        bool NpcInSeat(uint64_t carId, uint8_t seat) const;
        // Cars the host still drives: a wreck the host already retired
        // (despawnAt set) no longer counts against kMaxCars.
        size_t LiveRecords() const;
        void Takeover(Shared::Entities::CarEntity &car, Shared::Entities::PlayerEntity &player, bool steal);
        void Despawn(uint32_t key);
        void DespawnReleased(uint64_t networkId);
        // The jam watch: stuck tracks, then Shared::Traffic::FindJams.
        void WatchJams(std::chrono::steady_clock::time_point now);
        Shared::Traffic::JamCar ViewOf(const Record &record, const Shared::Entities::CarEntity &car, std::chrono::steady_clock::time_point now,
                                       const std::vector<glm::vec3> &onFoot) const;
        // Where the spawned players on foot stand.
        std::vector<glm::vec3> PlayersOnFoot() const;
        void SendReset(std::vector<uint32_t> keys, bool all);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        Events _events;
        uint64_t _missionGeneration = 0;
        // The host the records belong to: its keys and its cars.
        std::optional<MafiaNet::PeerGuid> _host;
        std::unordered_map<uint32_t, Record> _records;
        std::unordered_map<uint64_t, uint32_t> _keyByCar;
        // Former traffic cars players took, by network id, and since when
        // nobody sat in or came near one. They no longer have a host key, so
        // a later host's keys cannot meet them.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _released;
        // Road block barriers by the host's key, to their network ids.
        std::unordered_map<uint32_t, uint64_t> _barriers;
        // Where each NPC sits: car network id and seat.
        struct NpcSeat {
            uint64_t carId = 0;
            uint8_t seat = 0;
        };
        std::unordered_map<uint64_t, NpcSeat> _npcSeats;
        // When each player (network id) last sent a steal intent.
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> _lastStealIntent;
        Shared::Traffic::JamSettings _jam;
        std::chrono::steady_clock::time_point _lastJamCheck;
        // Recent jam resets: where, and until when no other one goes there.
        struct RecentJam {
            glm::vec3 center {0.0f};
            std::chrono::steady_clock::time_point until;
        };
        std::vector<RecentJam> _recentJams;
    };
} // namespace Mafia1Online::Features::Traffic
