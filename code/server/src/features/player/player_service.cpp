#include "player_service.h"

#include "shared/features/chat/text_policy.h"

#include "shared/features/car/car_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_climb.h"
#include "shared/features/player/player_camera_view.h"
#include "shared/features/player/player_movement.h"
#include "shared/features/player/player_native_pose.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>

namespace Mafia1Online::Features::Player {
    namespace {
        constexpr std::string_view kHumanModelExtension = ".i3d";
        constexpr size_t kMaxHumanModelNameBytes = 64;

        bool ValidPosition(const glm::vec3 &position, float yaw) {
            return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) && std::isfinite(yaw) && std::abs(position.x) <= 50000.0f && std::abs(position.y) <= 50000.0f && std::abs(position.z) <= 50000.0f;
        }

        bool ValidModelIdentifier(std::string_view model) {
            if (model.size() <= kHumanModelExtension.size() || model.size() > kMaxHumanModelNameBytes || !model.ends_with(kHumanModelExtension)) {
                return false;
            }
            return std::all_of(model.begin(), model.end(), [](unsigned char character) {
                return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
            });
        }

        const Shared::Entities::CarEntity *SeatedCar(Shared::Entities::PlayerEntity &player) {
            const Shared::Entities::CarEntity *seated = nullptr;
            Framework::CoreModules::GetReplication()->ForEach<Shared::Entities::CarEntity>([&](Shared::Entities::CarEntity *car) {
                for (uint8_t seat = 0; !seated && seat < car->seatCount && seat < Shared::Entities::CarEntity::kMaxSeats; ++seat) {
                    if (car->occupantIds[seat] == player.GetNetworkID() && car->occupantGenerations[seat] == player.spawnGeneration) {
                        seated = car;
                    }
                }
            });
            return seated;
        }
    } // namespace

    void PlayerService::RegisterRPC() {
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::Movement>([this](const Shared::Player::Movement &movement, MafiaNet::Packet *packet) {
            OnMovement(movement, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::Climb>([this](const Shared::Player::Climb &climb, MafiaNet::Packet *packet) {
            OnClimb(climb, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::NativePose>([this](const Shared::Player::NativePose &pose, MafiaNet::Packet *packet) {
            OnNativePose(pose, MafiaNet::ToPeerGuid(packet->guid));
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<Shared::Player::CameraView>([this](const Shared::Player::CameraView &view, MafiaNet::Packet *packet) {
            OnCameraView(view, MafiaNet::ToPeerGuid(packet->guid));
        });
    }

    void PlayerService::OnConnect(const Framework::Integrations::Server::PlayerConnectionData &info) {
        auto *replication               = Framework::CoreModules::GetReplication();
        auto *player                    = replication->CreateEntity<Shared::Entities::PlayerEntity>();
        player->controllerGuid          = static_cast<uint64_t>(info.guid);
        player->missionGeneration       = _currentMissionGeneration;
        player->nickname                = UniqueNickname(info.nickname, player->GetNetworkID());
        player->playerIndex             = info.playerIndex;
        player->streaming.alwaysVisible = true; // Until native movement gives the viewer a position.
        replication->SetViewer(info.guid, player);
        _sessions[info.guid] = {player->GetNetworkID()};
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player '{}' joined as replica {}", player->nickname, player->GetNetworkID());
    }

    std::string PlayerService::UniqueNickname(std::string_view requested, uint64_t networkId) const {
        std::string base = Shared::Chat::SanitizeLine(requested, Shared::Chat::kMaxNicknameCodePoints);
        if (base.empty()) {
            base = "Player";
        }
        const auto taken = [&](const std::string &candidate) {
            const std::string folded = Shared::Chat::FoldNickname(candidate);
            bool found = false;
            ForEach([&](Shared::Entities::PlayerEntity *other) {
                found = found || (other->GetNetworkID() != networkId && Shared::Chat::FoldNickname(other->nickname) == folded);
            });
            return found;
        };
        std::string candidate = base;
        for (unsigned suffix = 2; taken(candidate); ++suffix) {
            const std::string tag = " (" + std::to_string(suffix) + ")";
            // Keep the suffix inside the length limit, cutting whole code points.
            std::string trimmed = Shared::Chat::SanitizeLine(base, Shared::Chat::kMaxNicknameCodePoints - tag.size());
            candidate = trimmed + tag;
        }
        return candidate;
    }

    std::string PlayerService::SetNickname(uint64_t networkId, std::string_view requested) {
        auto *player = FindByNetworkId(networkId);
        if (!player) {
            return {};
        }
        player->nickname = UniqueNickname(requested, networkId);
        return player->nickname;
    }

    bool PlayerService::SetModel(uint64_t networkId, std::string_view model) {
        auto *player = FindByNetworkId(networkId);
        if (!player || !ValidModelIdentifier(model)) {
            return false;
        }
        player->model = model;
        return true;
    }

    void PlayerService::OnDisconnect(MafiaNet::PeerGuid guid) {
        if (const auto it = _sessions.find(guid); it != _sessions.end()) {
            const uint64_t departedId = it->second.networkId;
            ForEach([&](Shared::Entities::PlayerEntity *player) {
                if (player->cameraTargetId == departedId) {
                    player->cameraTargetId = 0;
                }
            });
        }
        _sessions.erase(guid);
    }

    bool PlayerService::SetCameraTarget(uint64_t observerId, uint64_t targetId) {
        auto *observer = FindByNetworkId(observerId);
        if (!observer) {
            return false;
        }
        if (targetId != 0 && targetId != observerId) {
            const auto *target = FindByNetworkId(targetId);
            if (!target || target->missionGeneration != observer->missionGeneration) {
                return false;
            }
        }
        observer->cameraTargetId = targetId == observerId ? 0 : targetId;
        return true;
    }

    Shared::Entities::PlayerEntity *PlayerService::FindByNetworkId(uint64_t networkId) const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (!replication) {
            return nullptr;
        }
        for (const auto &[guid, session] : _sessions) {
            if (session.networkId == networkId) {
                auto *entity = replication->GetEntity<Shared::Entities::PlayerEntity>(networkId);
                return entity && entity->controllerGuid == static_cast<uint64_t>(guid) ? entity : nullptr;
            }
        }
        return nullptr;
    }

    Shared::Entities::PlayerEntity *PlayerService::FindByGuid(MafiaNet::PeerGuid guid) const {
        const auto it = _sessions.find(guid);
        if (it == _sessions.end()) {
            return nullptr;
        }
        return FindByNetworkId(it->second.networkId);
    }

    MafiaNet::PeerGuid PlayerService::GuidForNetworkId(uint64_t networkId) const {
        for (const auto &[guid, session] : _sessions) {
            if (session.networkId == networkId) {
                return guid;
            }
        }
        return MafiaNet::UNASSIGNED_PEER_GUID;
    }

    bool PlayerService::SuggestedSpawnPosition(uint64_t networkId, glm::vec3 &position) const {
        const auto it = _sessions.find(GuidForNetworkId(networkId));
        if (it == _sessions.end() || it->second.suggestedGeneration != _currentMissionGeneration) {
            return false;
        }
        position = it->second.suggestedPosition;
        return true;
    }

    bool PlayerService::Spawn(uint64_t networkId, const glm::vec3 &position, float yaw, uint64_t missionGeneration) {
        auto *player = FindByNetworkId(networkId);
        if (!player || !ValidPosition(position, yaw) || missionGeneration == 0 || missionGeneration != _currentMissionGeneration) {
            return false;
        }
        ++player->spawnGeneration;
        player->missionGeneration = missionGeneration;
        player->position          = position;
        player->velocity          = glm::vec3(0.0f);
        player->rotation          = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        player->health            = 100.0f;
        player->spawned           = true;
        player->alive             = true;
        if (auto it = _sessions.find(GuidForNetworkId(networkId)); it != _sessions.end()) {
            it->second.hasSequence  = false;
            it->second.lastMovement = {};
            it->second.recentHeights.clear();
            it->second.recentHeights.emplace_back(std::chrono::steady_clock::now(), position.y);
        }
        return true;
    }

    bool PlayerService::Respawn(uint64_t networkId, const glm::vec3 &position, float yaw, uint64_t missionGeneration) {
        return Spawn(networkId, position, yaw, missionGeneration);
    }

    bool PlayerService::Despawn(uint64_t networkId) {
        auto *player = FindByNetworkId(networkId);
        if (!player) {
            return false;
        }
        ++player->spawnGeneration;
        player->spawned  = false;
        player->alive    = false;
        player->velocity = glm::vec3(0.0f);
        return true;
    }

    bool PlayerService::SetHealth(uint64_t networkId, float health) {
        auto *player = FindByNetworkId(networkId);
        if (!player || !player->spawned || !std::isfinite(health) || (!player->alive && health > 0.0f)) {
            return false;
        }
        player->health = std::clamp(health, 0.0f, 100.0f);
        player->alive  = player->health > 0.0f;
        if (!player->alive) {
            player->velocity = glm::vec3(0.0f);
        }
        return true;
    }

    bool PlayerService::SetMoney(uint64_t networkId, uint32_t amount) {
        auto *player = FindByNetworkId(networkId);
        if (!player || amount > Shared::Entities::PlayerEntity::kMaxMoney) {
            return false;
        }
        player->money = amount;
        return true;
    }

    bool PlayerService::GiveMoney(uint64_t networkId, int32_t amount) {
        auto *player = FindByNetworkId(networkId);
        if (!player) {
            return false;
        }
        const int64_t next = static_cast<int64_t>(player->money) + amount;
        if (next < 0 || next > Shared::Entities::PlayerEntity::kMaxMoney) {
            return false;
        }
        player->money = static_cast<uint32_t>(next);
        return true;
    }

    bool PlayerService::TrySpendMoney(uint64_t networkId, uint32_t cost) {
        auto *player = FindByNetworkId(networkId);
        if (!player || cost > player->money) {
            return false;
        }
        player->money -= cost;
        return true;
    }

    void PlayerService::ResetForMission(uint64_t missionGeneration) {
        _currentMissionGeneration = missionGeneration;
        auto *replication         = Framework::CoreModules::GetReplication();
        for (auto &[guid, session] : _sessions) {
            session.suggestedGeneration = 0;
            session.hasSequence         = false;
            session.recentHeights.clear();
            auto *player = replication->GetEntity<Shared::Entities::PlayerEntity>(session.networkId);
            if (!player) {
                continue;
            }
            ++player->spawnGeneration;
            player->missionGeneration = missionGeneration;
            player->spawned           = false;
            player->alive             = false;
            player->velocity          = glm::vec3(0.0f);
        }
    }

    void PlayerService::OnNativePose(const Shared::Player::NativePose &pose, MafiaNet::PeerGuid sender) {
        const auto it = _sessions.find(sender);
        if (it == _sessions.end() || pose.networkId != it->second.networkId || pose.missionGeneration != _currentMissionGeneration) {
            return;
        }
        auto *player = FindByGuid(sender);
        if (!player || player->missionGeneration != pose.missionGeneration || player->spawned) {
            return;
        }
        const glm::vec3 position(pose.x, pose.y, pose.z);
        if (!ValidPosition(position, pose.yaw)) {
            return;
        }
        it->second.suggestedPosition   = position;
        it->second.suggestedGeneration = pose.missionGeneration;
    }

    void PlayerService::OnClimb(const Shared::Player::Climb &climb, MafiaNet::PeerGuid sender) {
        const auto it = _sessions.find(sender);
        if (it == _sessions.end() || climb.networkId != it->second.networkId) {
            return;
        }
        auto *player = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::PlayerEntity>(climb.networkId);
        const glm::vec3 position(climb.x, climb.y, climb.z);
        const glm::vec2 direction(climb.directionX, climb.directionZ);
        if (!player || !player->spawned || !player->alive || climb.missionGeneration != player->missionGeneration || climb.spawnGeneration != player->spawnGeneration ||
            !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) || !std::isfinite(direction.x) || !std::isfinite(direction.y) ||
            glm::length(direction) < 0.5f || glm::length(direction) > 1.5f || glm::distance(position, player->position) > 6.0f || SeatedCar(*player)) {
            return;
        }
        auto relay = climb;
        Framework::CoreModules::GetNetworkPeer()->BroadcastRPC(relay);
    }

    void PlayerService::OnCameraView(const Shared::Player::CameraView &view, MafiaNet::PeerGuid sender) {
        const auto it = _sessions.find(sender);
        if (it == _sessions.end() || view.networkId != it->second.networkId) {
            return;
        }
        auto *source = FindByGuid(sender);
        if (!source || !source->spawned || !source->alive || source->missionGeneration != view.missionGeneration ||
            source->spawnGeneration != view.spawnGeneration || !std::isfinite(view.pitch) || view.pitch < 0.0f || view.pitch > 1.0f ||
            !std::isfinite(view.yaw) || std::abs(view.yaw) > 3.141593f) {
            return;
        }
        auto &session = it->second;
        if (session.lastCameraViewSequence != 0 && static_cast<int32_t>(view.sequence - session.lastCameraViewSequence) <= 0) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (session.lastCameraView != std::chrono::steady_clock::time_point {} &&
            now - session.lastCameraView < std::chrono::milliseconds(20)) {
            return;
        }
        session.lastCameraView = now;
        session.lastCameraViewSequence = view.sequence;
        ForEach([&](Shared::Entities::PlayerEntity *observer) {
            if (observer->cameraTargetId == source->GetNetworkID() && observer->missionGeneration == source->missionGeneration) {
                auto relay = view;
                Framework::CoreModules::GetNetworkPeer()->SendRPC(relay, MafiaNet::ToGuid(GuidForNetworkId(observer->GetNetworkID())),
                    MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
            }
        });
    }

    void PlayerService::OnMovement(const Shared::Player::Movement &movement, MafiaNet::PeerGuid sender) {
        auto it = _sessions.find(sender);
        if (it == _sessions.end() || movement.networkId != it->second.networkId) {
            return;
        }
        auto *player = Framework::CoreModules::GetReplication()->GetEntity<Shared::Entities::PlayerEntity>(movement.networkId);
        if (!player || !player->spawned || !player->alive || movement.missionGeneration != player->missionGeneration || movement.spawnGeneration != player->spawnGeneration) {
            return;
        }
        auto &session = it->second;
        if (session.hasSequence && static_cast<int32_t>(movement.sequence - session.lastSequence) <= 0) {
            return;
        }
        const glm::vec3 position(movement.x, movement.y, movement.z);
        if (!ValidPosition(position, movement.yaw)) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (const auto *car = SeatedCar(*player)) {
            // A seated human moves with the car, which can outrun the on-foot
            // bound and be teleported by scripts. Bound it to the car instead;
            // the observer's copy of the car trails the controller's report.
            if (glm::distance(position, car->position) > 6.0f + glm::length(car->velocity) * 0.5f) {
                return;
            }
            const float elapsed = session.hasSequence ? std::chrono::duration<float>(now - session.lastMovement).count() : 0.0f;
            player->velocity    = elapsed > 0.001f ? (position - player->position) / elapsed : glm::vec3(0.0f);
            // Driving downhill is not a fall.
            session.recentHeights.clear();
        }
        else if (session.hasSequence) {
            const float elapsed    = std::clamp(std::chrono::duration<float>(now - session.lastMovement).count(), 0.0f, 0.5f);
            const auto delta       = position - player->position;
            const float horizontal = std::sqrt(delta.x * delta.x + delta.z * delta.z);
            if (horizontal > 2.0f + elapsed * 20.0f || std::abs(delta.y) > 2.0f + elapsed * 80.0f) {
                return;
            }
            player->velocity = elapsed > 0.001f ? (position - player->position) / elapsed : glm::vec3(0.0f);
        }
        else if (glm::distance(position, player->position) > 5.0f) {
            return;
        }
        player->position     = position;
        player->rotation     = glm::angleAxis(movement.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        player->locomotion   = movement.locomotion;
        session.lastSequence = movement.sequence;
        session.hasSequence  = true;
        session.lastMovement = now;
        session.recentHeights.emplace_back(now, position.y);
        while (!session.recentHeights.empty() && now - session.recentHeights.front().first > std::chrono::seconds(6)) {
            session.recentHeights.pop_front();
        }
    }

    bool PlayerService::HasRecentFatalFall(uint64_t networkId) const {
        const auto it = _sessions.find(GuidForNetworkId(networkId));
        auto *player  = FindByNetworkId(networkId);
        if (it == _sessions.end() || !player || !player->spawned || !player->alive || it->second.recentHeights.empty() || std::chrono::steady_clock::now() - it->second.lastMovement > std::chrono::seconds(3)) {
            return false;
        }
        float high = player->position.y;
        for (const auto &[time, height] : it->second.recentHeights) {
            high = std::max(high, height);
        }
        return high - player->position.y >= 5.0f;
    }

    void PlayerService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        for (auto it = _sessions.begin(); it != _sessions.end();) {
            auto *viewer = replication->GetViewer(it->first);
            if (!viewer || viewer->GetNetworkID() != it->second.networkId) {
                it = _sessions.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void PlayerService::Reset() {
        _sessions.clear();
    }
} // namespace Mafia1Online::Features::Player
