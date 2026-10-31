#pragma once

#include <mafia1/sdk/ui/map_projection.h>

namespace Mafia1Online::Features::World {
    class WorldService;
}

namespace Mafia1Online::Features::CityMap {
    // The held-TAB city map on the countryside maps: replaces retail
    // 0mapar.bmp with the MafiaCountrysideMap 2.0 image (city and
    // countryside, maps/fullmap.bmp from a server asset ZIP) and points
    // DrawAll's projection at it. Every other mission, and a session without
    // the image, keeps the retail map. See docs/native_contract.md, "City map
    // countryside extension".
    class CityMapService final {
      public:
        bool Install(World::WorldService &world);
        void Uninstall();
    };

    // The projection DrawAll uses for the map texture it holds now; script
    // blips are placed with it.
    const SDK::UI::Map::Projection &ActiveProjection();
} // namespace Mafia1Online::Features::CityMap
