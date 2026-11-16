#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
uintptr_t image = 0;
bool failWrite = false;
HMODULE WINAPI TestModule(LPCWSTR) { return reinterpret_cast<HMODULE>(image); }
BOOL WINAPI TestProtect(LPVOID p, SIZE_T n, DWORD mode, PDWORD old) {
    if (failWrite && reinterpret_cast<uintptr_t>(p) == image + 0x7cb31) return FALSE;
    return VirtualProtect(p, n, mode, old);
}
}
#define GetModuleHandleW TestModule
#define VirtualProtect TestProtect
#include "../src/locomotion_response.cpp"
#undef VirtualProtect
#undef GetModuleHandleW

namespace {
void Require(bool ok, int line) {
    if (!ok) { std::fprintf(stderr, "locomotion regression line %d\n", line); std::exit(1); }
}
#define Check(x) Require((x), __LINE__)
template<class T> void Put(uintptr_t address, T value) {
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
}
void Seed(uintptr_t va, std::initializer_list<unsigned char> bytes) {
    std::memcpy(reinterpret_cast<void*>(image + va - 0x400000), bytes.begin(), bytes.size());
}
int evaluations = 0;
uint32_t observedState = 0;
void __cdecl FakeEvaluate(void* output, uintptr_t, uintptr_t actor) {
    ++evaluations;
    observedState = locomotion_response::Read<uint32_t>(actor + 0x164);
    *static_cast<int*>(output) = 42;
}
}

int main(int argc, char** argv) {
    using namespace locomotion_response;
    image = reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr, 0x600000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(image != 0);
    Seed(0x47cb31, {0xe8,0xfa,0x73,0xf9,0xff});
    Seed(0x413f30, {0x55,0x8b,0xec,0x83,0xec,0x50,0x53,0x8b,0x5d,0x10});
    Seed(0x47ca90, {0x55,0x8b,0xec,0x81,0xec,0xbc,0,0,0,0x53});
    if (argc > 1 && std::strcmp(argv[1], "signature") == 0) {
        Seed(0x413f30, {0x90});
        Check(!Install(true));
        Check(Read<uint32_t>(image + 0x7cb32) == 0xfff973fa);
        return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "write_failure") == 0) {
        failWrite = true;
        Check(!Install(true));
        Check(!g_enabled);
        Check(Read<uint32_t>(image + 0x7cb32) == 0xfff973fa);
        return 0;
    }
    Check(!Install(false));
    Check(Install(true));
    Check(image + 0x7cb36 + Read<uint32_t>(image + 0x7cb32) ==
          reinterpret_cast<uintptr_t>(&EvaluateHook));
    g_evaluate = FakeEvaluate;

    unsigned char actorBytes[0x29c] = {}, detailBytes[0x40] = {}, animationBytes[0x54] = {};
    const uintptr_t actor = reinterpret_cast<uintptr_t>(actorBytes);
    const uintptr_t detail = reinterpret_cast<uintptr_t>(detailBytes);
    const uintptr_t animation = reinterpret_cast<uintptr_t>(animationBytes);
    Put(image + 0x56c290, actor);
    Put(actor + 0x38, detail);
    Put<int16_t>(actor + 0xe2, 100);
    int output = 0;
    for (uint32_t base : {0x24u,0x34u,0x3cu}) {
        for (uint32_t direction = 0; direction < 8; ++direction) {
            const uint32_t posture = base == 0x34 ? 0x40 : base == 0x3c ? 0x20 : 0;
            Put<uint32_t>(actor + 0x100, (1u << 19) | posture | direction);
            Put<uint32_t>(actor + 0x164, base);
            EvaluateHook(&output, animation, actor);
            Check(observedState == base + ((8 - direction) & 7));
        }
    }
    Check(output == 42 && evaluations == 24);
    for (int scenario = 0; scenario < 11; ++scenario) {
        std::memset(actorBytes, 0, sizeof(actorBytes));
        std::memset(detailBytes, 0, sizeof(detailBytes));
        Put(image + 0x56c290, actor);
        Put<uint32_t>(image + 0x3c72bc, 0);
        Put(actor + 0x38, detail);
        Put<int16_t>(actor + 0xe2, 100);
        Put<uint32_t>(actor + 0x100, (1u << 19) | 2);
        Put<uint32_t>(actor + 0x164, 0x26);
        switch (scenario) {
        case 0: Put<uintptr_t>(image + 0x56c290, 0); break;
        case 1: Put<uint32_t>(actor + 0x20, 0x2000); break;
        case 2: Put<uintptr_t>(actor + 0x13c, 1); break;
        case 3: Put<uint32_t>(actor + 0x160, 1); break;
        case 4: Put<uint8_t>(actor + 0x210, 1); break;
        case 5: Put<int16_t>(actor + 0xe2, 0); break;
        case 6: Put<uint8_t>(detail + 0x34, 2); break;
        case 7: Put<uint32_t>(actor + 0x100, 2); break;
        case 8: Put<uint32_t>(actor + 0x100, (1u << 19) | 0x20 | 2); break;
        case 9: Put<uint32_t>(image + 0x3c72bc, 1); break;
        case 10: Put<uintptr_t>(actor + 0x38, 0); break;
        }
        EvaluateHook(&output, animation, actor);
        Check(observedState == 0x26);
    }
    VirtualFree(reinterpret_cast<void*>(image), 0, MEM_RELEASE);
    return 0;
}
