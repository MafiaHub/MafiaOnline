#include "collision_template.h"

#include "shared/features/map_object/map_object_motion.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/matrix.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Mafia1Online::Features::MapObject {
    namespace {
        namespace Native = SDK::Collision;

        constexpr uint32_t kMagic          = 0x43666947; // "GifC"
        constexpr uint16_t kStandaloneLink = 0x7fff;
        constexpr size_t kTreeHeaderSize   = 0x68;

        class Cursor {
          public:
            Cursor(std::span<const uint8_t> bytes, size_t position): _bytes(bytes), _position(position) {}

            template <typename T>
            bool Read(T &out) {
                if (_position > _bytes.size() || _bytes.size() - _position < sizeof(T)) {
                    return false;
                }
                std::memcpy(&out, _bytes.data() + _position, sizeof(T));
                _position += sizeof(T);
                return true;
            }
            bool Skip(size_t count) {
                if (_position > _bytes.size() || _bytes.size() - _position < count) {
                    return false;
                }
                _position += count;
                return true;
            }

          private:
            std::span<const uint8_t> _bytes;
            size_t _position;
        };

        Native::Header ReadHeader(uint32_t packed) {
            return {static_cast<uint8_t>(packed), static_cast<uint8_t>(packed >> 8), static_cast<uint8_t>(packed >> 16), static_cast<uint8_t>(packed >> 24)};
        }

        bool Finite(const glm::vec3 &value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool Finite(const glm::mat4 &value) {
            for (int column = 0; column < 4; ++column) {
                if (!Finite(glm::vec3(value[column])) || !std::isfinite(value[column].w)) {
                    return false;
                }
            }
            return true;
        }

        float Determinant3(const glm::mat4 &value) {
            return glm::determinant(glm::mat3(value));
        }

        float UniformScale(const glm::mat4 &value) {
            return std::cbrt(std::fabs(Determinant3(value)));
        }

        float HorizontalScale(const glm::mat4 &value) {
            return 0.5f * (glm::length(glm::vec3(value[0])) + glm::length(glm::vec3(value[2])));
        }

        glm::vec3 Point(const glm::mat4 &transform, const glm::vec3 &point) {
            return glm::vec3(transform * glm::vec4(point, 1.0f));
        }

        // Splits a transform into a rigid one and the scale along its axes.
        bool Decompose(const glm::mat4 &transform, glm::mat4 &rigid, glm::vec3 &scale) {
            const glm::vec3 c0(transform[0]), c1(transform[1]), c2(transform[2]);
            scale = {glm::length(c0), glm::length(c1), glm::length(c2)};
            if (!(scale.x > 1e-6f && scale.y > 1e-6f && scale.z > 1e-6f) || !Finite(transform)) {
                return false;
            }
            const glm::vec3 x = c0 / scale.x;
            glm::vec3 y       = c1 - glm::dot(c1, x) * x;
            if (!(glm::length(y) > 1e-6f)) {
                return false;
            }
            y           = glm::normalize(y);
            glm::vec3 z = glm::cross(x, y);
            if (glm::dot(z, c2) < 0.0f) {
                z = -z;
            }
            rigid    = glm::mat4(1.0f);
            rigid[0] = glm::vec4(x, 0.0f);
            rigid[1] = glm::vec4(y, 0.0f);
            rigid[2] = glm::vec4(z, 0.0f);
            rigid[3] = glm::vec4(glm::vec3(transform[3]), 1.0f);
            return true;
        }

        std::string Lower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); });
            return value;
        }

        // scene2.bin chunks: {u16 type, u32 size including this header}.
        template <typename F>
        bool EachChunk(std::span<const uint8_t> bytes, F &&visit) {
            while (!bytes.empty()) {
                if (bytes.size() < 6) {
                    return false;
                }
                uint16_t type = 0;
                uint32_t size = 0;
                std::memcpy(&type, bytes.data(), 2);
                std::memcpy(&size, bytes.data() + 2, 4);
                if (size < 6 || size > bytes.size() || !visit(type, bytes.subspan(6, size - 6))) {
                    return false;
                }
                bytes = bytes.subspan(size);
            }
            return true;
        }

        std::string CString(std::span<const uint8_t> value) {
            const auto end = std::find(value.begin(), value.end(), uint8_t {0});
            return std::string(value.begin(), end);
        }
    } // namespace

    uint8_t ScriptMaterial(uint8_t material) {
        switch (material) {
        case 24: return 25;
        case 41: return 33;
        default: return material < Shared::MapObject::kMaterialCount ? material : static_cast<uint8_t>(Shared::MapObject::kDefaultMaterial);
        }
    }

    bool ParseKlz(std::span<const uint8_t> bytes, KlzFile &out) {
        Cursor header(bytes, 0);
        uint32_t magic = 0, version = 0, treeOffset = 0, linkCount = 0, unused0 = 0, unused1 = 0;
        if (!header.Read(magic) || !header.Read(version) || !header.Read(treeOffset) || !header.Read(linkCount) || !header.Read(unused0) || !header.Read(unused1) ||
            magic != kMagic || linkCount > 1'000'000) {
            return false;
        }
        KlzFile file;
        file.records.resize(linkCount);
        for (auto &record : file.records) {
            uint32_t offset = 0;
            if (!header.Read(offset) || offset > bytes.size() || bytes.size() - offset < 5) {
                return false;
            }
            std::memcpy(&record.flags, bytes.data() + offset, 4);
            record.name = CString(bytes.subspan(offset + 4, std::min<size_t>(bytes.size() - offset - 4, 512)));
        }
        if (treeOffset > bytes.size() || bytes.size() - treeOffset < kTreeHeaderSize) {
            return false;
        }
        const auto u32 = [&](size_t offset) {
            uint32_t value = 0;
            std::memcpy(&value, bytes.data() + treeOffset + offset, 4);
            return value;
        };
        const uint32_t width = u32(0x18), length = u32(0x1c);
        const uint32_t faceCount = u32(0x30), orientedCount = u32(0x38), axisCount = u32(0x40), sphereCount = u32(0x48), plateCount = u32(0x50), cylinderCount = u32(0x58),
                       vertexCount = u32(0x60);
        constexpr uint32_t kLimit = 10'000'000;
        if (width > 65536 || length > 65536 || faceCount > kLimit || orientedCount > kLimit || axisCount > kLimit || sphereCount > kLimit || plateCount > kLimit ||
            cylinderCount > kLimit || vertexCount > kLimit) {
            return false;
        }
        // tCollTreeLevel, both cell bound arrays, one reserved word, then
        // the primitive arrays in LoadCollision's order.
        Cursor tree(bytes, treeOffset + kTreeHeaderSize);
        if (!tree.Skip(4 * (size_t(width) + 1) + 4 * (size_t(length) + 1) + 4)) {
            return false;
        }
        file.faces.reserve(faceCount);
        for (uint32_t i = 0; i < faceCount; ++i) {
            uint32_t packed = 0, links[3] = {};
            float plane[4] = {};
            if (!tree.Read(packed) || !tree.Read(links) || !tree.Read(plane)) {
                return false;
            }
            KlzFile::Face face;
            face.header = ReadHeader(packed);
            for (int v = 0; v < 3; ++v) {
                face.vertices[v] = static_cast<uint16_t>(links[v]);
            }
            const uint16_t link = static_cast<uint16_t>(links[0] >> 16);
            face.link   = (link == static_cast<uint16_t>(links[1] >> 16) && link == static_cast<uint16_t>(links[2] >> 16)) ? link : kStandaloneLink;
            face.normal = {plane[0], plane[1], plane[2]};
            file.faces.push_back(face);
        }
        const auto readMatrix = [&](glm::mat4 &out) {
            float values[16] = {};
            if (!tree.Read(values)) {
                return false;
            }
            out = glm::make_mat4(values);
            return true;
        };
        for (uint32_t i = 0; i < axisCount; ++i) {
            uint32_t packed = 0;
            KlzFile::Volume volume;
            if (!tree.Read(packed) || !tree.Read(volume.link) || !tree.Read(volume.minimum) || !tree.Read(volume.maximum)) {
                return false;
            }
            volume.header = ReadHeader(packed);
            file.volumes.push_back(volume);
        }
        for (uint32_t i = 0; i < orientedCount; ++i) {
            uint32_t packed = 0;
            KlzFile::Volume volume;
            glm::vec3 worldMin, worldMax;
            glm::mat4 inverse;
            if (!tree.Read(packed) || !tree.Read(volume.link) || !tree.Read(worldMin) || !tree.Read(worldMax) || !tree.Read(volume.minimum) || !tree.Read(volume.maximum) ||
                !readMatrix(volume.transform) || !readMatrix(inverse)) {
                return false;
            }
            volume.header = ReadHeader(packed);
            file.volumes.push_back(volume);
        }
        for (uint32_t i = 0; i < cylinderCount; ++i) {
            uint32_t packed = 0;
            KlzFile::Cylinder cylinder;
            if (!tree.Read(packed) || !tree.Read(cylinder.link) || !tree.Read(cylinder.x) || !tree.Read(cylinder.z) || !tree.Read(cylinder.radius)) {
                return false;
            }
            cylinder.header = ReadHeader(packed);
            file.cylinders.push_back(cylinder);
        }
        for (uint32_t i = 0; i < plateCount; ++i) {
            uint32_t packed = 0;
            KlzFile::Volume volume;
            glm::mat4 inverse;
            if (!tree.Read(packed) || !tree.Read(volume.link) || !tree.Read(volume.minimum) || !tree.Read(volume.maximum) || !readMatrix(volume.transform) || !readMatrix(inverse)) {
                return false;
            }
            volume.header = ReadHeader(packed);
            file.volumes.push_back(volume);
        }
        for (uint32_t i = 0; i < sphereCount; ++i) {
            uint32_t packed = 0;
            KlzFile::Sphere sphere;
            if (!tree.Read(packed) || !tree.Read(sphere.link) || !tree.Read(sphere.center) || !tree.Read(sphere.radius)) {
                return false;
            }
            sphere.header = ReadHeader(packed);
            file.spheres.push_back(sphere);
        }
        if (!tree.Skip(12 * size_t(vertexCount))) {
            return false;
        }
        out = std::move(file);
        return true;
    }

    bool ReadModelInstances(std::span<const uint8_t> bytes, std::vector<ModelInstance> &out) {
        // The root chunk is "SL" (0x4c53) and spans the file.
        if (bytes.size() < 6 || bytes[0] != 0x53 || bytes[1] != 0x4c) {
            return false;
        }
        uint32_t size = 0;
        std::memcpy(&size, bytes.data() + 2, 4);
        if (size != bytes.size()) {
            return false;
        }
        std::vector<ModelInstance> instances;
        const bool valid = EachChunk(bytes.subspan(6), [&](uint16_t type, std::span<const uint8_t> section) {
            // 0x4000 holds the scene's objects (C_mission's loader).
            if (type != 0x4000) {
                return true;
            }
            return EachChunk(section, [&](uint16_t objectType, std::span<const uint8_t> object) {
                if (objectType != 0x4010) {
                    return true;
                }
                ModelInstance instance;
                if (!EachChunk(object, [&](uint16_t field, std::span<const uint8_t> value) {
                        if (field == 0x0010) {
                            instance.frame = CString(value);
                        }
                        else if (field == 0x2012) {
                            instance.model = Lower(CString(value));
                        }
                        return true;
                    })) {
                    return false;
                }
                if (!instance.frame.empty() && !instance.model.empty() && instances.size() < 200000) {
                    instances.push_back(std::move(instance));
                }
                return true;
            });
        });
        if (valid) {
            out = std::move(instances);
        }
        return valid;
    }

    void CollisionTemplates::Load(KlzFile file, std::vector<ModelInstance> instances) {
        Clear();
        _file      = std::move(file);
        _instances = std::move(instances);
        const size_t records = _file.records.size();
        _facesByRecord.resize(records);
        _volumesByRecord.resize(records);
        _spheresByRecord.resize(records);
        _cylindersByRecord.resize(records);
        for (uint32_t i = 0; i < records; ++i) {
            const auto &name = _file.records[i].name;
            _exact.emplace(name, i);
            if (const auto dot = name.find('.'); dot != std::string::npos && dot > 0 && dot + 1 < name.size()) {
                _children[name.substr(0, dot)].emplace_back(i, name.substr(dot + 1));
            }
        }
        for (uint32_t i = 0; i < _file.faces.size(); ++i) {
            if (_file.faces[i].link < records) {
                _facesByRecord[_file.faces[i].link].push_back(i);
            }
        }
        for (uint32_t i = 0; i < _file.volumes.size(); ++i) {
            if (_file.volumes[i].link < records) {
                _volumesByRecord[_file.volumes[i].link].push_back(i);
            }
        }
        for (uint32_t i = 0; i < _file.spheres.size(); ++i) {
            if (_file.spheres[i].link < records) {
                _spheresByRecord[_file.spheres[i].link].push_back(i);
            }
        }
        for (uint32_t i = 0; i < _file.cylinders.size(); ++i) {
            if (_file.cylinders[i].link < records) {
                _cylindersByRecord[_file.cylinders[i].link].push_back(i);
            }
        }
        _loaded = true;
    }

    void CollisionTemplates::Clear() {
        _file = {};
        _instances.clear();
        _exact.clear();
        _children.clear();
        _facesByRecord.clear();
        _volumesByRecord.clear();
        _spheresByRecord.clear();
        _cylindersByRecord.clear();
        _loaded = false;
    }

    std::vector<CollisionTemplates::Owned> CollisionTemplates::RecordsOf(const std::string &instance) const {
        std::vector<Owned> owned;
        // Record 0 is the primary sector, retail's "no particular frame".
        if (const auto it = _exact.find(instance); it != _exact.end() && it->second != 0) {
            owned.push_back({it->second, {}});
        }
        if (const auto it = _children.find(instance); it != _children.end()) {
            for (const auto &[record, child] : it->second) {
                owned.push_back({record, child});
            }
        }
        return owned;
    }

    std::vector<std::string> CollisionTemplates::Candidates(const std::string &model) const {
        std::vector<std::string> names;
        for (const auto &instance : _instances) {
            if (instance.model != model) {
                continue;
            }
            for (const auto &owned : RecordsOf(instance.frame)) {
                const bool faces = !owned.child.empty() && !_facesByRecord[owned.record].empty();
                if (faces || !_volumesByRecord[owned.record].empty() || !_spheresByRecord[owned.record].empty() || !_cylindersByRecord[owned.record].empty()) {
                    names.push_back(instance.frame);
                    break;
                }
            }
        }
        return names;
    }

    ModelCollision CollisionTemplates::Localize(const std::string &instance, const glm::mat4 &instanceWorld) const {
        ModelCollision collision;
        collision.instance = instance;
        if (!Finite(instanceWorld) || !(std::fabs(Determinant3(instanceWorld)) > 1e-9f)) {
            return collision;
        }
        const glm::mat4 toModel     = glm::inverse(instanceWorld);
        const float uniformScale    = UniformScale(instanceWorld);
        const float horizontalScale = HorizontalScale(instanceWorld);
        for (const auto &owned : RecordsOf(instance)) {
            // A face needs a visual; the instance frame itself is the model.
            if (!owned.child.empty()) {
                for (uint32_t index : _facesByRecord[owned.record]) {
                    const auto &face = _file.faces[index];
                    if (face.header.type & Native::kNonFaceTypeMask) {
                        continue;
                    }
                    collision.faces.push_back({owned.child, face.vertices, ScriptMaterial(face.header.material), face.header.flags});
                }
            }
            for (uint32_t index : _volumesByRecord[owned.record]) {
                const auto &volume = _file.volumes[index];
                if (!Finite(volume.minimum) || !Finite(volume.maximum) || !(volume.minimum.x <= volume.maximum.x && volume.minimum.y <= volume.maximum.y &&
                                                                              volume.minimum.z <= volume.maximum.z)) {
                    continue;
                }
                const glm::vec3 center = 0.5f * (volume.minimum + volume.maximum);
                const glm::vec3 half   = 0.5f * (volume.maximum - volume.minimum);
                ModelCollision::Box box;
                box.halfExtent = half;
                box.material   = ScriptMaterial(volume.header.material);
                box.flags      = volume.header.flags;
                if (volume.header.type == Native::kAxisBox) {
                    box.transform = toModel * glm::translate(glm::mat4(1.0f), center);
                }
                else if (volume.header.type == Native::kOrientedBox || volume.header.type == Native::kPlate) {
                    if (!Finite(volume.transform) || !(std::fabs(Determinant3(volume.transform)) > 1e-6f)) {
                        continue;
                    }
                    box.transform = toModel * volume.transform * glm::translate(glm::mat4(1.0f), center);
                    box.plate     = volume.header.type == Native::kPlate;
                }
                else {
                    continue;
                }
                collision.boxes.push_back(box);
            }
            for (uint32_t index : _spheresByRecord[owned.record]) {
                const auto &sphere = _file.spheres[index];
                if (!Finite(sphere.center) || !(sphere.radius > 0.0f) || !(uniformScale > 1e-6f)) {
                    continue;
                }
                collision.balls.push_back({Point(toModel, sphere.center), sphere.radius / uniformScale, ScriptMaterial(sphere.header.material), sphere.header.flags});
            }
            for (uint32_t index : _cylindersByRecord[owned.record]) {
                const auto &cylinder = _file.cylinders[index];
                if (!std::isfinite(cylinder.x) || !std::isfinite(cylinder.z) || !(cylinder.radius > 0.0f) || !(horizontalScale > 1e-6f)) {
                    continue;
                }
                const glm::vec3 center(cylinder.x, instanceWorld[3].y, cylinder.z);
                collision.posts.push_back({Point(toModel, center), cylinder.radius / horizontalScale, ScriptMaterial(cylinder.header.material), cylinder.header.flags});
            }
        }
        return collision;
    }

    CollisionShape BuildShape(const ModelCollision *collision, const std::vector<MeshPart> &parts, const glm::mat4 &rootWorld, uint8_t material,
                              const glm::vec3 &modelMin, const glm::vec3 &modelMax) {
        CollisionShape shape;
        if (!collision || collision->Empty()) {
            for (const auto &part : parts) {
                for (const auto &triangle : part.triangles) {
                    shape.triangles.push_back({Point(part.world, part.vertices[triangle[0]]), Point(part.world, part.vertices[triangle[1]]),
                                               Point(part.world, part.vertices[triangle[2]]), material, 0});
                }
            }
            return shape;
        }
        for (const auto &face : collision->faces) {
            const auto part = std::find_if(parts.begin(), parts.end(), [&](const MeshPart &candidate) { return candidate.child == face.child; });
            if (part == parts.end() || face.vertices[0] >= part->vertices.size() || face.vertices[1] >= part->vertices.size() || face.vertices[2] >= part->vertices.size()) {
                continue;
            }
            shape.triangles.push_back({Point(part->world, part->vertices[face.vertices[0]]), Point(part->world, part->vertices[face.vertices[1]]),
                                       Point(part->world, part->vertices[face.vertices[2]]), face.material, face.flags});
        }
        const auto addBox = [&](const glm::mat4 &model, glm::vec3 half, bool plate, uint8_t boxMaterial, uint8_t flags) {
            glm::mat4 rigid;
            glm::vec3 scale;
            if (!Decompose(rootWorld * model, rigid, scale)) {
                return;
            }
            half *= scale;
            if (plate) {
                // Retail plates are 0.1 thick whatever their size.
                half.y = 0.1f;
                shape.plates.push_back({rigid, half, boxMaterial, flags});
            }
            else {
                shape.boxes.push_back({rigid, half, boxMaterial, flags});
            }
        };
        for (const auto &box : collision->boxes) {
            addBox(box.transform, box.halfExtent, box.plate, box.material, box.flags);
        }
        const float uniformScale = UniformScale(rootWorld);
        for (const auto &ball : collision->balls) {
            shape.balls.push_back({Point(rootWorld, ball.center), ball.radius * uniformScale, ball.material, ball.flags});
        }
        float topY = Point(rootWorld, modelMin).y;
        for (int index = 0; index < 8; ++index) {
            const glm::vec3 corner((index & 1) ? modelMax.x : modelMin.x, (index & 2) ? modelMax.y : modelMin.y, (index & 4) ? modelMax.z : modelMin.z);
            topY = std::max(topY, Point(rootWorld, corner).y);
        }
        const glm::vec3 up = glm::normalize(glm::vec3(rootWorld[1]));
        for (const auto &post : collision->posts) {
            if (up.y > 0.999f) {
                const glm::vec3 center = Point(rootWorld, post.center);
                shape.posts.push_back({{center.x, center.z}, post.radius * HorizontalScale(rootWorld), topY, post.material, post.flags});
            }
            else {
                // A tilted object: the post becomes a box as tall as the model.
                const glm::vec3 center(post.center.x, 0.5f * (modelMin.y + modelMax.y), post.center.z);
                addBox(glm::translate(glm::mat4(1.0f), center), {post.radius, 0.5f * std::max(0.01f, modelMax.y - modelMin.y), post.radius}, false, post.material, post.flags);
            }
        }
        return shape;
    }
} // namespace Mafia1Online::Features::MapObject
