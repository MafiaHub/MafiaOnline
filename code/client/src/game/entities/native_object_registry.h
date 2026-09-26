#pragma once

#include <cstdint>
#include <unordered_map>

namespace Mafia1Online::Game::Entities {
    struct NativeObjectHandle {
        void *object        = nullptr;
        uint64_t networkId  = 0;
        uint64_t generation = 0;
    };

    // Main-thread only. The generation prevents a reused native address from
    // resolving an old replica after destruction, stream out or mission close.
    class NativeObjectRegistry final {
      public:
        NativeObjectHandle Bind(void *object, uint64_t networkId);
        void *Resolve(const NativeObjectHandle &handle) const;
        NativeObjectHandle FindByNative(void *object) const;
        NativeObjectHandle FindByNetwork(uint64_t networkId) const;
        void InvalidateNative(void *object);
        void InvalidateNetwork(uint64_t networkId);
        void Clear();

      private:
        struct Entry {
            uint64_t networkId;
            uint64_t generation;
        };

        std::unordered_map<void *, Entry> _byNative;
        std::unordered_map<uint64_t, void *> _byNetwork;
        uint64_t _nextGeneration = 1;
    };
} // namespace Mafia1Online::Game::Entities
