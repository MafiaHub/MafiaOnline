#include "register_entities.h"

#include "features/car/car_entity.h"
#include "features/door/door_entity.h"
#include "features/car/car_debris_entity.h"
#include "features/pickup/weapon_pickup_entity.h"
#include "features/player/player_entity.h"
#include "features/sound/sound_entity.h"
#include "features/world/mission_entity.h"
#include "features/world/world_state_entity.h"

#include <networking/replication/entity_registry.h>

namespace Mafia1Online::Shared::Entities {
    void RegisterEntities() {
        auto &registry = Framework::Networking::Replication::EntityRegistry::Get();
        registry.Register<PlayerEntity>(PlayerEntity::kTypeName);
        registry.Register<CarEntity>(CarEntity::kTypeName);
        registry.Register<CarDebrisEntity>(CarDebrisEntity::kTypeName);
        registry.Register<MissionEntity>(MissionEntity::kTypeName);
        registry.Register<WeaponPickupEntity>(WeaponPickupEntity::kTypeName);
        registry.Register<WorldStateEntity>(WorldStateEntity::kTypeName);
        registry.Register<DoorEntity>(DoorEntity::kTypeName);
        registry.Register<SoundEntity>(SoundEntity::kTypeName);
    }
} // namespace Mafia1Online::Shared::Entities
