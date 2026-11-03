#pragma once

#include "shared/features/car/car_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/pedestrian/pedestrian_entity.h"
#include "shared/features/player/player_entity.h"

#include <networking/replication/replication_manager.h>

namespace Mafia1Online::Core {
    // Streaming and per-distance transform rates, server side; rate bands
    // throttle transforms only, reliable state is unaffected. Every band
    // keeps an entity at its full send rate for as far as the retail game
    // would show it moving, and slows it only past that:
    //   250 m  default camera far clip, which also scales the fog
    //          (reM C_mission.cpp:77-78; fog I3D_driver_render.cpp:215-219).
    //          The mod keeps it unless a script calls Camera.setRange.
    //   150 m  retail turns actors off beyond this (Game_constants.h:35-37,
    //          ai_groups.cpp:111-122).
    //   100 m  retail despawns pedestrians beyond this
    //          (C_traffic_generator.cpp:36-38) and draws people in traffic
    //          cars to 60/100/140 m by detail (C_traffic_car.cpp:210-212).
    // A human 1.8 m tall is about 15 px of a 1080p screen at 100 m and 10 px
    // at 150 m. Receivers interpolate whatever rate arrives, and their delay
    // follows each entity's own interval, so a slower band costs a later
    // pose, not a stepping one. A band's interval adds to the sender's own
    // cadence: the humans' 200 ms band behind a host sampling every 100-117
    // ms delivers poses up to ~320 ms apart, past the 250 ms
    // SnapshotBuffer::kRestartGapMs. The buffer reads a long gap as a paused
    // sender only behind a body at rest (whose unchanged pose the server
    // stops sending); behind a moving one it glides on and learns the
    // slower interval, so a band crossing never stops a walker.
    inline void ConfigureInterest(Framework::Networking::Replication::ReplicationManager &replication) {
        using Framework::Networking::Replication::SerializeRateBands;

        // Fewer RM3 messages: the poses one connection gets in a pass share
        // MTU-sized packets.
        replication.SetTransformBatchingEnabled(true);
        // Hysteresis at the streaming edge and look-ahead along the viewer's
        // motion, so a car at 30 m/s meets traffic already streamed in.
        replication.SetInterestStreamOutMargin(20.0F);
        replication.SetInterestLookaheadSeconds(1.0F);
        replication.SetInterestRebuildInterval(100);

        // Players send at 20 Hz. Full rate to 150 m, where retail switches
        // actors off; 10 Hz out to the far clip; 5 Hz beyond it, where they
        // are drawn only as map blips. Always visible so everyone's map
        // keeps them, and banded anyway.
        SerializeRateBands players {150.0F, 250.0F, 100, 200};
        players.appliesToAlwaysVisible = true;
        replication.SetSerializeRateBands(Shared::Entities::PlayerEntity::kTypeName, players);

        // Cars send at 30 Hz (a parked one sends nothing: its bytes do not
        // change). Driving cars stay at full rate as far as the far clip
        // shows them; 15 Hz to 400 m and 7.5 Hz beyond, past anything the
        // default camera draws. Traffic streams to 260 m, so this only ever
        // slows always-visible player and script cars.
        SerializeRateBands cars {250.0F, 400.0F, 66, 133};
        cars.appliesToAlwaysVisible = true;
        replication.SetSerializeRateBands(Shared::Entities::CarEntity::kTypeName, cars);

        // Walkers and NPCs send at 10 Hz and stream to 150 m. Full rate to
        // 100 m, as far as retail keeps a pedestrian; 5 Hz from there to the
        // stream edge, at 10-15 px tall.
        const SerializeRateBands humans {100.0F, 100.0F, 200, 200};
        replication.SetSerializeRateBands(Shared::Entities::PedestrianEntity::kTypeName, humans);
        replication.SetSerializeRateBands(Shared::Entities::NpcEntity::kTypeName, humans);

        // Safety caps above any density the host produces (32 walkers a
        // snapshot, retail's 50-90 m generator ring), so a pile-up of
        // spawned bodies cannot flood one viewer. Owned and always-visible
        // entities are never counted, and the simulation host is exempt.
        replication.SetInterestBudget(Shared::Entities::PedestrianEntity::kTypeName, 96);
        replication.SetInterestBudget(Shared::Entities::CarEntity::kTypeName, 96);
    }
} // namespace Mafia1Online::Core
