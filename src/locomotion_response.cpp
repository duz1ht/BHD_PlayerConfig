#include "locomotion_response.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace locomotion_response {
namespace {
uintptr_t g_base = 0;
bool g_attempted = false, g_enabled = false;
using Evaluate = void (__cdecl*)(void*, uintptr_t, uintptr_t);
Evaluate g_evaluate = nullptr;
uintptr_t Address(uintptr_t va) { return g_base + va - 0x400000; }
template<class T> T Read(uintptr_t address) {
    T value; std::memcpy(&value, reinterpret_cast<void*>(address), sizeof(value)); return value;
}
void SetState(uintptr_t actor, uint32_t value) {
    std::memcpy(reinterpret_cast<void*>(actor + 0x164), &value, sizeof(value));
}
uint32_t Family(uint32_t state) {
    for (uint32_t base : {0x24u, 0x34u, 0x3cu})
        if (state >= base && state < base + 8) return base;
    return 0;
}
bool Eligible(uintptr_t actor) {
    if (!actor || actor != Read<uintptr_t>(Address(0x96C290)) ||
        Read<uint32_t>(Address(0x7C72BC)) != 0) return false;
    // Native grounded branch plus exclusions for mount, queued/special actions,
    // death and a change of posture. Do not accelerate entry/exit from locomotion.
    const uintptr_t detail = Read<uintptr_t>(actor + 0x38);
    if (!detail || (Read<uint32_t>(actor + 0x20) & 0x422004) ||
        (Read<uint8_t>(detail + 0x34) & 2) || Read<uintptr_t>(actor + 0x13c) ||
        Read<uint32_t>(actor + 0x160) || Read<uint8_t>(actor + 0x210) ||
        Read<int16_t>(actor + 0xe2) <= 0) return false;
    const uint32_t command = Read<uint32_t>(actor + 0x100);
    if (((command >> 19) & 3) == 0) return false;
    const uint32_t desiredFamily = (command & 0x20) ? 0x3c : ((command & 0x40) ? 0x34 : 0x24);
    return Family(Read<uint32_t>(actor + 0x164)) == desiredFamily;
}
void __cdecl EvaluateHook(void* output, uintptr_t animation, uintptr_t actor) {
    if (g_enabled && animation && Eligible(actor)) {
        const uint32_t direction = Read<uint32_t>(actor + 0x100) & 7;
        const uint32_t base = Family(Read<uint32_t>(actor + 0x164));
        SetState(actor, base + ((8 - direction) & 7));
    }
    g_evaluate(output, animation, actor);
}
struct Hook {
    uintptr_t va;
    unsigned char original[5];
    uintptr_t target;
};
bool Matches(uintptr_t va, const unsigned char* bytes, size_t size) {
    MEMORY_BASIC_INFORMATION info = {};
    const uintptr_t address = Address(va);
    if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        address + size > reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
    return std::memcmp(reinterpret_cast<void*>(address), bytes, size) == 0;
}
bool Write(uintptr_t va, const unsigned char* bytes) {
    void* address = reinterpret_cast<void*>(Address(va));
    DWORD old = 0;
    if (!VirtualProtect(address, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(address, bytes, 5);
    FlushInstructionCache(GetCurrentProcess(), address, 5);
    DWORD ignored = 0;
    VirtualProtect(address, 5, old, &ignored);
    return true;
}
} // namespace
bool Install(bool enabled) {
    if (!enabled) return false;
    if (g_attempted) return g_enabled;
    g_attempted = true;
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!g_base) return false;
    Hook hook = {0x47cb31, {0xe8,0xfa,0x73,0xf9,0xff},
                 reinterpret_cast<uintptr_t>(&EvaluateHook)};
    const unsigned char evaluate[] = {0x55,0x8b,0xec,0x83,0xec,0x50,0x53,0x8b,0x5d,0x10};
    const unsigned char soldier[] = {0x55,0x8b,0xec,0x81,0xec,0xbc,0,0,0,0x53};
    if (!Matches(0x413f30, evaluate, sizeof(evaluate)) ||
        !Matches(0x47ca90, soldier, sizeof(soldier))) return false;
    if (!Matches(hook.va, hook.original, 5)) return false;
    g_evaluate = reinterpret_cast<Evaluate>(Address(0x413f30));
    unsigned char replacement[5] = {0xe8};
    const uint32_t delta = static_cast<uint32_t>(hook.target - Address(hook.va) - 5);
    std::memcpy(replacement + 1, &delta, 4);
    if (!Write(hook.va, replacement)) return false;
    g_enabled = true;
    return true;
}
} // namespace locomotion_response
