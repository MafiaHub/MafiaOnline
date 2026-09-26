#pragma once

#include <scripting/builtins/player.h>
#include <scripting/builtins/quaternion.h>
#include <scripting/builtins/vector3.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Mafia1Online::Shared::Entities {
    class PlayerEntity;
}

namespace Mafia1Online::Scripting {
    // Server scripting handle for a connected player. The player replica is
    // server owned, so every setter goes through the server services that
    // validate and replicate it; the framework's owner-routed connection
    // methods are replaced by versions that address the controlling client.
    class Player final: public Framework::Scripting::Builtins::Player {
      public:
        Player(uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

        Shared::Entities::PlayerEntity *ResolvePlayer() const;

        std::string GetNickname() const;
        std::string GetModel() const;
        double GetHealth() const;
        double GetMoney() const;
        bool IsAlive() const;
        bool IsSpawned() const;
        double GetMissionGeneration() const;
        double GetSpawnGeneration() const;
        Framework::Scripting::Builtins::Vector3 GetPlayerPosition() const;
        Framework::Scripting::Builtins::Quaternion GetPlayerRotation() const;
        int32_t GetCurrentWeapon() const;
        bool HasWeapon(uint32_t weaponId) const;
        bool SetHealth(double health);
        bool Despawn();
        bool RemoveWeapon(uint32_t weaponId);
        bool RemoveAllWeapons();
        bool SetCurrentWeapon(uint32_t weaponId);
        bool SetWeaponAmmo(uint32_t weaponId, uint32_t loaded, uint32_t reserve);
        bool GiveAmmo(uint32_t weaponId, int32_t amount);
        void KickPlayer(const std::string &reason);
        void EmitToClient(const std::string &eventName, const std::string &payloadJson);
        int GetConnectionPing() const;
        std::string GetConnectionAddress() const;
        std::string GetConnectionSteamId() const;
        std::string GetConnectionDiscordId() const;
        std::string GetConnectionHardwareId() const;
        void SetNameVisible(bool visible);
        void SetHealthBarVisible(bool visible);
        void SetLabel(const std::string &text);
        void SetLabelColor(uint32_t color);
        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);

      private:
        uint64_t ControllerGuid() const;
        static std::unique_ptr<v8pp::class_<Player>> _class;
    };
} // namespace Mafia1Online::Scripting
