#pragma once

#include <networking/replication/network_entity.h>

#include <mafianet/string.h>

#include <cstdint>
#include <string>

namespace Mafia1Online::Shared::Entities {
    // The server's selected world. The generation distinguishes successive
    // loads of the same mission, including a script-requested reset.
    class MissionEntity final: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "Mafia1Online::Mission";

        std::string mission;
        uint64_t generation = 1;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            MafiaNet::RakString name(mission.c_str());
            fields.Field(name);
            fields.Field(generation);
            if (!fields.Writing()) {
                mission = name.C_String();
            }
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(mission);
            fields.Field(generation);
        }
    };
} // namespace Mafia1Online::Shared::Entities
