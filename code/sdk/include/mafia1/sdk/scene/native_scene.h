#pragma once

#include <mafia1/sdk/player/native_actor.h>
#include <mafia1/sdk/graphics/native_graph.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Mafia1Online::SDK::Scene {
    // These interfaces are from the loaded LS3DF.dll. Their methods dispatch
    // through the live object's vtable so DLL relocation is harmless.
    struct NativeFrame;
    struct NativeScene;
    struct NativeDriver;

    struct NativeFrameVTable {
        int(__stdcall *release)(NativeFrame *);
        void(__stdcall *setWorldPosition)(NativeFrame *, const Player::Vector3 *);
        void *_unused08;
        void(__stdcall *setDirection)(NativeFrame *, const Player::Vector3 *, float);
        void *_unused10[2];
        void(__stdcall *update)(NativeFrame *);
        void *_unused1c[2];
        void(__stdcall *setOn)(NativeFrame *, bool);
        int(__stdcall *setName)(NativeFrame *, const char *);
        int(__stdcall *linkTo)(NativeFrame *, NativeFrame *, uint32_t);
        void *_unused30[2];
        NativeFrame *(__stdcall *findChildFrame)(NativeFrame *, const char *, uint32_t);
    };
    static_assert(offsetof(NativeFrameVTable, setWorldPosition) == 0x04);
    static_assert(offsetof(NativeFrameVTable, setDirection) == 0x0c);
    static_assert(offsetof(NativeFrameVTable, update) == 0x18);
    static_assert(offsetof(NativeFrameVTable, setOn) == 0x24);
    static_assert(offsetof(NativeFrameVTable, setName) == 0x28);
    static_assert(offsetof(NativeFrameVTable, linkTo) == 0x2c);
    // Retail human setup calls this slot with ENUMF_ALL (0xffff), e.g. for the
    // base mesh and weapon joints.
    static_assert(offsetof(NativeFrameVTable, findChildFrame) == 0x38);

    // I3D_camera adds projection setters after I3D_frame's virtual methods.
    // The supported LS3DF.dll vtable at 0x1009b6d0 has SetRange at +0x58.
    struct NativeCameraVTable {
        NativeFrameVTable frame;
        void *_unused3c[5];
        void(__stdcall *setFov)(NativeFrame *, float);
        void *_unused54;
        void(__stdcall *setRange)(NativeFrame *, float, float);
        void *_unused5c[3];
        void(__stdcall *setAspectRatio)(NativeFrame *, float);
    };
    static_assert(offsetof(NativeCameraVTable, setFov) == 0x50);
    static_assert(offsetof(NativeCameraVTable, setRange) == 0x58);
    static_assert(offsetof(NativeCameraVTable, setAspectRatio) == 0x68);

    // I3D_light appends its setters at +0x50 in the retail LS3DF vtable.
    // VC6 reverses the overloaded SetColor declarations: RGB is +0x54,
    // vector is +0x58 (verified against the original LS3DF.dll vtable).
    struct NativeLightVTable {
        NativeFrameVTable frame;
        void *_unused3c[5];
        void(__stdcall *setLightType)(NativeFrame *, int);
        void(__stdcall *setColorRgb)(NativeFrame *, float, float, float);
        void(__stdcall *setColor)(NativeFrame *, const Player::Vector3 *);
        void(__stdcall *setPower)(NativeFrame *, float);
    };
    static_assert(offsetof(NativeLightVTable, setLightType) == 0x50);
    static_assert(offsetof(NativeLightVTable, setColorRgb) == 0x54);
    static_assert(offsetof(NativeLightVTable, setColor) == 0x58);
    static_assert(offsetof(NativeLightVTable, setPower) == 0x5c);

    // Original LS3DF I3D_object vtable at 0x1009c348 has
    // SetTransparency (0x10037dd0) at +0x84. Derived mesh visuals keep it.
    struct NativeObjectVTable {
        NativeFrameVTable frame;
        void *_unused3c[18];
        void(__stdcall *setTransparency)(NativeFrame *, float);
    };
    static_assert(offsetof(NativeObjectVTable, setTransparency) == 0x84);

    // I3D_frame::GetWorldMatrix at Game 0x47acd0: rebuilds the world matrix
    // through UpdateWMatrixProc only while FRMFLAGS_WMAT_VALID (0x20 at
    // +0xac) is clear, then returns &m_mWorldMat.
    inline constexpr uintptr_t kFrameGetWorldMatrix = 0x47acd0;
    inline constexpr uint32_t kFrameLocalMatrixScaled = 0x4;
    inline constexpr uint32_t kFrameRotationValid = 0x8;
    inline constexpr uint32_t kFrameLocalMatrixBuilt = 0x10;
    inline constexpr uint32_t kFrameWorldMatrixValid = 0x20;
    inline constexpr uint32_t kFrameWorldBoundValid = 0x100;
    inline constexpr uint32_t kFrameConstructed = 0x40000000;

    struct NativeFrame {
        NativeFrameVTable *vtable;

        struct WorldBasis {
            Player::Vector3 right;
            Player::Vector3 up;
            Player::Vector3 forward;
        };

        WorldBasis GetWorldBasis() {
            const float *matrix = UpdatedWorldMatrix();
            return {{matrix[0], matrix[1], matrix[2]}, {matrix[4], matrix[5], matrix[6]}, {matrix[8], matrix[9], matrix[10]}};
        }
        // reM I3D_frame::GetPos: the local translation row.
        Player::Vector3 LocalPosition() const { return {_localMatrix[12], _localMatrix[13], _localMatrix[14]}; }
        Player::Vector3 LocalScale() const { return {_scale[0], _scale[1], _scale[2]}; }
        std::array<float, 4> LocalRotation() const { return {_rotation[0], _rotation[1], _rotation[2], _rotation[3]}; }
        // Valid after Update(); row-major with translation in row 3.
        const float *WorldMatrix() const { return _worldMatrix; }
        NativeFrame *Parent() const { return _parent; }
        const char *Name() const { return _name ? _name : ""; }
        uint32_t FrameType() const { return _frameType; }
        bool SupportsTransparency() const {
            if (_frameType != 1) { // FRAME_VISUAL
                return false;
            }
            const uint32_t type = *reinterpret_cast<const uint32_t *>(reinterpret_cast<const std::byte *>(this) + 0x1d4);
            switch (type) {
            case 0x50414d4c: // I3D_VISUAL_LIT_OBJECT
            case 0x44524242: // I3D_VISUAL_BILLBOARD
            case 0x4850524d: // I3D_VISUAL_MORPH
            case 0x4d474e53: // I3D_VISUAL_SINGLEMESH
            case 0x524d4d53: // I3D_VISUAL_SINGLEMORPH
            case 0x534e454c: // I3D_VISUAL_LENSF
            case 0x5f4a424f: // I3D_VISUAL_OBJECT
                return true;
            default: return false;
            }
        }
        float Transparency() const {
            return *reinterpret_cast<const float *>(reinterpret_cast<const std::byte *>(this) + 0x214);
        }
        void SetTransparency(float opacity) {
            reinterpret_cast<NativeObjectVTable *>(vtable)->setTransparency(this, opacity);
        }
        // FRMFLAGS_ON, bit 0 of the flags SetOn (LS3DF 0x1001b400) writes at +0xac.
        bool IsOn() const { return (_flags & 1u) != 0; }
        Player::NativeActor *ActorOwner() const {
            return _internalBuffer ? *static_cast<Player::NativeActor **>(_internalBuffer) : nullptr;
        }

        // reM I3D_frame: matrix translation is row 3, forward is row 2.
        // GetWorldMatrix rebuilds parent and local transforms on demand;
        // Update() alone leaves some mission dummy frames' cache at zero.
        const float *UpdatedWorldMatrix() {
            using Call = const float *(__thiscall *)(NativeFrame *);
            return reinterpret_cast<Call>(kFrameGetWorldMatrix)(this);
        }
        Player::Vector3 WorldPosition() {
            const float *matrix = UpdatedWorldMatrix();
            return {matrix[12], matrix[13], matrix[14]};
        }
        Player::Vector3 WorldDirection() {
            const float *matrix = UpdatedWorldMatrix();
            Player::Vector3 direction {matrix[8], matrix[9], matrix[10]};
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length > 0.0001f) {
                direction.x /= length;
                direction.y /= length;
                direction.z /= length;
            }
            return direction;
        }

        int Release() {
            return vtable->release(this);
        }
        void SetWorldPosition(const Player::Vector3 &position) {
            vtable->setWorldPosition(this, &position);
        }
        void SetDirection(const Player::Vector3 &direction, float roll = 0.0f) {
            vtable->setDirection(this, &direction, roll);
        }
        // reM I3D_frame::SetRot/SetScale are inline. The cached transform
        // flags and field offsets are verified by I3D_frame.h in LS3DF.
        void SetLocalRotation(float w, float x, float y, float z) {
            const float length = std::sqrt(w * w + x * x + y * y + z * z);
            if (length <= 0.0001f) {
                return;
            }
            _rotation[0] = w / length;
            _rotation[1] = x / length;
            _rotation[2] = y / length;
            _rotation[3] = z / length;
            _flags &= ~(kFrameLocalMatrixScaled | kFrameLocalMatrixBuilt | kFrameWorldMatrixValid | kFrameWorldBoundValid);
            _flags |= kFrameRotationValid | kFrameConstructed;
        }
        void SetLocalScale(const Player::Vector3 &scale) {
            _scale[0] = scale.x;
            _scale[1] = scale.y;
            _scale[2] = scale.z;
            _flags &= ~(kFrameLocalMatrixScaled | kFrameRotationValid | kFrameLocalMatrixBuilt |
                        kFrameWorldMatrixValid | kFrameWorldBoundValid | kFrameConstructed);
            _flags |= kFrameRotationValid | kFrameConstructed;
        }
        void Update() {
            vtable->update(this);
        }
        void SetOn(bool on) {
            vtable->setOn(this, on);
        }
        bool SetName(const char *name) {
            return vtable->setName(this, name) >= 0;
        }
        NativeFrame *FindChildFrame(const char *name, uint32_t flags = 0xffff) {
            return vtable->findChildFrame(this, name, flags);
        }
        // A world position that is never stale: a bone whose ancestor moved
        // this frame, such as a ped seated in a driven car, is rebuilt first.
        Player::Vector3 ValidWorldPosition() {
            return WorldPosition();
        }
        bool LinkTo(NativeFrame *parent, uint32_t flags = 0) {
            return vtable->linkTo(this, parent, flags) >= 0;
        }

        uint32_t _referenceCount;
        void *_internalBuffer;
        void *_properties;
        float _worldMatrix[16];
        float _localMatrix[16];
        float _scale[3];
        float _rotation[4];
        uint32_t _flags;
        std::byte _unusedB0[0x100 - 0xb0];
        char *_name;
        std::byte _unused104[0x110 - 0x104];
        uint32_t _frameType;
        std::byte _unused114[0x120 - 0x114];
        NativeFrame *_parent;
    };
    static_assert(offsetof(NativeFrame, _worldMatrix) == 0x10);
    static_assert(offsetof(NativeFrame, _localMatrix) == 0x50);
    static_assert(offsetof(NativeFrame, _scale) == 0x90);
    static_assert(offsetof(NativeFrame, _rotation) == 0x9c);
    static_assert(offsetof(NativeFrame, _flags) == 0xac);
    static_assert(offsetof(NativeFrame, _name) == 0x100);
    static_assert(offsetof(NativeFrame, _frameType) == 0x110);
    static_assert(offsetof(NativeFrame, _parent) == 0x120);

    // Typed retail I3D_camera projection, verified against the reM layout.
    // Only SetRange may change the clipping planes: it rebuilds the matrices.
    struct NativeCameraFrame {
        NativeFrame frame;
        std::byte _frameTail[0x140 - sizeof(NativeFrame)];
        float fovRadians;
        float nearClip;
        float farClip;
    };
    static_assert(offsetof(NativeCameraFrame, fovRadians) == 0x140);
    static_assert(offsetof(NativeCameraFrame, nearClip) == 0x144);
    static_assert(offsetof(NativeCameraFrame, farClip) == 0x148);

    inline float CameraFovRadians(const NativeFrame *camera) {
        return reinterpret_cast<const NativeCameraFrame *>(camera)->fovRadians;
    }
    inline void CameraSetRange(NativeFrame *camera, float nearClip, float farClip) {
        reinterpret_cast<NativeCameraVTable *>(camera->vtable)->setRange(camera, nearClip, farClip);
    }

    inline float CameraNearClip(const NativeFrame *camera) {
        return reinterpret_cast<const NativeCameraFrame *>(camera)->nearClip;
    }

    inline float CameraFarClip(const NativeFrame *camera) {
        return reinterpret_cast<const NativeCameraFrame *>(camera)->farClip;
    }
    inline void CameraSetFov(NativeFrame *camera, float radians) {
        reinterpret_cast<NativeCameraVTable *>(camera->vtable)->setFov(camera, radians);
    }
    inline void CameraSetAspectRatio(NativeFrame *camera, float ratio) {
        reinterpret_cast<NativeCameraVTable *>(camera->vtable)->setAspectRatio(camera, ratio);
    }
    inline void LightSetType(NativeFrame *light, int type) {
        reinterpret_cast<NativeLightVTable *>(light->vtable)->setLightType(light, type);
    }
    inline void LightSetColor(NativeFrame *light, float red, float green, float blue) {
        reinterpret_cast<NativeLightVTable *>(light->vtable)->setColorRgb(light, red, green, blue);
    }
    inline void LightSetPower(NativeFrame *light, float power) {
        reinterpret_cast<NativeLightVTable *>(light->vtable)->setPower(light, power);
    }
    inline void SectorAddLight(NativeFrame *sector, NativeFrame *light) {
        // I3D_frame::LinkTo only attaches the frame hierarchy. Rendering reads
        // I3D_sector::m_lights, which mission loading fills with AddLight.
        using Call = void(__stdcall *)(NativeFrame *, NativeFrame *);
        reinterpret_cast<Call>(Graphics::Ls3dfBase() + 0x4f3d0)(sector, light);
    }

    // reM I3D_COLLISION, filled by TestColHierarchy.
    struct NativeCollision {
        float distance;
        Player::Vector3 normal;
        uint16_t properties[3];
        uint16_t _pad16;
        int hitId;
    };
    static_assert(sizeof(NativeCollision) == 0x1c);
    static_assert(offsetof(NativeCollision, normal) == 0x04);
    static_assert(offsetof(NativeCollision, hitId) == 0x18);

    struct NativeSceneVTable {
        void *_unused00[0x54 / sizeof(void *)];
        int(__stdcall *render)(NativeScene *);
        NativeFrame *(__stdcall *findFrame)(NativeScene *, const char *, uint32_t);
        void(__stdcall *addFrame)(NativeScene *, NativeFrame *);
        int(__stdcall *deleteFrame)(NativeScene *, NativeFrame *);
        void *_unused64[(0x6c - 0x64) / sizeof(void *)];
        void(__stdcall *setActiveCamera)(NativeScene *, NativeFrame *);
        void *_unused70;
        int(__stdcall *transformPoints)(NativeScene *, const Player::Vector3 *, float *, uint32_t);
        void *_unused78[(0xbc - 0x78) / sizeof(void *)];
        int(__stdcall *setFrameSectorPos)(NativeScene *, NativeFrame *, const Player::Vector3 *);
        void *_unusedC0[(0xd0 - 0xc0) / sizeof(void *)];
        NativeFrame *(__stdcall *testColHierarchy)(NativeScene *, const Player::Vector3 *, const Player::Vector3 *,
                                                   NativeFrame *, NativeCollision *, uint32_t, NativeFrame *, int, void *);
        void *_unusedD4[(0xe8 - 0xd4) / sizeof(void *)];
        void(__stdcall *setWeatherSystemParam)(NativeScene *, uint32_t, uint32_t);
        uint32_t(__stdcall *getWeatherSystemParam)(NativeScene *, uint32_t);
        void(__stdcall *weatherSystemReset)(NativeScene *);
        void *_unusedF4[(0x104 - 0xf4) / sizeof(void *)];
        int(__stdcall *setViewport)(NativeScene *, uint32_t, uint32_t, uint32_t, uint32_t);
    };
    static_assert(offsetof(NativeSceneVTable, render) == 0x54);
    static_assert(offsetof(NativeSceneVTable, findFrame) == 0x58);
    static_assert(offsetof(NativeSceneVTable, addFrame) == 0x5c);
    static_assert(offsetof(NativeSceneVTable, deleteFrame) == 0x60);
    // LS3DF 0x10049e60 at scene vtable +0x74: the active camera's clip
    // transform to screen pixels, writing x, y, z and w = 1 / clip w.
    static_assert(offsetof(NativeSceneVTable, transformPoints) == 0x74);
    // DropOutItems files a dropped weapon's model into its sector through
    // this slot at 0x57ff66.
    static_assert(offsetof(NativeSceneVTable, setFrameSectorPos) == 0xbc);
    // Retail C_car::Hit calls this slot at 0x42368a with flags, skip frame,
    // parameter and callback all zero.
    static_assert(offsetof(NativeSceneVTable, testColHierarchy) == 0xd0);
    // WEATHER_SETPARAM calls +0xe8 at 0x4743c6 and WEATHER_RESET +0xf0 at
    // 0x474375; OsefujPocasi reads +0xec at 0x5b9571.
    static_assert(offsetof(NativeSceneVTable, setWeatherSystemParam) == 0xe8);
    static_assert(offsetof(NativeSceneVTable, getWeatherSystemParam) == 0xec);
    static_assert(offsetof(NativeSceneVTable, weatherSystemReset) == 0xf0);
    static_assert(offsetof(NativeSceneVTable, setViewport) == 0x104);

    struct NativeScene {
        NativeSceneVTable *vtable;

        NativeFrame *FindFrame(const char *name, uint32_t flags = 0xffff) {
            return vtable->findFrame(this, name, flags);
        }
        NativeFrame *ActiveCamera() const {
            return _activeCamera;
        }
        NativeFrame *PrimarySector() const {
            return _primarySector;
        }
        // reM I3D_scene::m_vClearColor at +0x220, consumed by I3D_driver::Render.
        void SetClearColor(const Player::Vector3 &color) { _clearColor = color; }
        bool Render() { return vtable->render(this) >= 0; }
        void SetActiveCamera(NativeFrame *camera) { vtable->setActiveCamera(this, camera); }
        bool SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            return vtable->setViewport(this, x, y, x + width, y + height) >= 0;
        }
        void AddFrame(NativeFrame *frame) {
            vtable->addFrame(this, frame);
        }
        bool DeleteFrame(NativeFrame *frame) {
            return vtable->deleteFrame(this, frame) >= 0;
        }
        void SetFrameSectorPos(NativeFrame *frame, const Player::Vector3 &position) {
            vtable->setFrameSectorPos(this, frame, &position);
        }
        // Float parameters travel as their bit pattern; Get returns what Set
        // takes, so a value read back restores the same state.
        void SetWeatherParam(uint32_t param, uint32_t value) {
            vtable->setWeatherSystemParam(this, param, value);
        }
        uint32_t GetWeatherParam(uint32_t param) {
            return vtable->getWeatherSystemParam(this, param);
        }
        // Restores the rain or, with the alternate mode flag set, the snow
        // preset; keeps the enabled flag and drops live particles.
        void ResetWeather() {
            vtable->weatherSystemReset(this);
        }
        // Screen pixel position of a world point; false behind the camera.
        bool ProjectToScreen(const Player::Vector3 &world, float &x, float &y) {
            float out[4] = {};
            if (vtable->transformPoints(this, &world, out, 1) < 0 || !(out[3] > 0.0f)) {
                return false;
            }
            x = out[0];
            y = out[1];
            return true;
        }
        // Nearest hit on root's subtree for a ray of length |direction|.
        NativeFrame *TestColHierarchy(const Player::Vector3 &start, const Player::Vector3 &direction, NativeFrame *root,
                                      NativeCollision &collision) {
            collision = {};
            return vtable->testColHierarchy(this, &start, &direction, root, &collision, 0, nullptr, 0, nullptr);
        }

        std::byte _unused04[0x178];
        NativeFrame *_activeCamera;
        std::byte _unused180[0x90];
        NativeFrame *_primarySector;
        std::byte _unused214[0x0c];
        Player::Vector3 _clearColor;
    };
    static_assert(offsetof(NativeScene, _activeCamera) == 0x17c);
    static_assert(offsetof(NativeScene, _primarySector) == 0x210);
    static_assert(offsetof(NativeScene, _clearColor) == 0x220);

    struct NativeDriverVTable {
        void *_unused00[0x1c / sizeof(void *)];
        int(__stdcall *render)(NativeDriver *, NativeScene *);
        void *_unused20[(0x50 - 0x20) / sizeof(void *)];
        NativeFrame *(__stdcall *createFrame)(NativeDriver *, int);
    };
    static_assert(offsetof(NativeDriverVTable, render) == 0x1c);
    static_assert(offsetof(NativeDriverVTable, createFrame) == 0x50);

    struct NativeDriver {
        NativeDriverVTable *vtable;

        NativeFrame *CreateModel() {
            return vtable->createFrame(this, 9);
        }
        NativeScene *CreateScene() {
            return reinterpret_cast<NativeScene *>(vtable->createFrame(this, 13));
        }
        NativeFrame *CreateCamera() {
            return vtable->createFrame(this, 3);
        }
        NativeFrame *CreateLight() {
            return vtable->createFrame(this, 2);
        }
        // I3D_FRAME_TYPE FRAME_DUMMY; the caller owns the reference.
        NativeFrame *CreateDummy() {
            return vtable->createFrame(this, 6);
        }
    };

    inline NativeDriver *GetDriver() {
        // Game.exe stores a pointer to the live LS3DF driver here.
        return *reinterpret_cast<NativeDriver **>(0x647ed8);
    }

    struct NativeModelCache {
        bool OpenModel(NativeFrame *model, const char *name) {
            using Call = int(__thiscall *)(NativeModelCache *, NativeFrame *, const char *, uint32_t, void *, void *, uint32_t);
            return reinterpret_cast<Call>(0x4087e0)(this, model, name, 0, nullptr, nullptr, 0) >= 0;
        }
    };

    inline NativeModelCache *GetModelCache() {
        return reinterpret_cast<NativeModelCache *>(0x647dd0);
    }
} // namespace Mafia1Online::SDK::Scene
