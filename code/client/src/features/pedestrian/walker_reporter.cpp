#include <utils/safe_win32.h>

#include "walker_reporter.h"

#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/pedestrian/pedestrian_report.h"
#include "shared/net/sample_clock.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafia1/sdk/traffic/native_traffic.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>

namespace Mafia1Online::Features::Pedestrian {
    namespace {
        float YawOf(SDK::Scene::NativeFrame *model) {
            const auto forward = model->WorldDirection();
            return std::atan2(forward.x, forward.z);
        }
    } // namespace

    void WalkerReporter::Reset() {
        _reported.clear();
    }

    uint32_t WalkerReporter::KeyOf(SDK::Traffic::NativeTrafficElement *element) const {
        const auto reported = _reported.find(element);
        return reported == _reported.end() ? 0 : reported->second.key;
    }

    SDK::Traffic::NativeTrafficElement *WalkerReporter::ActiveElement(uint32_t key) const {
        for (const auto &[element, reported] : _reported) {
            if (reported.key == key && element->IsActive()) {
                return element;
            }
        }
        return nullptr;
    }

    bool WalkerReporter::Retire(uint32_t key) {
        for (auto &[element, reported] : _reported) {
            if (reported.key == key && element->IsActive()) {
                element->Deactivate();
                return true;
            }
        }
        return false;
    }

    void WalkerReporter::Update(uint64_t missionGeneration, const std::vector<SDK::Traffic::NativeTrafficGenerator *> &generators, std::chrono::milliseconds corpseLifetime) {
        // A corpse past its lifetime retires before this pass reports it.
        if (corpseLifetime.count() > 0) {
            const auto now = std::chrono::steady_clock::now();
            for (auto &[element, reported] : _reported) {
                if (reported.dead && element->IsActive() && now - reported.deadSince >= corpseLifetime) {
                    element->Deactivate();
                }
            }
        }
        if (missionGeneration != _missionGeneration) {
            _reported.clear();
            _nextKey = 1;
            _missionGeneration = missionGeneration;
        }
        auto *network = Framework::CoreModules::GetNetworkPeer();

        std::unordered_set<SDK::Traffic::NativeTrafficElement *> active;
        for (auto **element = SDK::Traffic::ElementsBegin(); element != SDK::Traffic::ElementsEnd(); ++element) {
            if ((*element)->IsActive() && (*element)->Model()) {
                active.insert(*element);
            }
        }
        for (auto it = _reported.begin(); it != _reported.end();) {
            if (active.contains(it->first)) {
                ++it;
                continue;
            }
            Shared::Pedestrian::Retired retired;
            retired.missionGeneration = missionGeneration;
            retired.key = it->second.key;
            network->BroadcastRPC(retired);
            it = _reported.erase(it);
        }
        for (auto *element : active) {
            if (_reported.contains(element)) {
                continue;
            }
            // The pool opened "Models\<name>"; the model cache adds that
            // directory itself, so players get the bare file name.
            std::string_view model = element->ModelFileName();
            if (const auto slash = model.find_last_of("\\/"); slash != std::string_view::npos) {
                model.remove_prefix(slash + 1);
            }
            if (model.empty() || model.size() > Shared::Entities::PedestrianEntity::kMaxModelLength) {
                continue;
            }
            const auto pool = std::find_if(generators.begin(), generators.end(), [&](auto *generator) {
                return generator->OwnsElement(element);
            });
            const auto position = element->Model()->WorldPosition();
            Shared::Pedestrian::Spawned spawned;
            spawned.missionGeneration = missionGeneration;
            spawned.key = _nextKey++;
            spawned.model = std::string(model);
            spawned.generator = pool == generators.end() ? Shared::Pedestrian::Spawned::kNoGenerator : static_cast<uint8_t>(pool - generators.begin());
            spawned.walkStyle = static_cast<uint8_t>(element->WalkStyle());
            spawned.umbrella = element->HasUmbrella();
            spawned.x = position.x;
            spawned.y = position.y;
            spawned.z = position.z;
            spawned.yaw = YawOf(element->Model());
            network->BroadcastRPC(spawned);
            _reported.emplace(element, Reported {spawned.key, position});
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - _lastSnapshot;
        if (elapsed >= kSnapshotInterval) {
            // A first or long-delayed snapshot measures speed over one interval.
            const float seconds = elapsed > 4 * kSnapshotInterval ? std::chrono::duration<float>(kSnapshotInterval).count() : std::chrono::duration<float>(elapsed).count();
            _lastSnapshot = now;
            SendSnapshots(missionGeneration, seconds);
        }
    }

    void WalkerReporter::SendSnapshots(uint64_t missionGeneration, float seconds) {
        auto *network = Framework::CoreModules::GetNetworkPeer();
        Shared::Pedestrian::Snapshot snapshot;
        snapshot.missionGeneration = missionGeneration;
        snapshot.hostClockMs = Shared::Net::SampleClock(MafiaNet::GetTime());
        const auto flush = [&] {
            if (snapshot.count) {
                network->BroadcastRPC(snapshot, MafiaNet::Priority::High, MafiaNet::Reliability::Unreliable);
                snapshot.count = 0;
            }
        };
        for (auto &[element, reported] : _reported) {
            const auto position = element->Model()->WorldPosition();
            const float dx = position.x - reported.lastPosition.x;
            const float dz = position.z - reported.lastPosition.z;
            reported.lastPosition = position;
            auto &pose = snapshot.walkers[snapshot.count++];
            pose.key = reported.key;
            pose.x = position.x;
            pose.y = position.y;
            pose.z = position.z;
            pose.yaw = YawOf(element->Model());
            pose.velocityX = dx / seconds;
            pose.velocityZ = dz / seconds;
            pose.dead = element->IsDead();
            pose.alpha = static_cast<uint8_t>(std::clamp(element->Alpha(), 0.0f, 1.0f) * 255.0f + 0.5f);
            if (pose.dead && !reported.dead) {
                reported.dead = true;
                reported.deadSince = std::chrono::steady_clock::now();
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Walker {} died (animation {})", reported.key, element->CurrentAnimation());
            }
            pose.animation = static_cast<int16_t>(element->CurrentAnimation());
            pose.animationFlag = element->AnimationFlag();
            pose.umbrella = element->HasUmbrella();
            if (snapshot.count == Shared::Pedestrian::Snapshot::kMaxWalkers) {
                flush();
            }
        }
        flush();
    }
} // namespace Mafia1Online::Features::Pedestrian
