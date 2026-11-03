#include <utils/safe_win32.h>

#include "city_signs.h"

#include "crash_pieces.h"

#include <MinHook.h>
#include <logging/logger.h>
#include <mafia1/sdk/core/mission.h>
#include <mafia1/sdk/world/native_props.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace Mafia1Online::Features::Props {
    namespace {
        using SDK::Player::NativeActor;
        using SDK::Player::Vector3;
        using SDK::World::NativeCachePlacement;
        using SDK::World::NativeCacheSegment;
        using SDK::World::NativeCityCache;
        namespace Wire = Shared::World::Props;

        SDK::World::HandleSignalMarkCrashCall gOriginal = nullptr;
        CitySigns *gSigns = nullptr;

        void __fastcall StrikeHook(void *game, void *, uint32_t *primitive, const Vector3 *hit, const Vector3 *unused, const Vector3 *impulse, NativeActor *person) {
            if (gSigns) {
                gSigns->OnStrike(game, primitive, hit, unused, impulse, person);
            } else {
                gOriginal(game, primitive, hit, unused, impulse, person);
            }
        }

        SDK::Collision::TreeLevel *Tree() {
            return SDK::Collision::LoadedTree(SDK::Core::System::kCollision);
        }

        std::span<const uint16_t> SignalMarkIds(const void *game) {
            const auto *base = static_cast<const std::byte *>(game);
            const auto *first = *reinterpret_cast<const uint16_t *const *>(base + SDK::World::kSignalMarkIdsFirstOffset);
            const auto *last = *reinterpret_cast<const uint16_t *const *>(base + SDK::World::kSignalMarkIdsLastOffset);
            return first ? std::span<const uint16_t>(first, last) : std::span<const uint16_t>();
        }

        bool IsSign(const void *game, uint16_t group) {
            const auto ids = SignalMarkIds(game);
            return std::find(ids.begin(), ids.end(), group) != ids.end();
        }

        struct Found {
            NativeCachePlacement *placement = nullptr;
            uint16_t segment = 0;
            uint16_t index = 0;
        };

        // HandleSignalMarkCrash's search (0x5b7c0c-0x5b7db7): the nearest
        // active sign to the hit, under kSignalMarkHeight up and down and
        // kSignalMarkReach across; streamed: only in segments streamed in.
        Found Nearest(const NativeCityCache &cache, const void *game, const Vector3 &hit, bool streamed) {
            Found found;
            float closest = SDK::World::kSignalMarkReach * SDK::World::kSignalMarkReach;
            const auto segments = cache.segments.Items();
            for (size_t s = segments.size(); s-- > 0;) {
                if (streamed && !segments[s].active) {
                    continue;
                }
                const auto placements = segments[s].placements.Items();
                for (size_t p = placements.size(); p-- > 0;) {
                    auto &placement = placements[p];
                    if (!placement.active || !IsSign(game, placement.group)) {
                        continue;
                    }
                    const float dx = placement.position.x - hit.x;
                    const float dy = placement.position.y - hit.y;
                    const float dz = placement.position.z - hit.z;
                    if (dy * dy >= SDK::World::kSignalMarkHeight * SDK::World::kSignalMarkHeight) {
                        continue;
                    }
                    if (const float across = dx * dx + dz * dz; across < closest) {
                        closest = across;
                        found = {&placement, static_cast<uint16_t>(s), static_cast<uint16_t>(p)};
                    }
                }
            }
            return found;
        }

        NativeCachePlacement *Find(NativeCityCache &cache, uint16_t segment, uint16_t index) {
            const auto segments = cache.segments.Items();
            if (segment >= segments.size()) {
                return nullptr;
            }
            const auto placements = segments[segment].placements.Items();
            return index < placements.size() ? &placements[index] : nullptr;
        }

        // The crash model's file name, as HandleSignalMarkCrash passes it
        // (0x5b7ddb-0x5b7df9).
        char *ModelFileName(SDK::Scene::NativeFrame *model) {
            static char empty[] = "";
            char *name = *reinterpret_cast<char **>(reinterpret_cast<std::byte *>(model) + SDK::World::kModelFileNameOffset);
            if (!name) {
                return empty;
            }
            char *base = std::strrchr(name, '\\');
            return base ? base + 1 : name;
        }

        Wire::Vector ToWire(const Vector3 &vector) {
            return {vector.x, vector.y, vector.z};
        }
        Vector3 ToNative(const Wire::Vector &vector) {
            return {vector.x, vector.y, vector.z};
        }
    } // namespace

    bool CitySigns::Install(Report report) {
        _report = std::move(report);
        gSigns = this;
        const auto target = reinterpret_cast<void *>(SDK::World::kHandleSignalMarkCrash);
        if (MH_CreateHook(target, reinterpret_cast<void *>(&StrikeHook), reinterpret_cast<void **>(&gOriginal)) != MH_OK || MH_EnableHook(target) != MH_OK) {
            Uninstall();
            return false;
        }
        return true;
    }

    void CitySigns::Uninstall() {
        const auto target = reinterpret_cast<void *>(SDK::World::kHandleSignalMarkCrash);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        gOriginal = nullptr;
        gSigns = nullptr;
        _report = nullptr;
        Reset();
    }

    void CitySigns::Reset() {
        _broken.clear();
    }

    void CitySigns::Record(uint16_t segment, uint16_t placement, SDK::World::TreePrimitiveId primitive) {
        _broken.push_back({segment, placement, primitive});
    }

    void CitySigns::OnStrike(void *game, uint32_t *primitive, const Vector3 *hit, const Vector3 *unused, const Vector3 *impulse, NativeActor *person) {
        auto &cache = *NativeCityCache::Get();
        if (!primitive || !(*primitive & SDK::World::kSignalMarkRequired) || (*primitive & SDK::World::kCollisionPrimitiveOff) || !cache.open) {
            gOriginal(game, primitive, hit, unused, impulse, person);
            return;
        }
        // The sign the native code will take, if it finds one.
        Found broken = Nearest(cache, game, *hit, true);
        const size_t piecesMark = CrashPieces::Mark();
        gOriginal(game, primitive, hit, unused, impulse, person);
        if (*primitive & SDK::World::kCollisionPrimitiveOff) {
            if (!broken.placement || broken.placement->active) {
                return;
            }
        } else {
            // None streamed in: the camera is away, and nobody here sees the
            // pieces it would have thrown.
            broken = Nearest(cache, game, *hit, false);
            if (!broken.placement) {
                return;
            }
            *primitive |= SDK::World::kCollisionPrimitiveOff;
            cache.Close(broken.placement);
        }
        const auto struck = SDK::World::IdentifyTreePrimitive(Tree(), primitive);
        Record(broken.segment, broken.index, struck);
        Wire::SignBreak sign;
        sign.segment = broken.segment;
        sign.placement = broken.index;
        sign.primitiveKind = static_cast<uint8_t>(struck.kind);
        sign.primitiveIndex = struck.index;
        sign.position = ToWire(broken.placement->position);
        sign.hit = ToWire(*hit);
        sign.impulse = ToWire(*impulse);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Traffic sign {}/{} broken here (primitive {} #{})", sign.segment, sign.placement, sign.primitiveKind,
                                                                     sign.primitiveIndex);
        if (_report && sign.Valid()) {
            _report(sign, piecesMark);
        }
    }

    void CitySigns::Apply(const Wire::SignBreak &received, bool silent) {
        auto &cache = *NativeCityCache::Get();
        auto *mission = SDK::Core::Mission::Get();
        auto *game = mission->Game();
        auto *placement = cache.open ? Find(cache, received.segment, received.placement) : nullptr;
        if (!placement || !IsSign(game, placement->group) || Wire::Distance(ToWire(placement->position), received.position) > Wire::kBreakablePlaceTolerance) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Traffic sign {}/{} is not where the server has it broken", received.segment, received.placement);
            return;
        }
        // Only a primitive that breaks a sign is turned off.
        SDK::World::TreePrimitiveId struck {static_cast<SDK::World::TreePrimitiveKind>(received.primitiveKind), received.primitiveIndex};
        uint32_t *primitive = SDK::World::ResolveTreePrimitive(Tree(), struck);
        if (primitive && !(*primitive & SDK::World::kSignalMarkRequired)) {
            primitive = nullptr;
        }
        if (!primitive) {
            struck = {};
        }
        const bool standing = placement->active;
        if (standing) {
            auto sign = received;
            sign.Clamp();
            // A segment streamed out has no model to throw pieces from.
            auto *model = cache.SegmentStreamed(received.segment) ? cache.PooledModel(*placement) : nullptr;
            silent = silent || !model;
            if (!silent) {
                Vector3 hit = ToNative(sign.hit);
                Vector3 impulse = ToNative(sign.impulse);
                reinterpret_cast<SDK::World::CreateCrashActorsCall>(SDK::World::kCreateCrashActors)(model, &hit, &impulse, sign.impulse.Length(), ModelFileName(model), false, 0, 0,
                                                                                                    true, true);
            }
            cache.Close(placement);
        }
        if (primitive) {
            *primitive |= SDK::World::kCollisionPrimitiveOff;
        }
        // Broken here already (or the reporter's own echo): only its
        // primitive is new.
        const bool known = std::any_of(_broken.begin(), _broken.end(), [&](const Broken &entry) {
            return entry.segment == received.segment && entry.placement == received.placement && entry.primitive.kind == struck.kind &&
                   entry.primitive.index == struck.index;
        });
        if (!known) {
            Record(received.segment, received.placement, struck);
        }
        if (standing) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Traffic sign {}/{} broken by another client{}", received.segment, received.placement,
                                                                         silent ? ", without pieces" : "");
        }
    }

    size_t CitySigns::Repair() {
        auto &cache = *NativeCityCache::Get();
        auto *tree = Tree();
        std::vector<uint16_t> streamed;
        std::vector<NativeCachePlacement *> closed;
        for (const auto &entry : _broken) {
            if (auto *primitive = SDK::World::ResolveTreePrimitive(tree, entry.primitive)) {
                *primitive &= ~SDK::World::kCollisionPrimitiveOff;
            }
            auto *placement = cache.open ? Find(cache, entry.segment, entry.placement) : nullptr;
            if (!placement || placement->active || std::find(closed.begin(), closed.end(), placement) != closed.end()) {
                continue;
            }
            closed.push_back(placement);
            if (cache.SegmentStreamed(entry.segment) && std::find(streamed.begin(), streamed.end(), entry.segment) == streamed.end()) {
                streamed.push_back(entry.segment);
            }
        }
        // A streamed-in segment goes out before its signs are back on and
        // comes in with models for them; the others get theirs when they
        // stream in.
        for (const uint16_t segment : streamed) {
            cache.StreamOut(segment);
        }
        for (auto *placement : closed) {
            placement->active = true;
        }
        for (const uint16_t segment : streamed) {
            cache.StreamIn(segment);
        }
        _broken.clear();
        return closed.size();
    }

    std::pair<size_t, size_t> CitySigns::Count() const {
        const auto &cache = *NativeCityCache::Get();
        const auto *game = SDK::Core::Mission::Get()->Game();
        size_t signs = 0;
        if (cache.open) {
            for (const auto &segment : cache.segments.Items()) {
                for (const auto &placement : segment.placements.Items()) {
                    signs += IsSign(game, placement.group) ? 1 : 0;
                }
            }
        }
        size_t primitives = 0;
        if (const auto *tree = Tree()) {
            const auto count = [&](const auto *array, uint32_t size) {
                for (uint32_t i = 0; array && i < size; ++i) {
                    primitives += (*reinterpret_cast<const uint32_t *>(&array[i].header) & SDK::World::kSignalMarkRequired) ? 1 : 0;
                }
            };
            count(tree->faces, tree->faceCount);
            count(tree->orientedBoxes, tree->orientedBoxCount);
            count(tree->axisBoxes, tree->axisBoxCount);
            count(tree->spheres, tree->sphereCount);
            count(tree->plates, tree->plateCount);
            count(tree->cylinders, tree->cylinderCount);
        }
        return {signs, primitives};
    }
} // namespace Mafia1Online::Features::Props
