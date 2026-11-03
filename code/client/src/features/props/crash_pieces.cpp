#include "crash_pieces.h"

#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/world/native_props.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Mafia1Online::Features::Props {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::World::NativeCrashPiece;
        using SDK::World::NativeCrashWorld;
        namespace Wire = Shared::World::Props;

        // A piece still moving when the report comes is put there after
        // this long; one that never sleeps is reported after kReportAfter.
        constexpr auto kPlaceAfter = std::chrono::seconds(3);
        constexpr auto kReportAfter = std::chrono::seconds(20);
        // A group with nothing left to do goes after this long.
        constexpr auto kForgetAfter = std::chrono::minutes(2);
        // A piece counts as placed within these of the pose.
        constexpr float kPlacedDistance = 0.05f;
        constexpr float kPlacedCosine = 0.999f;

        std::string Describe(const CrashPieces::Key &key) {
            return key.sign ? "traffic sign " + std::to_string(key.signKey >> 16) + "/" + std::to_string(key.signKey & 0xffff) : "'" + key.name + "'";
        }

        NativeCrashWorld World() {
            return {SDK::Core::Mission::Get()->Game()};
        }

        bool IsPiece(NativeActor *actor) {
            return actor && static_cast<uint32_t>(actor->GetType()) == NativeCrashWorld::kCrashObjectType;
        }

        NativeCrashPiece &PieceOf(NativeActor *actor) {
            return *reinterpret_cast<NativeCrashPiece *>(actor);
        }

        Wire::Vector Unit(const SDK::Player::Vector3 &vector) {
            const float length = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
            return length > 0.0001f ? Wire::Vector {vector.x / length, vector.y / length, vector.z / length} : Wire::Vector {0.0f, 0.0f, 1.0f};
        }

        Wire::Pose PoseOf(NativeActor *actor, size_t index) {
            auto &body = PieceOf(actor).Body();
            Wire::Pose pose;
            pose.index = static_cast<uint16_t>(index);
            const auto position = actor->Position();
            pose.position = {position.x, position.y, position.z};
            pose.direction = Unit(actor->Direction());
            pose.up = Unit(body.Up());
            return pose;
        }

        void Place(NativeActor *actor, const Wire::Pose &pose) {
            const auto here = PoseOf(actor, pose.index);
            if (Wire::Distance(here.position, pose.position) <= kPlacedDistance && Wire::Dot(here.direction, pose.direction) >= kPlacedCosine &&
                Wire::Dot(here.up, pose.up) >= kPlacedCosine) {
                return;
            }
            const auto unit = pose.Orthonormalized();
            PieceOf(actor).Body().Place({unit.position.x, unit.position.y, unit.position.z}, {unit.direction.x, unit.direction.y, unit.direction.z},
                                        {unit.up.x, unit.up.y, unit.up.z});
        }
    } // namespace

    size_t CrashPieces::Mark() {
        return World().TemporaryActors().size();
    }

    void CrashPieces::Capture(const Key &key, size_t mark, bool local) {
        const auto actors = World().TemporaryActors();
        Group group;
        group.key = key;
        group.local = local;
        group.thrown = std::chrono::steady_clock::now();
        for (size_t i = mark; i < actors.size() && group.pieces.size() < Wire::kMaxPieces; ++i) {
            if (IsPiece(actors[i])) {
                group.pieces.push_back(actors[i]);
            }
        }
        if (group.pieces.empty()) {
            return;
        }
        // The same break again (its own echo, a repair since): the newest
        // pieces are the ones that count.
        std::erase_if(_groups, [&](const Group &entry) { return entry.key == key; });
        _groups.push_back(std::move(group));
    }

    void CrashPieces::Apply(const Key &key, const std::vector<Wire::Pose> &poses, bool now) {
        const auto found = std::find_if(_groups.begin(), _groups.end(), [&](const Group &entry) { return entry.key == key; });
        if (found == _groups.end()) {
            return;
        }
        auto &group = *found;
        Prune(group);
        // Its own report: these are the poses it has already.
        group.reported = true;
        group.pending.clear();
        for (const auto &pose : poses) {
            if (pose.index >= group.pieces.size() || !group.pieces[pose.index]) {
                continue;
            }
            if (now) {
                Place(group.pieces[pose.index], pose);
            } else {
                group.pending.push_back(pose);
            }
        }
        group.pendingSince = std::chrono::steady_clock::now();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Pieces of {} go where the breaker has them: {} poses for {} pieces{}", Describe(key), poses.size(),
                                                                     group.pieces.size(), now ? ", placed now" : "");
    }

    void CrashPieces::Prune(Group &group) const {
        const auto actors = World().TemporaryActors();
        for (auto &piece : group.pieces) {
            if (piece && std::find(actors.begin(), actors.end(), piece) == actors.end()) {
                piece = nullptr;
            }
        }
    }

    void CrashPieces::Update() {
        const auto now = std::chrono::steady_clock::now();
        for (auto &group : _groups) {
            Prune(group);
            if (group.local && !group.reported) {
                const bool resting = std::all_of(group.pieces.begin(), group.pieces.end(), [](NativeActor *piece) { return !piece || PieceOf(piece).Body().Asleep(); });
                if (resting || now - group.thrown >= kReportAfter) {
                    group.reported = true;
                    std::vector<Wire::Pose> poses;
                    for (size_t i = 0; i < group.pieces.size(); ++i) {
                        if (group.pieces[i]) {
                            poses.push_back(PoseOf(group.pieces[i], i));
                        }
                    }
                    if (!poses.empty() && _report) {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Pieces of {} at rest here: {} of {}", Describe(group.key), poses.size(), group.pieces.size());
                        _report(group.key, poses);
                    }
                }
            }
            std::erase_if(group.pending, [&](const Wire::Pose &pose) {
                auto *piece = group.pieces[pose.index];
                if (!piece) {
                    return true;
                }
                if (!PieceOf(piece).Body().Asleep() && now - group.pendingSince < kPlaceAfter) {
                    return false;
                }
                Place(piece, pose);
                return true;
            });
        }
        std::erase_if(_groups, [&](const Group &group) {
            return (group.reported || !group.local) && group.pending.empty() && now - group.thrown >= kForgetAfter;
        });
    }

    void CrashPieces::Reset() {
        _groups.clear();
    }
} // namespace Mafia1Online::Features::Props
