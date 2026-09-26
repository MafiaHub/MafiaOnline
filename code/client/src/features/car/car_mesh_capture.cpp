#include "car_mesh_capture.h"
#include "shared/features/car/car_damage_state.h"

#include <mafia1/sdk/car/native_car.h>
#include <mafia1/sdk/car/native_vehicle_state.h>

#include <cstring>
#include <vector>

namespace Mafia1Online::Features::Car {
    namespace {
        constexpr size_t kMaxNativeVehicleStateBytes = 128 * 1024;

        template <typename T> bool Read(const std::vector<std::byte> &bytes, size_t offset, T &out) {
            if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
                return false;
            }
            std::memcpy(&out, bytes.data() + offset, sizeof(T));
            return true;
        }
    } // namespace

    bool CaptureNativeMesh(SDK::Car::NativeCar &car, std::vector<Shared::Car::MeshVertex> &out) {
        out.clear();
        auto &vehicle = car.Vehicle();
        const int nativeSize = vehicle.VehicleStateSize();
        if (nativeSize < static_cast<int>(sizeof(SDK::Car::NativeVehicleStateHeader)) ||
            static_cast<size_t>(nativeSize) > kMaxNativeVehicleStateBytes) {
            return false;
        }
        std::vector<std::byte> bytes(static_cast<size_t>(nativeSize));
        if (!vehicle.SaveVehicleState(bytes.data())) {
            return false;
        }
        SDK::Car::NativeVehicleStateHeader header {};
        if (!Read(bytes, 0, header) || header.version != 9 || header.totalSize != bytes.size() ||
            header.lightCount < 0 || header.zoneCount < 0 ||
            static_cast<size_t>(header.lightCount) != vehicle.Lights().Size() ||
            static_cast<size_t>(header.zoneCount) != vehicle.DeformZones().Size() ||
            header.zoneCount > Shared::Car::DamageState::kMaxZones) {
            return false;
        }
        size_t offset = sizeof(header) + static_cast<size_t>(header.lightCount) * sizeof(SDK::Car::NativeVehicleStateLight);
        if (offset > bytes.size()) {
            return false;
        }
        for (int zoneIndex = 0; zoneIndex < header.zoneCount; ++zoneIndex) {
            SDK::Car::NativeVehicleStateZone zone {};
            if (!Read(bytes, offset, zone) || zone.primaryCount < 0 || zone.secondaryCount < 0 ||
                static_cast<size_t>(zone.primaryCount) + static_cast<size_t>(zone.secondaryCount) >
                    Shared::Car::kMaxMeshVertices - out.size()) {
                out.clear();
                return false;
            }
            offset += sizeof(zone);
            for (uint8_t lod = 0; lod < 2; ++lod) {
                const int count = lod == 0 ? zone.primaryCount : zone.secondaryCount;
                for (int i = 0; i < count; ++i) {
                    SDK::Car::NativeVehicleStateVertex vertex {};
                    if (!Read(bytes, offset, vertex)) {
                        out.clear();
                        return false;
                    }
                    out.push_back({static_cast<uint8_t>(zoneIndex), lod, vertex.displacement});
                    offset += sizeof(vertex);
                }
            }
        }
        return true;
    }
} // namespace Mafia1Online::Features::Car
