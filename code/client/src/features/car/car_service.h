#pragma once

#include "game/entities/native_object_registry.h"
#include "shared/features/car/car_damage_state.h"
#include "shared/features/car/car_hit_report.h"
#include "shared/features/car/car_mesh_checkpoint.h"

#include <utils/snapshot_buffer.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace Mafia1Online::SDK::Scene {
    struct NativeFrame;
}
namespace Mafia1Online::SDK::Car {
    struct NativeCar;
    struct NativeVehicle;
}
namespace Mafia1Online::Shared::Entities {
    class CarEntity;
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
        void OnNativeDeformed(SDK::Car::NativeCar *car);
        // C_car::AI detour. An observer car skips native physics: the
        // interpolated pose is written inside the actor tick, before the
        // camera reads it, and only wheels and engine sound are refreshed.
        using NativeStep = void (*)(SDK::Car::NativeCar *, unsigned int);
        bool DriveObserver(SDK::Car::NativeCar *car, unsigned int frameMs, NativeStep native);
        // Switches observers between interpolation of the replicated path
        // and native physics corrected toward the pose predicted to now.
        bool ToggleSyncMode();
        void OnSimulated(SDK::Car::NativeCar *car, unsigned int frameMs);

      private:
        struct Stream {
            struct PendingHit {
                Shared::Car::AuthoritativeHit hit;
                std::chrono::steady_clock::time_point received;
            };
            struct PoseSample {
                double time = 0.0;
                glm::vec3 position {0.0f};
                glm::vec3 velocity {0.0f};
                glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
                glm::vec3 angularVelocity {0.0f};
            };
            // Replicated poses dated on the local clock from the controller's
            // physics clock, rendered delayMs behind by Hermite interpolation.
            std::deque<PoseSample> poses;
            float delayMs      = 100.0f;
            float latenessPeak = 0.0f;
            float intervalMs   = 33.0f;
            double lastPoseTime = 0.0;
            std::deque<double> offsetWindow;
            uint32_t simClockMs = 0;
            // The update serial of the last actor tick DriveObserver placed.
            uint64_t drivenSerial = 0;
            // Until this local clock time the car runs on corrected native
            // physics because a locally simulated car is close.
            double contactUntil = 0.0;
            MafiaNet::Time lastPacketTime        = 0;
            uint64_t poseClockSource             = 0;
            uint32_t poseClockMs                 = 0;
            double poseClockOffset               = 0.0;
            std::chrono::steady_clock::time_point lastSent;
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
            uint16_t pendingMeshChunkCount = 0;
            uint16_t pendingMeshTotal = 0;
            uint64_t pendingMeshRevision = 0;
            uint64_t pendingMeshSourceGuid = 0;
            std::vector<uint8_t> pendingMeshReceived;
            std::vector<Shared::Car::MeshVertex> pendingMeshVertices;
            std::vector<Shared::Car::MeshVertex> checkpointVertices;
            std::vector<Shared::Car::MeshVertex> lastReportedMesh;
            uint64_t lastQueuedHitSequence = 0;
            uint64_t lastAppliedHitSequence = 0;
            std::deque<PendingHit> pendingHits;
        };
        struct NativeCar {
            Game::Entities::NativeObjectHandle handle;
            SDK::Scene::NativeFrame *frame = nullptr;
            bool removalQueued             = false;
            uint32_t radarColor            = 0;
            float appliedOpacity           = 1.0f;
            uint64_t opacityDamageRevision = 0;
            bool hasAppliedOpacity         = false;
        };

        void CreateNativeCar(World::WorldService &world, uint64_t networkId, const char *model, const Framework::Utils::TransformSnapshot &pose);
        void ReportLoadFailure(World::WorldService &world, uint64_t networkId);
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
        void QueueRemoval(World::WorldService &world, uint64_t networkId);
        void ApplyPose(World::WorldService &world, uint64_t networkId, const Framework::Utils::TransformSnapshot &pose, bool forceSnap = false);
        void ResetNativePhysics(SDK::Car::NativeCar &actor, uint64_t networkId);
        void DetectTerminal(World::WorldService &world, uint64_t networkId, Stream &stream);
        static bool SamplePose(const Stream &stream, double time, Stream::PoseSample &out);
        static void PushPose(Stream &stream, const Stream::PoseSample &pose, double arrival, bool reset);
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

        std::unordered_map<uint64_t, Stream> _streams;
        std::unordered_map<uint64_t, NativeCar> _nativeById;
        std::unordered_map<void *, uint64_t> _idByNative;
        // One render clock per frame for every observer car.
        uint64_t _updateSerial = 0;
        uint64_t _clockSerial  = ~0ull;
        double _frameClock     = 0.0;
        bool _predictedSync    = false;
        // World centres and velocities of the cars this client simulated in
        // the current actor tick.
        std::vector<std::pair<glm::vec3, glm::vec3>> _simulatedCars;
        uint64_t _simulatedSerial = ~0ull;
    };
} // namespace Mafia1Online::Features::Car
