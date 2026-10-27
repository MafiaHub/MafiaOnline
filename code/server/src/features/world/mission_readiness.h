#pragma once

#include <mafianet/types.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Framework::Networking {
    class NetworkPeer;
}

namespace Mafia1Online::Features::World {
    class MissionReadiness final {
      public:
        void Register(Framework::Networking::NetworkPeer &network);
        void SetGeneration(uint64_t generation);
        // A peer that does not count toward AllReady still reports its own
        // readiness (the simulation host).
        void OnConnect(MafiaNet::PeerGuid guid, bool countsTowardAll = true);
        void OnDisconnect(MafiaNet::PeerGuid guid);
        bool IsReady(MafiaNet::PeerGuid guid) const;
        bool AllReady() const;
        void SetResultCallback(std::function<void(MafiaNet::PeerGuid, uint64_t, uint8_t)> callback) {
            _resultCallback = std::move(callback);
        }

        uint64_t Generation() const {
            return _generation;
        }

      private:
        void OnResult(MafiaNet::PeerGuid guid, uint64_t generation, uint8_t state);

        uint64_t _generation = 0;
        std::unordered_map<uint64_t, uint64_t> _loadedByPeer;
        std::unordered_set<uint64_t> _uncounted;
        std::function<void(MafiaNet::PeerGuid, uint64_t, uint8_t)> _resultCallback;
    };
} // namespace Mafia1Online::Features::World
