#include "pickup_scripting.h"

#include "script_runtime.h"

#include "shared/features/pickup/weapon_pickup_entity.h"
#include "shared/scripting_catalog.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <stdexcept>

namespace Mafia1Online::Scripting {
    using Shared::Entities::WeaponPickupEntity;
    using v8pp::metadata::docs;
    using v8pp::metadata::param;
    using v8pp::metadata::property_docs;

    std::unique_ptr<v8pp::class_<Pickup>> Pickup::_class;

    Pickup::Pickup(uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {
        if (!ResolvePickup()) {
            throw std::runtime_error("Pickup handle does not refer to a streamed weapon pickup");
        }
    }

    v8::Local<v8::Value> WrapPickup(v8::Isolate *isolate, uint64_t networkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (networkId == 0 || !replication || !replication->GetEntity<WeaponPickupEntity>(networkId)) {
            return v8::Null(isolate);
        }
        Pickup::GetClass(isolate);
        return v8pp::class_<Pickup>::create_object(isolate, networkId);
    }

    WeaponPickupEntity *Pickup::ResolvePickup() const {
        return dynamic_cast<WeaponPickupEntity *>(Resolve());
    }

    Framework::Scripting::Builtins::Vector3 Pickup::GetPickupPosition() const {
        const auto *pickup = ResolvePickup();
        return pickup ? Framework::Scripting::Builtins::Vector3(pickup->position.x, pickup->position.y, pickup->position.z)
                      : Framework::Scripting::Builtins::Vector3();
    }

    Framework::Scripting::Builtins::Quaternion Pickup::GetPickupRotation() const {
        return Framework::Scripting::Builtins::Quaternion(glm::angleAxis(static_cast<float>(GetHeading()), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    uint32_t Pickup::GetWeaponId() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->weaponId : 0;
    }

    double Pickup::GetHeading() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->yaw : 0.0;
    }

    uint32_t Pickup::GetLoaded() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->loaded : 0;
    }

    uint32_t Pickup::GetReserve() const {
        const auto *pickup = ResolvePickup();
        return pickup ? pickup->reserve : 0;
    }

    double Pickup::GetMissionGeneration() const {
        const auto *pickup = ResolvePickup();
        return pickup ? static_cast<double>(pickup->missionGeneration) : 0.0;
    }

    std::string Pickup::ToString() const {
        const auto *pickup = ResolvePickup();
        return pickup ? "Pickup(" + std::to_string(GetId()) + ", weapon " + std::to_string(pickup->weaponId) + ")"
                      : "Pickup(" + std::to_string(GetId()) + ", gone)";
    }

    v8pp::class_<Pickup> &Pickup::GetClass(v8::Isolate *isolate) {
        if (_class) {
            return *_class;
        }
        Framework::Scripting::Builtins::Entity::GetClass(isolate);
        _class = std::make_unique<v8pp::class_<Pickup>>(isolate, ClientCatalog(), "Pickup",
            "A weapon pickup streamed from the server. Script-created pickups and weapons dropped by players use the same handle; values are the latest replicated state.");
        auto &cls = *_class;
        cls.auto_wrap_objects(true);
        cls.inherit<Framework::Scripting::Builtins::Entity>();
        cls.ctor<uint64_t>(docs("void", {param("id", "number", false, "Network entity identifier.")}, "Creates a handle for a streamed weapon pickup; throws when the ID does not resolve to a pickup."));
        cls.function("toString", &Pickup::ToString, docs("string", {}, "Formats this pickup for logging.", "The pickup ID and weapon, or gone when it has streamed out."));
        cls.property("position", &Pickup::GetPickupPosition, property_docs("Vector3", "Replicated pickup position. Read only on the client."));
        cls.property("rotation", &Pickup::GetPickupRotation, property_docs("Quaternion", "Facing derived from heading. Read only on the client."));
        cls.property("weaponId", &Pickup::GetWeaponId, property_docs("number", "Weapon lying here."));
        cls.property("heading", &Pickup::GetHeading, property_docs("number", "Facing in radians."));
        cls.property("loaded", &Pickup::GetLoaded, property_docs("number", "Loaded rounds or throwable count."));
        cls.property("reserve", &Pickup::GetReserve, property_docs("number", "Reserve rounds."));
        cls.property("missionGeneration", &Pickup::GetMissionGeneration, property_docs("number", "Server generation of the mission containing this pickup."));
        return cls;
    }

    void Pickup::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }
} // namespace Mafia1Online::Scripting
