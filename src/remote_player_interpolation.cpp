#include "remote_player_interpolation.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "camera_position_math.h"
#include "high_frequency_viewmodel_math.h"

namespace remote_player_interpolation {
namespace {
using camera_position::Position;
using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

constexpr uintptr_t kActorPool = 0x00715900;
constexpr uintptr_t kLocalPlayer = 0x0096C290;
constexpr uintptr_t kNetworkReady = 0x009F2194;
constexpr uintptr_t kPlayerPacket = 0x00482E10;
constexpr uintptr_t kBuildPlayerPalette = 0x00484150;
constexpr uintptr_t kBuildActorMatrix = 0x0055DB60;
constexpr uintptr_t kGameUpdateSerial = 0x009F374C;
constexpr size_t kActorSize = 0x29C;
constexpr size_t kPlayerSlots = 256;
constexpr int kMaxBones = 64;
constexpr int kAttachmentBone = 16;
constexpr int kSecondaryBone = 15;
constexpr float kFixedToWorld = 1.0f / 65536;
constexpr int64_t kMaxStep = 4 * 65536;
constexpr Milliseconds kMaxPacketGap{250};
constexpr Milliseconds kMaxBlendTime{100};
constexpr Milliseconds kGameUpdateTime{16};
constexpr Milliseconds kMaxPoseGap{100};

template <typename T>
T Read(uintptr_t address) {
    return *reinterpret_cast<const T*>(address);
}

struct Identity {
    uintptr_t actor, definition, playerState;
    uint32_t networkId, life;

    bool operator==(const Identity& other) const {
        return actor == other.actor && definition == other.definition &&
               playerState == other.playerState && networkId == other.networkId &&
               life == other.life;
    }
};

bool WithinBlendDistance(Position a, Position b) {
    const int64_t x = int64_t(b.x) - a.x;
    const int64_t y = int64_t(b.y) - a.y;
    const int64_t z = int64_t(b.z) - a.z;
    const auto withinRange = [](int64_t axis) { return axis >= -kMaxStep && axis <= kMaxStep; };
    return withinRange(x) && withinRange(y) && withinRange(z) &&
           x * x + y * y + z * z <= kMaxStep * kMaxStep;
}

struct SnapshotHistory {
    Identity identity{};
    Position previous{}, current{};
    Clock::time_point received{};
    Milliseconds blendTime{};

    void Observe(Identity key, Position position, Position before, Clock::time_point now) {
        const auto gap = Milliseconds(now - received);
        blendTime = identity == key && current == before && gap > Milliseconds::zero() &&
                    gap <= kMaxPacketGap && WithinBlendDistance(before, position)
                   ? std::min(gap, kMaxBlendTime) : Milliseconds::zero();
        identity = key;
        previous = before;
        current = position;
        received = now;
    }

    float Remaining(Identity key, Position live, Clock::time_point now) const {
        if (!(identity == key) || !(current == live) || blendTime == Milliseconds::zero()) {
            return 0.0f;
        }
        return static_cast<float>(std::clamp(
            1.0 - Milliseconds(now - received) / blendTime, 0.0, 1.0));
    }
};

struct PacketContext {
    const void* buffer;
    int size;
    const void* end;
    const void* cursor;
    uintptr_t actor;
    int mode;
    int record;
};
static_assert(sizeof(void*) == 4 && offsetof(PacketContext, actor) == 0x10);
using ReadPacketFn = int(__cdecl*)(PacketContext*);
using BuildPaletteFn = void(__cdecl*)(float*, uintptr_t, float*, float*, void*);
using BuildActorMatrixFn = void(__cdecl*)(float*, const int32_t*);

constexpr int kMatrixSize = high_frequency_viewmodel::kMatrixValueCount;
using Matrix = std::array<float, kMatrixSize>;

bool IsRigidMatrix(const float* matrix) {
    const auto dot = [matrix](int a, int b) {
        return matrix[a] * matrix[b] + matrix[a + 1] * matrix[b + 1] + matrix[a + 2] * matrix[b + 2];
    };
    const float determinant =
        matrix[0] * (matrix[5] * matrix[10] - matrix[6] * matrix[9]) -
        matrix[4] * (matrix[1] * matrix[10] - matrix[2] * matrix[9]) +
        matrix[8] * (matrix[1] * matrix[6] - matrix[2] * matrix[5]);
    constexpr float tolerance = 0.02f;
    return std::abs(determinant - 1.0f) < tolerance &&
           std::abs(dot(0, 0) - 1.0f) < tolerance && std::abs(dot(4, 4) - 1.0f) < tolerance &&
           std::abs(dot(8, 8) - 1.0f) < tolerance && std::abs(dot(0, 4)) < tolerance &&
           std::abs(dot(0, 8)) < tolerance && std::abs(dot(4, 8)) < tolerance &&
           std::isfinite(matrix[12]) && std::isfinite(matrix[13]) && std::isfinite(matrix[14]);
}

struct BonePose {
    Matrix local{};
    bool valid = false;
};

struct PoseHistory {
    Identity identity{};
    uintptr_t skeleton = 0;
    int boneCount = 0;
    uint32_t update = 0;
    Position position{};
    Clock::time_point sampleTime{}, lastDraw{};
    bool blending = false;
    std::array<BonePose, kMaxBones> previous{}, current{};

    // Capture native poses once per game update in the actor's coordinate frame.
    float Observe(Identity key, uintptr_t rig, int bones, Position live, uint32_t serial,
                  Clock::time_point now, const float* actorMatrix, const float* palette) {
        const bool continuous = identity == key && skeleton == rig && boneCount == bones &&
            now - lastDraw <= kMaxPoseGap && WithinBlendDistance(position, live);
        lastDraw = now;
        if (!continuous || serial != update) {
            blending = continuous && serial - update == 1;
            if (blending) std::copy_n(current.begin(), bones, previous.begin());
            Matrix inverse{};
            high_frequency_viewmodel::InvertRigidTransform(actorMatrix, inverse.data());
            for (int bone = 0; bone < bones; ++bone) {
                const auto* native = palette + bone * kMatrixSize;
                auto& pose = current[bone];
                pose.valid = IsRigidMatrix(native);
                if (pose.valid) high_frequency_viewmodel::MultiplyMatrix(inverse.data(), native, pose.local.data());
            }
            identity = key;
            skeleton = rig;
            boneCount = bones;
            position = live;
            update = serial;
            sampleTime = now;
        }
        return blending ? static_cast<float>(std::clamp(Milliseconds(now - sampleTime) /
                                                         kGameUpdateTime, 0.0, 1.0)) : 1.0f;
    }

    void Apply(float alpha, const float* actorMatrix, float* palette,
               float* attachment, float* secondary) const {
        for (int bone = 0; bone < boneCount; ++bone) {
            auto* native = palette + bone * kMatrixSize;
            // The renderer can hide a bone with a singular matrix.
            if (!previous[bone].valid || !current[bone].valid || !IsRigidMatrix(native)) continue;
            Matrix local{}, world{};
            high_frequency_viewmodel::InterpolateRigidTransform(previous[bone].local.data(),
                current[bone].local.data(), alpha, local.data());
            high_frequency_viewmodel::MultiplyMatrix(actorMatrix, local.data(), world.data());

            // The native builder places these sockets from bones 16 and 15.
            // Keep each socket's offset relative to its current bone.
            float* socket = bone == kAttachmentBone ? attachment :
                            bone == kSecondaryBone ? secondary : nullptr;
            if (socket != nullptr) {
                Matrix inverse{}, relative{};
                high_frequency_viewmodel::InvertRigidTransform(native, inverse.data());
                high_frequency_viewmodel::MultiplyMatrix(inverse.data(), socket, relative.data());
                high_frequency_viewmodel::MultiplyMatrix(world.data(), relative.data(), socket);
            }
            std::copy(world.begin(), world.end(), native);
        }
    }
};

SnapshotHistory g_players[kPlayerSlots]{};
PoseHistory g_poses[kPlayerSlots]{};
bool g_positionEnabled = false;
bool g_animationEnabled = false;

Position ReadPosition(uintptr_t actor) {
    return {Read<int32_t>(actor + 8), Read<int32_t>(actor + 0xC), Read<int32_t>(actor + 0x10)};
}

size_t FindSlot(uintptr_t actor) {
    const auto pool = Read<uintptr_t>(kActorPool);
    if (pool == 0 || actor < pool) return kPlayerSlots;
    const auto offset = actor - pool;
    if (offset % kActorSize != 0 || offset / kActorSize >= kPlayerSlots) return kPlayerSlots;
    return offset / kActorSize;
}

bool IdentifyRemotePlayer(uintptr_t actor, Identity& identity) {
    const auto definition = Read<uintptr_t>(actor + 0x24);
    const auto flags = Read<uint32_t>(actor + 0x20);
    if (Read<int>(kNetworkReady) == 0 || actor == Read<uintptr_t>(kLocalPlayer) ||
        (flags & 5) != 1 || Read<int16_t>(actor + 0xE2) <= 0 ||
        Read<uintptr_t>(actor + 0x130) != 0 || Read<uintptr_t>(actor + 0x13C) != 0 ||
        definition == 0 ||
        Read<uintptr_t>(definition + 0x13C) != kPlayerPacket) return false;
    identity = {actor, definition, Read<uintptr_t>(actor + 0x38),
                Read<uint32_t>(actor + 0x48), Read<uint32_t>(actor + 0x4C)};
    return true;
}

int __cdecl ReceivePlayerPosition(PacketContext* packet) {
    // This call site has already validated the slot and the native callback.
    const auto definition = Read<uintptr_t>(packet->actor + 0x24);
    const auto receive = Read<ReadPacketFn>(definition + 0x13C);
    if (packet->mode != 2 || (packet->record != 0xB && packet->record != 0xC) ||
        reinterpret_cast<uintptr_t>(receive) != kPlayerPacket) return receive(packet);

    const auto actor = packet->actor;
    const size_t slot = FindSlot(actor);
    if (slot == kPlayerSlots) return receive(packet);
    auto& history = g_players[slot];
    Identity beforeKey{}, afterKey{};
    const bool wasOnFoot = IdentifyRemotePlayer(actor, beforeKey);
    const auto before = ReadPosition(actor);
    const int result = receive(packet);
    if (result != 0 || !wasOnFoot || !IdentifyRemotePlayer(actor, afterKey) ||
        !(beforeKey == afterKey)) {
        history = {};
    } else {
        history.Observe(afterKey, ReadPosition(actor), before, Clock::now());
    }
    return result;
}

struct VisualOffset { float x, y, z; };

VisualOffset ToVisualOffset(const SnapshotHistory& history, float remaining) {
    const float scale = kFixedToWorld * remaining;
    const auto axis = [scale](int32_t from, int32_t to) {
        return static_cast<float>(int64_t(from) - to) * scale;
    };
    // The native matrix builder maps game coordinates to (-Y, Z, X).
    return {-axis(history.previous.y, history.current.y),
             axis(history.previous.z, history.current.z),
             axis(history.previous.x, history.current.x)};
}

void Translate(float* matrix, VisualOffset offset) {
    matrix[12] += offset.x;
    matrix[13] += offset.y;
    matrix[14] += offset.z;
}

void __cdecl BuildWorldPlayerPalette(float* palette, uintptr_t actor,
                                    float* attachment, float* secondary, void* extra) {
    reinterpret_cast<BuildPaletteFn>(kBuildPlayerPalette)(palette, actor, attachment, secondary, extra);
    const size_t slot = FindSlot(actor);
    if (slot == kPlayerSlots) return;
    auto& position = g_players[slot];
    auto& pose = g_poses[slot];
    if (!g_animationEnabled && position.identity.actor == 0) return;

    Identity identity{};
    if (!IdentifyRemotePlayer(actor, identity)) {
        position = {};
        pose.identity = {};
        return;
    }
    const auto live = ReadPosition(actor);
    const auto now = Clock::now();
    float remaining = 0.0f;
    if (g_positionEnabled && position.identity.actor != 0) {
        if (identity == position.identity && live == position.current) {
            remaining = position.Remaining(identity, live, now);
        } else {
            position = {};
        }
    }
    if (!g_animationEnabled && remaining <= 0.0f) return;

    const auto modelGroup = Read<uintptr_t>(actor + 0x28);
    const auto skeleton = modelGroup != 0 ? Read<uintptr_t>(modelGroup + 0x90) : 0;
    const int bones = skeleton != 0 ? Read<int>(skeleton + 0x10) : 0;
    // The caller's stack reserves 0x1000 bytes for the 64-byte matrices.
    if (bones < 1 || bones > kMaxBones) {
        pose.identity = {};
        return;
    }
    if (g_animationEnabled) {
        Matrix actorMatrix{};
        reinterpret_cast<BuildActorMatrixFn>(kBuildActorMatrix)(
            actorMatrix.data(), reinterpret_cast<const int32_t*>(actor + 8));
        const float alpha = pose.Observe(identity, skeleton, bones, live,
            Read<uint32_t>(kGameUpdateSerial), now, actorMatrix.data(), palette);
        if (alpha < 1.0f) pose.Apply(alpha, actorMatrix.data(), palette, attachment, secondary);
    }
    if (remaining > 0.0f) {
        const auto offset = ToVisualOffset(position, remaining);
        for (int bone = 0; bone < bones; ++bone) Translate(palette + bone * kMatrixSize, offset);
        Translate(attachment, offset);
        Translate(secondary, offset);
    }
}

struct CallPatch {
    uintptr_t address;
    uintptr_t replacement;
    size_t size;
    unsigned char expected[6];
    DWORD protection = 0;
};
}  // namespace

bool Install(bool position, bool animation) {
    if (!position && !animation) return true;
    CallPatch patches[] = {
        {0x0048DF08, reinterpret_cast<uintptr_t>(&BuildWorldPlayerPalette), 5,
         {0xE8, 0x43, 0x62, 0xFF, 0xFF}},
        {0x00485E6C, reinterpret_cast<uintptr_t>(&ReceivePlayerPosition), 6,
         {0xFF, 0x90, 0x3C, 0x01, 0x00, 0x00}},
    };
    // Animation-only mode needs only the renderer, so leave reception unpatched.
    const size_t count = position ? 2 : 1;
    for (size_t index = 0; index < count; ++index) {
        const auto& call = patches[index];
        if (std::memcmp(reinterpret_cast<const void*>(call.address), call.expected, call.size) != 0)
            return false;
    }
    bool ready = true;
    for (size_t index = 0; index < count; ++index) {
        auto& call = patches[index];
        if (!VirtualProtect(reinterpret_cast<void*>(call.address), call.size,
                            PAGE_EXECUTE_READWRITE, &call.protection)) {
            ready = false;
            break;
        }
    }
    if (ready) {
        g_positionEnabled = position;
        g_animationEnabled = animation;
    }
    for (size_t index = 0; index < count; ++index) {
        const auto& call = patches[index];
        auto* address = reinterpret_cast<unsigned char*>(call.address);
        if (ready) {
            unsigned char replacement[] = {0xE8, 0, 0, 0, 0, 0x90};
            const auto distance = static_cast<uint32_t>(call.replacement - (call.address + 5));
            std::memcpy(replacement + 1, &distance, sizeof(distance));
            std::memcpy(address, replacement, call.size);
            FlushInstructionCache(GetCurrentProcess(), address, call.size);
        }
        DWORD unused = 0;
        if (call.protection != 0) VirtualProtect(address, call.size, call.protection, &unused);
    }
    return ready;
}
}  // namespace remote_player_interpolation
