#include <utils/safe_win32.h>

#include "map_scripting.h"

#include "player_scripting.h"
#include "script_runtime.h"
#include "visual_scripting.h"
#include "features/world/world_service.h"
#include "shared/scripting_catalog.h"

#include <mafia1/sdk/graphics/native_graph.h>
#include <mafia1/sdk/ui/native_hud.h>
#include <mafia1/sdk/ui/native_map.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/resource/resource_manager.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace Mafia1Online::Scripting {
    namespace {
        using v8pp::metadata::docs;
        using v8pp::metadata::param;
        namespace Map = SDK::UI::Map;

        constexpr size_t kMaxBlips       = 256;
        constexpr size_t kMaxIdLength    = 64;
        constexpr size_t kMaxLabelLength = 40;
        constexpr float kDefaultSize     = 5.0f;
        constexpr float kMinSize         = 2.0f;
        constexpr float kMaxSize         = 16.0f;
        constexpr float kLabelHeight     = 11.0f;
        // Edge arrows sit just inside the map border.
        constexpr float kEdgeInset = 0.94f;
        constexpr uint32_t kOutline = 0xd0101010u;

        enum class Shape { Dot, Square, Diamond, Triangle };

        struct Blip {
            std::string owner;
            std::string id;
            glm::vec3 position {0.0f};
            uint64_t entity = 0;
            uint32_t rgb    = 0xffffff;
            std::string label;
            Shape shape = Shape::Dot;
            float size  = kDefaultSize;
            bool edge   = false;
            bool flash  = false;
        };
        std::vector<Blip> gBlips;

        bool ReadOption(v8::Isolate *isolate, v8::Local<v8::Value> options, const char *name, v8::Local<v8::Value> &out) {
            if (options.IsEmpty() || options->IsUndefined() || options->IsNull()) return false;
            return options.As<v8::Object>()->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, name)).ToLocal(&out) && !out->IsUndefined();
        }

        bool ParseShape(const std::string &name, Shape &out) {
            if (name == "dot") out = Shape::Dot;
            else if (name == "square") out = Shape::Square;
            else if (name == "diamond") out = Shape::Diamond;
            else if (name == "triangle") out = Shape::Triangle;
            else return false;
            return true;
        }

        bool ReadOptions(v8::Isolate *isolate, v8::Local<v8::Value> options, Blip &blip) {
            if (!options.IsEmpty() && !options->IsUndefined() && !options->IsNull() && !options->IsObject()) return false;
            v8::Local<v8::Value> value;
            if (ReadOption(isolate, options, "color", value)) {
                if (!value->IsUint32() || value.As<v8::Uint32>()->Value() > 0xffffff) return false;
                blip.rgb = value.As<v8::Uint32>()->Value();
            }
            if (ReadOption(isolate, options, "label", value)) {
                if (!value->IsString()) return false;
                auto label = v8pp::from_v8<std::string>(isolate, value);
                if (label.size() > kMaxLabelLength) label.resize(kMaxLabelLength);
                blip.label = ToNativeText(label);
            }
            if (ReadOption(isolate, options, "icon", value)) {
                if (!value->IsString() || !ParseShape(v8pp::from_v8<std::string>(isolate, value), blip.shape)) return false;
            }
            if (!Args::OptionalFloat(isolate, options, "size", kMinSize, kMaxSize, blip.size)) return false;
            for (auto [name, flag] : {std::pair {"edge", &blip.edge}, std::pair {"flash", &blip.flash}}) {
                if (!ReadOption(isolate, options, name, value)) continue;
                if (!value->IsBoolean()) return false;
                *flag = value.As<v8::Boolean>()->Value();
            }
            return true;
        }

        uint32_t Argb(uint32_t rgb, float alpha) {
            return (static_cast<uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f) << 24) | rgb;
        }

        SDK::Graphics::NativeTLVertex Vertex(float x, float y, uint32_t color) {
            return {x, y, 0.0f, 1.0f, color, 0, 0.0f, 0.0f};
        }

        void DrawTriangles(const SDK::Graphics::NativeTLVertex *vertices, uint32_t count) {
            auto *graph = SDK::Graphics::GetGraph();
            graph->vtable->setState(graph, SDK::Graphics::kAlphaState, SDK::Graphics::kAlphaBlend);
            graph->vtable->setTexture(graph, nullptr);
            graph->vtable->drawPrimitiveList(graph, 3, count, const_cast<SDK::Graphics::NativeTLVertex *>(vertices), 1);
        }

        void DrawShape(Shape shape, float x, float y, float radius, uint32_t color) {
            switch (shape) {
            case Shape::Dot: {
                constexpr uint32_t segments = 16;
                std::array<SDK::Graphics::NativeTLVertex, segments * 3> vertices;
                for (uint32_t index = 0; index < segments; ++index) {
                    const float a = static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> / segments;
                    const float b = static_cast<float>(index + 1) * 2.0f * std::numbers::pi_v<float> / segments;
                    vertices[index * 3]     = Vertex(x, y, color);
                    vertices[index * 3 + 1] = Vertex(x + std::cos(a) * radius, y + std::sin(a) * radius, color);
                    vertices[index * 3 + 2] = Vertex(x + std::cos(b) * radius, y + std::sin(b) * radius, color);
                }
                DrawTriangles(vertices.data(), segments);
                break;
            }
            case Shape::Square: SDK::Graphics::FillRect(x - radius, y - radius, radius * 2.0f, radius * 2.0f, color); break;
            case Shape::Diamond: {
                const float r = radius * 1.3f;
                const SDK::Graphics::NativeTLVertex vertices[6] {Vertex(x, y - r, color), Vertex(x + r, y, color), Vertex(x - r, y, color),
                                                                  Vertex(x - r, y, color), Vertex(x + r, y, color), Vertex(x, y + r, color)};
                DrawTriangles(vertices, 2);
                break;
            }
            case Shape::Triangle: {
                const float r = radius * 1.3f;
                const SDK::Graphics::NativeTLVertex vertices[3] {Vertex(x, y - r, color), Vertex(x + r, y + r * 0.8f, color), Vertex(x - r, y + r * 0.8f, color)};
                DrawTriangles(vertices, 1);
                break;
            }
            }
        }

        // A wedge at the map border pointing toward a blip outside the window.
        void DrawEdgeArrow(float x, float y, float dx, float dy, float radius, uint32_t fill) {
            const float length = std::hypot(dx, dy);
            if (length < 0.001f) return;
            dx /= length;
            dy /= length;
            const auto wedge = [&](float r, uint32_t color) {
                const SDK::Graphics::NativeTLVertex vertices[3] {Vertex(x + dx * r * 1.6f, y + dy * r * 1.6f, color),
                                                                  Vertex(x - dx * r * 0.6f - dy * r, y - dy * r * 0.6f + dx * r, color),
                                                                  Vertex(x - dx * r * 0.6f + dy * r, y - dy * r * 0.6f - dx * r, color)};
                DrawTriangles(vertices, 1);
            };
            wedge(radius + 1.5f, kOutline);
            wedge(radius, fill);
        }

        bool Resolve(const Blip &blip, glm::vec3 &out) {
            if (blip.entity == 0) {
                out = blip.position;
                return true;
            }
            auto *replication = Framework::CoreModules::GetReplication();
            auto *entity      = replication ? replication->GetEntityByNetworkID(blip.entity) : nullptr;
            if (!entity) return false;
            out = entity->position;
            return true;
        }

        std::vector<Blip>::iterator Find(const std::string &owner, const std::string &id) {
            return std::find_if(gBlips.begin(), gBlips.end(), [&](const Blip &blip) { return blip.owner == owner && blip.id == id; });
        }

        bool ReadId(const v8::FunctionCallbackInfo<v8::Value> &info, std::string &out) {
            if (info.Length() < 1 || !info[0]->IsString()) return false;
            out = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            return !out.empty() && out.size() <= kMaxIdLength;
        }

        void JS_SetBlip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            constexpr const char *usage = "CityMap.setBlip(id, target, options?) expects an id of 1 to 64 characters, a position, Player or Vehicle "
                                          "and { color?: 0xRRGGBB, label?: string, icon?: 'dot' | 'square' | 'diamond' | 'triangle', size?: number, edge?: boolean, flash?: boolean }";
            Blip blip;
            if (info.Length() < 2 || info.Length() > 3 || !ReadId(info, blip.id)) {
                Args::Throw(isolate, usage);
                return;
            }
            if (auto *player = v8pp::class_<Player>::unwrap_object(isolate, info[1])) {
                blip.entity = player->GetId();
            }
            else if (auto *vehicle = v8pp::class_<Vehicle>::unwrap_object(isolate, info[1])) {
                blip.entity = vehicle->GetId();
            }
            else if (!Args::ReadPosition(isolate, info[1], blip.position)) {
                Args::Throw(isolate, usage);
                return;
            }
            if (!ReadOptions(isolate, info.Length() == 3 ? info[2] : v8::Local<v8::Value>(), blip)) {
                Args::Throw(isolate, usage);
                return;
            }
            blip.owner = ScriptOwner(isolate);
            if (blip.owner.empty()) {
                info.GetReturnValue().Set(false);
                return;
            }
            if (auto existing = Find(blip.owner, blip.id); existing != gBlips.end()) {
                *existing = std::move(blip);
            }
            else if (gBlips.size() >= kMaxBlips) {
                info.GetReturnValue().Set(false);
                return;
            }
            else {
                gBlips.push_back(std::move(blip));
            }
            info.GetReturnValue().Set(true);
        }

        void JS_RemoveBlip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::string id;
            if (info.Length() != 1 || !ReadId(info, id)) {
                Args::Throw(info.GetIsolate(), "CityMap.removeBlip(id) expects an id of 1 to 64 characters");
                return;
            }
            const auto existing = Find(ScriptOwner(info.GetIsolate()), id);
            const bool found    = existing != gBlips.end();
            if (found) gBlips.erase(existing);
            info.GetReturnValue().Set(found);
        }

        void JS_ClearBlips(const v8::FunctionCallbackInfo<v8::Value> &info) {
            const auto owner = ScriptOwner(info.GetIsolate());
            std::erase_if(gBlips, [&owner](const Blip &blip) { return blip.owner == owner; });
        }

        void JS_IsOpen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(GetWorld().IsReady() && Map::IsShown());
        }

        void CleanupResource(const std::string &owner) {
            std::erase_if(gBlips, [&owner](const Blip &blip) { return blip.owner == owner; });
        }
    } // namespace

    void ClientCityMap::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        const auto target = "Vector3 | { x: number; y: number; z: number } | Player | Vehicle";
        v8pp::module map(isolate, ClientCatalog(), "CityMap",
            "Blips on the retail city map the player holds TAB for. Each resource has its own blips; they are drawn over the map in the game's own "
            "projection and cleared when the resource stops or the mission unloads. Nothing here is visible to other players.");
        map.function("setBlip", &JS_SetBlip,
            docs("boolean",
                 {param("id", "string", false, "This resource's name for the blip, 1 to 64 characters; setting it again replaces it."),
                  param("target", target, false, "A fixed point, or a streamed player or vehicle the blip follows; an entity blip hides while it is not streamed."),
                  param("options", "{ color?: number; label?: string; icon?: \"dot\" | \"square\" | \"diamond\" | \"triangle\"; size?: number; edge?: boolean; flash?: boolean }", true,
                        "0xRRGGBB color (white), a label of at most 40 characters drawn beside it, the icon (dot), its radius in 600-line pixels from 2 to 16 (5), "
                        "whether a blip outside the shown part of the map becomes an arrow at the map border (false), and whether it pulses (false).")},
                 "Adds or replaces a blip.", "False when no resource is running the call or 256 blips are already set."));
        map.function("removeBlip", &JS_RemoveBlip, docs("boolean", {param("id", "string", false, "The blip's id.")}, "Removes one of this resource's blips.", "False when it had no such blip."));
        map.function("clearBlips", &JS_ClearBlips, docs("void", {}, "Removes every blip this resource set."));
        map.function("isOpen", &JS_IsOpen, docs("boolean", {}, "Whether the player is holding the city map open.", "False while no mission is loaded."));
        map.publish(global);
    }

    void ClientCityMap::RegisterResourceCleanup(Framework::Scripting::ResourceManager &manager) {
        manager.AddOnResourceStopped(&CleanupResource);
    }

    void ClientCityMap::Render() {
        if (gBlips.empty() || !GetWorld().IsReady() || !Map::IsShown()) return;
        Map::View view;
        if (!Map::CurrentView(view)) return;
        const float seconds =
            static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() % 60000) / 1000.0f;
        const float pulse     = 0.6f + 0.4f * std::sin(seconds * 2.0f * std::numbers::pi_v<float> * 1.5f);
        const float textSize  = kLabelHeight * view.scale;
        struct Label {
            float x, y;
            const Blip *blip;
        };
        std::vector<Label> labels;
        for (const auto &blip : gBlips) {
            glm::vec3 position;
            if (!Resolve(blip, position)) continue;
            auto relative     = Map::ToRelative(view, position.x, position.z);
            const float reach = std::max(std::fabs(relative.u), std::fabs(relative.v));
            const bool inside = reach <= Map::kEdge;
            if (!inside && !blip.edge) continue;
            const float radius = blip.size * view.scale;
            const uint32_t fill = Argb(blip.rgb, blip.flash ? pulse : 1.0f);
            if (!inside) {
                const float factor = Map::kEdge * kEdgeInset / reach;
                const float dx = relative.u * view.extentX * 0.8f, dy = relative.v * view.extentY * 0.74f;
                relative.u *= factor;
                relative.v *= factor;
                float x = 0.0f, y = 0.0f;
                Map::ToScreen(view, relative, x, y);
                DrawEdgeArrow(x, y, dx, dy, radius, fill);
                continue;
            }
            float x = 0.0f, y = 0.0f;
            Map::ToScreen(view, relative, x, y);
            DrawShape(blip.shape, x, y, radius + 1.5f, kOutline);
            DrawShape(blip.shape, x, y, radius, fill);
            if (!blip.label.empty()) labels.push_back({x + radius + 3.0f * view.scale, y, &blip});
        }
        // Labels last, so no blip covers another's text.
        for (const auto &label : labels) {
            const float width = SDK::UI::MeasureText(label.blip->label.c_str(), textSize);
            SDK::UI::DrawShadowedText(label.blip->label.c_str(), label.x, label.y - textSize * 0.5f, width + 4.0f, textSize, Argb(label.blip->rgb, 1.0f));
        }
    }

    void ClientCityMap::Reset() {
        gBlips.clear();
    }
} // namespace Mafia1Online::Scripting
