#include "native_object_registry.h"

#include <cassert>

namespace Mafia1Online::Game::Entities {
    NativeObjectHandle NativeObjectRegistry::Bind(void *object, uint64_t networkId) {
        assert(object && networkId);
        InvalidateNative(object);
        InvalidateNetwork(networkId);

        const uint64_t generation = _nextGeneration++;
        _byNative.emplace(object, Entry {networkId, generation});
        _byNetwork.emplace(networkId, object);
        return {object, networkId, generation};
    }

    void *NativeObjectRegistry::Resolve(const NativeObjectHandle &handle) const {
        const auto it = _byNative.find(handle.object);
        if (it == _byNative.end() || it->second.networkId != handle.networkId || it->second.generation != handle.generation) {
            return nullptr;
        }
        return handle.object;
    }

    NativeObjectHandle NativeObjectRegistry::FindByNative(void *object) const {
        const auto it = _byNative.find(object);
        return it == _byNative.end() ? NativeObjectHandle {} : NativeObjectHandle {object, it->second.networkId, it->second.generation};
    }

    NativeObjectHandle NativeObjectRegistry::FindByNetwork(uint64_t networkId) const {
        const auto it = _byNetwork.find(networkId);
        return it == _byNetwork.end() ? NativeObjectHandle {} : FindByNative(it->second);
    }

    void NativeObjectRegistry::InvalidateNative(void *object) {
        const auto it = _byNative.find(object);
        if (it == _byNative.end()) {
            return;
        }
        _byNetwork.erase(it->second.networkId);
        _byNative.erase(it);
    }

    void NativeObjectRegistry::InvalidateNetwork(uint64_t networkId) {
        const auto it = _byNetwork.find(networkId);
        if (it != _byNetwork.end()) {
            InvalidateNative(it->second);
        }
    }

    void NativeObjectRegistry::Clear() {
        _byNative.clear();
        _byNetwork.clear();
    }
} // namespace Mafia1Online::Game::Entities
