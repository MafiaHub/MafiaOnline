#pragma once

#include <cstdint>
#include <string_view>

namespace Mafia1Online::Features::Traffic {
    // Simulation host: whether a car retail's police or mafia code made (a
    // road block's or a reinforcement's police car, a mafia pursuit car, a
    // police car back in traffic) may leave the road now, and how. Every
    // reference retail keeps to such a car or its crew is either released by
    // C_shvestky::TickDoRiciActors' own retirement (crime records' officer
    // lists, the radar, C_sledovacka's hunts, C_game's actor lists, the
    // police pool, the police board for a pool car) or listed here and
    // waited out; see docs/npc_multiplayer_audit.md, "Retiring police,
    // mafia and road block cars".
    struct CrewCarFacts {
        // g_TrafficCarPursuitEntries (0x63d818) holds it, or a crew member
        // of it: a mafia crew still hunts the player who rammed it.
        bool pursuit = false;
        // An active road block (C_game::m_RoadBlocks, m_bActive) lists it or
        // a crew member; the block hands its members to the retirement list
        // itself once the wanted player is gone.
        bool activeRoadBlock = false;
        // The police board (g_PoliceNastenka 0x646d10) has an action with a
        // crew member among its officers, or with this car as its car.
        bool boardCase = false;
        // A crime record names it (driven car, recorded vehicle, the police
        // car a player rammed), or a record above level 0 lists a crew
        // member as one of its officers (m_Nasrany).
        bool crimeRecord = false;
        // A crew officer the police recruited whose case is not over: the
        // retirement pass keeps it (and so the car it sits in) until then.
        bool recruitedOnDuty = false;
        // A seat holds someone the host does not run as an ordinary NPC: a
        // player's replica, or a scripted NPC (scripts own those).
        bool foreignOccupant = false;
        // A traffic element holds it: an active police car back in traffic
        // (a dynamic element) leaves the road with C_traffic_car::
        // DeactivateElem; any other element (one waiting in gSpawnedCars)
        // is not ours to break up.
        bool dynamicElement = false;
        bool otherElement   = false;
    };

    enum class CrewCarVerdict : uint8_t {
        // Into C_shvestky's retirement list with its crew, retired by the
        // next native pass.
        Retire,
        // C_traffic_car::DeactivateElem on its slot; retail frees or lists
        // it for retirement itself.
        Deactivate,
        KeepPursuit,
        KeepRoadBlock,
        KeepCase,
        KeepOccupant,
        KeepElement,
    };

    // A case on a player comes first: nothing goes while one still runs
    // through the car or its crew.
    constexpr CrewCarVerdict JudgeCrewCar(const CrewCarFacts &facts) {
        if (facts.pursuit) {
            return CrewCarVerdict::KeepPursuit;
        }
        if (facts.activeRoadBlock) {
            return CrewCarVerdict::KeepRoadBlock;
        }
        if (facts.boardCase || facts.crimeRecord || facts.recruitedOnDuty) {
            return CrewCarVerdict::KeepCase;
        }
        if (facts.foreignOccupant) {
            return CrewCarVerdict::KeepOccupant;
        }
        if (facts.otherElement) {
            return CrewCarVerdict::KeepElement;
        }
        return facts.dynamicElement ? CrewCarVerdict::Deactivate : CrewCarVerdict::Retire;
    }

    constexpr std::string_view CrewCarVerdictName(CrewCarVerdict verdict) {
        switch (verdict) {
        case CrewCarVerdict::Retire: return "retire";
        case CrewCarVerdict::Deactivate: return "deactivate its traffic element";
        case CrewCarVerdict::KeepPursuit: return "kept: a mafia pursuit runs";
        case CrewCarVerdict::KeepRoadBlock: return "kept: its road block is up";
        case CrewCarVerdict::KeepCase: return "kept: a police case runs through it";
        case CrewCarVerdict::KeepOccupant: return "kept: a player or scripted NPC sits in it";
        case CrewCarVerdict::KeepElement: return "kept: a waiting traffic element holds it";
        }
        return "kept";
    }
} // namespace Mafia1Online::Features::Traffic
