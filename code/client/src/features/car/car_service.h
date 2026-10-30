#pragma once

#include "car_pose_buffer.h"
#include "game/entities/native_object_registry.h"
#include "shared/features/car/car_contact.h"
#include "shared/features/car/car_damage_state.h"
#include "shared/features/car/car_entity.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/car_mesh_checkpoint.h"
#include "shared/features/car/car_movement.h"

#include <utils/snapshot_buffer.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::SDK::Car {
    struct NativeCar;
    struct NativeVehicle;
}
namespace Mafia1Online::SDK::Player {
    struct NativeActor;
    struct Vector3;
}

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::Car {
    // Client-side receive buffer and native temporary-car owner. Every native
    // use resolves the handle against the current mission's registry.
    class CarService final {
      public:
        void RegisterRPC();
        void Update(World::WorldService &world);
        bool Sample(uint64_t networkId, Framework::Utils::TransformSnapshot &out) const;
        void Reset();
        SDK::Scene::NativeFrame *OnNativeDestroyed(World::WorldService &world, void *car);
        bool OnNativeExplosion(World::WorldService &world, SDK::Car::NativeCar *car);
        void OnNativeWorldCollision(World::WorldService &world, SDK::Car::NativeCar *car, uint8_t material);
        void OnLocalHumanDeath(World::WorldService &world, void *human);
        void OnNativeDeformed(SDK::Car::NativeCar *car);
        // C_car::Call_Collision: `car`'s sweep hit `other`.
        void OnCarCollision(SDK::Car::NativeCar *car, SDK::Player::NativeActor *other);
        // C_car::Collision: `striker`'s sweep hit `car` with `impulse`. A car
        // another client simulates, hit by one this client does, is reported
        // to its controller, which applies it unless its own physics had it.
        void OnCarHit(SDK::Car::NativeCar *car, SDK::Player::NativeActor *striker, const SDK::Player::Vector3 &position,
                      const SDK::Player::Vector3 &impulse, const SDK::Player::Vector3 &normal, float mass);
        // Collision_Filter_Body: whether `car`'s sweep passes through `other`,
        // the two cars of a reported contact this client applied
        // (ContactReplayed).
        bool ContactGhosted(SDK::Car::NativeCar *car, SDK::Player::NativeActor *other) const;
        // C_car::AI detour. An observer car skips native physics: the
        // interpolated pose is written inside the actor tick, before the
        // camera reads it, and only wheels and engine sound are refreshed.
        using NativeStep = void (*)(SDK::Car::NativeCar *, unsigned int);
        bool DriveObserver(SDK::Car::NativeCar *car, unsigned int frameMs, NativeStep native);
        // Switches observers between interpolation of the replicated path
        // and native physics corrected toward the pose predicted to now.
        bool ToggleSyncMode();
        // Sync trace (Debug::SyncTrace) of the cars DriveObserver placed this
        // frame: completed once C_game::Tick returns, after C_car::Update and
        // the camera, and written at the end of the frame, before rendering,
        // so a write after the camera read or a frame the car moved twice or
        // not at all shows.
        void TraceAfterGameTick();
        void TraceFrameEnd();

        // Simulation host: a car the retail game owns (a traffic pool car, or
        // a temporary car it created) is this network car's native car. The
        // host never creates a car of its own for a car it drives, and never
        // deletes a pool car; a temporary one goes with RemoveTemporaryActor
        // when the network car does.
        enum class Origin : uint8_t {
            Created,
            Pool,
            Temporary,
        };
        bool Adopt(World::WorldService &world, uint64_t networkId, SDK::Car::NativeCar *car, Origin origin);
        // Unbinds the adopted car, which stays in the retail world.
        void ReleaseAdopted(World::WorldService &world, uint64_t networkId);
        [[nodiscard]] bool IsAdopted(uint64_t networkId) const;
        void OnSimulated(SDK::Car::NativeCar *car, unsigned int frameMs);

      private:
        struct Stream {
            struct PendingHit {
                Shared::Car::AuthoritativeHit hit;
                std::chrono::steady_clock::time_point received;
            };
            using PoseSample = CarPoseSample;
            // Replicated poses dated on the local clock from the controller's
            // physics clock, rendered a delay behind (CarPoseBuffer).
            CarPoseBuffer poses;
            double lastPoseTime = 0.0;
            std::deque<double> offsetWindow;
            // Controller: the frame clock (World::FrameClock) of the physics
            // step its car's pose comes from.
            double simClock = 0.0;
            // The update serial of the last actor tick DriveObserver placed.
            uint64_t drivenSerial = 0;
            // Until this local clock time the car runs on corrected native
            // physics because a locally simulated car is close.
            double contactUntil = 0.0;
            // The frame is where native physics put it: interpolation next
            // starts from there (CarPoseBuffer::Rebase).
            bool nativeShown = false;
            // How long the controller's poses take to get here, in ms (half
            // its round trip to the server and this client's).
            float transitMs = 0.0f;
            // Local clock times of the first and the latest impact of the
            // current contact a locally simulated car gave it here, and the
            // pose time it was shown at for the first.
            double impactAt = 0.0;
            double lastImpactAt = 0.0;
            double impactRenderAt = 0.0;
            MafiaNet::Time lastPacketTime        = 0;
            uint64_t poseClockSource             = 0;
            uint32_t poseClockMs                 = 0;
            uint8_t poseClockFraction            = 0;
            // The frame clock when poseClockOffset last moved.
            double lastOffsetUpdate              = 0.0;
            double poseClockOffset               = 0.0;
            std::chrono::steady_clock::time_point lastSent;
            // When the next report is due (ReportMovement's 30 Hz schedule).
            std::chrono::steady_clock::time_point nextSend;
            // The last report sent, and the last one that carried the slow
            // fields, for the idle and slow-field tests (ReportMovement).
            std::optional<Shared::Car::Movement> lastMovement;
            std::optional<Shared::Car::Movement> lastSlowMovement;
            std::chrono::steady_clock::time_point lastSlowSent;
            uint32_t sequence              = 0;
            uint32_t engineReportSequence  = 0;
            uint64_t appliedEngineRevision = 0;
            uint64_t appliedDynamicsCommandRevision = 0;
            bool controllerDynamicsInitialized = false;
            bool hasReportedEngineState    = false;
            bool lastReportedEngineOn      = false;
            bool hasAppliedEngineState     = false;
            std::chrono::steady_clock::time_point lastEngineApply;
            uint64_t appliedTerminalSequence = 0;
            uint64_t appliedTransformRevision = 0;
            bool explosionReported = false;
            bool terminalReported = false;
            bool simulationController = false;
            bool creationFailed       = false;
            Shared::Car::DamageState lastReportedDamage;
            std::deque<Shared::Car::DamageState> pendingDamageReports;
            uint32_t damageReportSequence = 0;
            bool hasReportedDamage = false;
            std::chrono::steady_clock::time_point lastDamageReport;
            uint64_t appliedDamageRevision = 0;
            uint64_t appliedRepairRevision = 0;
            std::chrono::steady_clock::time_point lastDamageApply;
            std::chrono::steady_clock::time_point lastMeshReport;
            std::chrono::steady_clock::time_point lastMeshRequest;
            bool meshDirty = false;
            bool hasReportedMesh = false;
            uint32_t meshReportSequence = 0;
            uint64_t appliedMeshRevision = 0;
            uint64_t checkpointRevision = 0;
            uint64_t checkpointSourceGuid = 0;
            uint64_t checkpointRepairRevision = 0;
            uint16_t pendingMeshChunkCount = 0;
            uint16_t pendingMeshTotal = 0;
            uint64_t pendingMeshRevision = 0;
            uint64_t pendingMeshSourceGuid = 0;
            uint64_t pendingMeshRepairRevision = 0;
            std::vector<uint8_t> pendingMeshReceived;
            std::vector<Shared::Car::MeshVertex> pendingMeshVertices;
            std::vector<Shared::Car::MeshVertex> checkpointVertices;
            std::vector<Shared::Car::MeshVertex> lastReportedMesh;
            uint64_t lastQueuedHitSequence = 0;
            uint64_t lastAppliedHitSequence = 0;
            std::deque<PendingHit> pendingHits;
            struct PendingContact {
                Shared::Car::ContactImpact impact;
                // Local clock times it arrived, and when the reporter had it.
                double arrival   = 0.0;
                double contactAt = 0.0;
            };
            uint64_t lastContactSequence = 0;
            std::deque<PendingContact> pendingContacts;
        };
        struct NativeCar {
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            bool removalQueued             = false;
            uint32_t radarColor            = 0;
            Origin origin                  = Origin::Created;
            float appliedOpacity           = 1.0f;
            uint64_t opacityDamageRevision = 0;
            bool hasAppliedOpacity         = false;
        };

        struct TraceFrame {
            // How many times this frame DriveObserver placed it.
            uint32_t ticks      = 0;
            // The car the local player sits in and this client simulates:
            // its camera is the baseline an observer's is held to.
            bool own            = false;
            unsigned int frameMs = 0;
            double wall         = 0.0;
            double frameClock   = 0.0;
            double renderTime   = 0.0;
            // The controller's physics clock the render time stands for.
            double ownerClock   = 0.0;
            float delayMs       = 0.0f;
            float leadMs        = 0.0f;
            float transitMs     = 0.0f;
            float intervalMs    = 0.0f;
            float jitterMs      = 0.0f;
            float latenessMs    = 0.0f;
            CarPoseBuffer::SampleTrace sample;
            float correctionMeters  = 0.0f;
            float correctionRadians = 0.0f;
            bool contact = false;
            bool predict = false;
            CarPoseSample presented;
            glm::vec3 written {0.0f};
            glm::quat writtenRotation {1.0f, 0.0f, 0.0f, 0.0f};
            bool afterTick = false;
            glm::vec3 afterUpdate {0.0f};
            glm::vec3 worldCenter {0.0f};
            glm::vec3 camera {0.0f};
            glm::vec3 local {0.0f};
            int localSeat = -1;
            // After C_car::Update: each wheel's flags and visible spin, and
            // the pedals, gear, revs and lights the native car holds.
            std::string wheelFlags;
            std::string wheelSpin;
            float power     = 0.0f;
            float brake     = 0.0f;
            float handbrake = 0.0f;
            int gear        = 0;
            float rpm       = 0.0f;
            uint32_t lights = 0;
        };
        void TraceObserver(uint64_t networkId, const Stream &stream, SDK::Scene::NativeFrame &frame, unsigned int frameMs, double renderTime,
                           const CarPoseSample &presented, bool contact, bool predict);

        // One pose the transform channel delivered, into the stream's buffer.
        void ReceivePose(uint64_t networkId, Stream &stream, const Shared::Entities::CarEntity::ReceivedPose &pose);
        void CreateNativeCar(World::WorldService &world, uint64_t networkId, const char *model, const Framework::Utils::TransformSnapshot &pose);
        void ReportLoadFailure(World::WorldService &world, uint64_t networkId);
        // The damage, mesh and hit state a new native car has to rebuild.
        static void ResetNativeState(Stream &stream);
        void ReportMovement(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncDynamics(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncEngine(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncTerminal(World::WorldService &world, uint64_t networkId, Stream &stream);
        void ReportDamage(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncDamage(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncRepair(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncOpacity(World::WorldService &world, uint64_t networkId);
        void ReportMesh(World::WorldService &world, uint64_t networkId, Stream &stream);
        void SyncMesh(World::WorldService &world, uint64_t networkId, Stream &stream);
        void OnMeshCheckpoint(const Shared::Car::MeshCheckpointChunk &chunk);
        void OnAuthoritativeHit(const Shared::Car::AuthoritativeHit &hit);
        void ApplyAuthoritativeHits(World::WorldService &world, uint64_t networkId, Stream &stream);
        void OnContactImpact(const Shared::Car::ContactImpact &impact);
        void ApplyContactImpacts(World::WorldService &world, uint64_t networkId, Stream &stream);
        // Records a contact between two network cars, one this client's.
        void RecordContact(uint64_t first, uint64_t second);
        void QueueRemoval(World::WorldService &world, uint64_t networkId);
        void ApplyPose(World::WorldService &world, uint64_t networkId, const Framework::Utils::TransformSnapshot &pose, bool forceSnap = false);
        void ResetNativePhysics(SDK::Car::NativeCar &actor, uint64_t networkId);
        void DetectTerminal(World::WorldService &world, uint64_t networkId, Stream &stream);
        void CaptureWaterCamera(World::WorldService &world, uint64_t networkId, SDK::Car::NativeCar &car);
        static double LocalClockMs();
        void SyncRadar(World::WorldService &world, uint64_t networkId);
        void SyncDoors(SDK::Car::NativeCar &car, const Shared::Entities::CarEntity &state);
        static void ApplySiren(SDK::Car::NativeVehicle &vehicle, const Shared::Entities::CarEntity &state);
        static uint32_t SirenLights(uint32_t lightState, bool sirenOn);

      public:
        bool AllowNativeDeactivation(World::WorldService &world, void *car) const;
        // The local driver's siren key; the server owns the siren state.
        bool ToggleSiren(World::WorldService &world);

      private:
        World::WorldService *_world = nullptr;

        std::unordered_map<uint64_t, Stream> _streams;
        std::unordered_map<uint64_t, NativeCar> _nativeById;
        std::unordered_map<void *, uint64_t> _idByNative;
        struct WaterCamera {
            Game::Entities::NativeObjectHandle player;
            uint64_t carId = 0;
            glm::vec3 position {0.0f};
            glm::vec3 direction {0.0f, 0.0f, 1.0f};
        };
        std::optional<WaterCamera> _waterCamera;
        uint64_t _updateSerial = 0;
        // This frame's World::FrameClock, read once DriveObserver first runs.
        uint64_t _clockSerial  = ~0ull;
        double _frameClock     = 0.0;
        bool _predictedSync    = false;
        // World centres and velocities of the cars this client simulated in
        // the current actor tick.
        std::vector<std::pair<glm::vec3, glm::vec3>> _simulatedCars;
        uint64_t _simulatedSerial = ~0ull;
        // Local clock times of the contacts between each pair of network
        // cars (lower id first) one of which this client simulates.
        std::map<std::pair<uint64_t, uint64_t>, std::vector<double>> _contacts;
        // Each (hit car, striker) pair's last hit, local clock ms.
        std::map<std::pair<uint64_t, uint64_t>, double> _reportedContacts;
        // When this client last applied a reported contact between two
        // network cars (lower id first), local clock ms.
        std::map<std::pair<uint64_t, uint64_t>, double> _replayedContacts;
        std::unordered_map<uint64_t, TraceFrame> _traceFrames;
    };
} // namespace Mafia1Online::Features::Car
