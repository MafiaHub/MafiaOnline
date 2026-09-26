#pragma once

#include <string>
#include <string_view>

#include <integrations/server/instance.h>

#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>

namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
}

namespace Mafia1Online::Shared::Player {
    struct Movement;
    struct NativePose;
    struct Climb;
    struct CameraView;
} // namespace Mafia1Online::Shared::Player

namespace Mafia1Online::Features::Player {
    class PlayerService final {
      public:
        void RegisterRPC();
        void OnConnect(const Framework::Integrations::Server::PlayerConnectionData &info);
        void OnDisconnect(MafiaNet::PeerGuid guid);
        bool Spawn(uint64_t networkId, const glm::vec3 &position, float yaw, uint64_t missionGeneration);
        // Applies the nickname policy; returns the name actually used, or an
        // empty string for an unknown player.
        std::string SetNickname(uint64_t networkId, std::string_view requested);
        bool SetModel(uint64_t networkId, std::string_view model);
        bool SetMoney(uint64_t networkId, uint32_t amount);
        bool GiveMoney(uint64_t networkId, int32_t amount);
        bool TrySpendMoney(uint64_t networkId, uint32_t cost);
        bool Respawn(uint64_t networkId, const glm::vec3 &position, float yaw, uint64_t missionGeneration);
        bool Despawn(uint64_t networkId);
        bool SetHealth(uint64_t networkId, float health);
        bool SetCameraTarget(uint64_t observerId, uint64_t targetId);
        void ResetForMission(uint64_t missionGeneration);
        Shared::Entities::PlayerEntity *FindByNetworkId(uint64_t networkId) const;
        Shared::Entities::PlayerEntity *FindByGuid(MafiaNet::PeerGuid guid) const;
        MafiaNet::PeerGuid GuidForNetworkId(uint64_t networkId) const;
        bool SuggestedSpawnPosition(uint64_t networkId, glm::vec3 &position) const;
        bool HasRecentFatalFall(uint64_t networkId) const;
        template <typename Callback>
        void ForEach(Callback &&callback) const {
            for (const auto &[guid, session] : _sessions) {
                if (auto *player = FindByNetworkId(session.networkId)) {
                    callback(player);
                }
            }
        }
        void OnMovement(const Shared::Player::Movement &movement, MafiaNet::PeerGuid sender);
        void OnNativePose(const Shared::Player::NativePose &pose, MafiaNet::PeerGuid sender);
        void OnClimb(const Shared::Player::Climb &climb, MafiaNet::PeerGuid sender);
        void OnCameraView(const Shared::Player::CameraView &view, MafiaNet::PeerGuid sender);
        void Update();
        void Reset();

      private:
        std::string UniqueNickname(std::string_view requested, uint64_t networkId) const;
        struct Session {
            uint64_t networkId    = 0;
            uint32_t lastSequence = 0;
            bool hasSequence      = false;
            std::chrono::steady_clock::time_point lastMovement;
            std::chrono::steady_clock::time_point lastCameraView;
            uint32_t lastCameraViewSequence = 0;
            glm::vec3 suggestedPosition  = glm::vec3(0.0f);
            uint64_t suggestedGeneration = 0;
            std::deque<std::pair<std::chrono::steady_clock::time_point, float>> recentHeights;
        };
        std::unordered_map<MafiaNet::PeerGuid, Session> _sessions;
        uint64_t _currentMissionGeneration = 1;
    };
} // namespace Mafia1Online::Features::Player
