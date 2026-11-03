#pragma once

#include "shared/features/pumper/pumper_report.h"

#include <mafianet/types.h>

#include <array>
#include <chrono>
#include <cstdint>

namespace Mafia1Online::Features::Simulation {
    class SimulationService;
}
namespace Mafia1Online::Features::Player {
    class PlayerService;
}
namespace Mafia1Online::Features::Car {
    class CarService;
}

namespace Mafia1Online::Features::Pumper {
    // The gas station attendants. The simulation host runs them (the retail
    // C_pumper AI) and reports them ten times a second and on every change;
    // every spawned player gets every report (three attendants). The server
    // owns the services: a player's retail "use the pump" comes here, is
    // checked against the car the player drives, the attendant, the policy
    // and the price, and is then handed to the host (Serve) and the player
    // (Accepted). The host's fuel event fills the network car's tank and
    // charges the player; the attendant's return releases the car.
    class PumperService final {
      public:
        // A service ends unfinished after this long (retail takes about 40 s).
        static constexpr auto kServiceTimeout = std::chrono::seconds(180);
        // The attendant's reported position may be this far from the car: he
        // stands on his route's start, a few metres from the pump.
        static constexpr float kServiceReach = 30.0f;
        // A tank fuller than this many litres short is refused as full.
        static constexpr float kMinimumLitres = 0.5f;
        static constexpr double kMaxPricePerLitre = 10000.0;

        void RegisterRPC(const Simulation::SimulationService &simulation, Player::PlayerService &players, Car::CarService &cars);
        void SetMissionGeneration(uint64_t generation);
        void Update();

        void SetEnabled(bool enabled) {
            _enabled = enabled;
        }
        [[nodiscard]] bool IsEnabled() const {
            return _enabled;
        }
        void SetPricePerLitre(double price) {
            _pricePerLitre = price;
        }
        [[nodiscard]] double PricePerLitre() const {
            return _pricePerLitre;
        }

        // The host's newest report of an attendant, by load index; null
        // before the first report of this mission.
        [[nodiscard]] const Shared::Pumper::State *Find(uint32_t index) const {
            return index < Shared::Pumper::kMaxPumpers && _known[index] ? &_latest[index] : nullptr;
        }
        // The car the server has the attendant serve, 0 for none.
        [[nodiscard]] uint64_t ServiceCar(uint32_t index) const {
            return index < Shared::Pumper::kMaxPumpers && _services[index].active ? _services[index].carId : 0;
        }
        template <typename F>
        void ForEach(F &&visit) const {
            for (uint32_t index = 0; index < Shared::Pumper::kMaxPumpers; ++index) {
                if (_known[index]) {
                    visit(_latest[index]);
                }
            }
        }

      private:
        struct Service {
            bool active = false;
            uint64_t carId = 0;
            uint64_t playerId = 0;
            uint64_t playerGeneration = 0;
            std::chrono::steady_clock::time_point startedAt {};
            bool fuelled = false;
            // The simulation host told to serve.
            MafiaNet::PeerGuid host {};
        };

        void OnSnapshot(const Shared::Pumper::Snapshot &received, MafiaNet::PeerGuid sender);
        void OnRequest(const Shared::Pumper::Request &request, MafiaNet::PeerGuid sender);
        void OnProgress(const Shared::Pumper::Progress &progress, MafiaNet::PeerGuid sender);
        void Refuse(uint64_t playerId, MafiaNet::PeerGuid guid, const Shared::Pumper::Request &request, const char *reason, uint32_t cost);
        void Fill(uint8_t index);
        // Releases the customer's car; tells the host to send the attendant
        // back unless it reported the end itself.
        void End(uint8_t index, bool abortHost);
        void Send(Shared::Pumper::Command::Kind kind, uint8_t index, uint64_t carId, MafiaNet::PeerGuid guid);

        const Simulation::SimulationService *_simulation = nullptr;
        Player::PlayerService *_players = nullptr;
        Car::CarService *_cars = nullptr;
        uint64_t _missionGeneration = 0;
        std::array<Shared::Pumper::State, Shared::Pumper::kMaxPumpers> _latest {};
        std::array<bool, Shared::Pumper::kMaxPumpers> _known {};
        std::array<Service, Shared::Pumper::kMaxPumpers> _services {};
        bool _hasReport = false;
        uint32_t _hostSession = 0;
        uint32_t _lastHostTimeMs = 0;
        bool _enabled = true;
        double _pricePerLitre = 0.0;
    };
} // namespace Mafia1Online::Features::Pumper
