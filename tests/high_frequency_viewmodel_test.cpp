#include "../src/high_frequency_viewmodel.cpp"

#include <cstdio>
#include <cstdlib>

namespace {
bool rotationEnabled = true;
bool rawClockAvailable = false;
bool positionClockAvailable = false;
constexpr uint32_t kSameSerial = 12;
const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
const float* nativePalette = nullptr;
bool usedNativePalette = false;
float renderedX = 0;

void __cdecl BuildIdentity(float* output, int32_t*) {
    std::memcpy(output, kIdentity, sizeof(kIdentity));
}

void __cdecl Render(void*, const float* palette) {
    usedNativePalette = palette == nativePalette;
    renderedX = palette[12];
}

void Check(bool valid, int line) {
    if (!valid) {
        std::fprintf(stderr, "Viewmodel regression at line %d\n", line);
        std::exit(1);
    }
}
#define CheckThat(condition) Check((condition), __LINE__)
}

namespace camera_position {
bool TryGetSimulationSerial(uint32_t& serial) {
    serial = kSameSerial;
    return positionClockAvailable;
}
uint32_t GetHistoryEpoch() { return 0; }
}

namespace raw_input {
bool TryGetSimulationSerial(uint32_t& serial) {
    serial = kSameSerial;
    return rawClockAvailable;
}
}

namespace high_frequency_camera {
bool IsRotationEnabled() { return rotationEnabled; }
bool GetPresentationCameraPose(int32_t[6], int32_t[6]) { return true; }
}

int main() {
    using namespace high_frequency_viewmodel;
    g_enabled = true;
    g_moduleBase = reinterpret_cast<uintptr_t>(&BuildIdentity) - kBuildPoseMatrixRva;
    int32_t model[5] = {0, 0, 0, 0, 1};
    float palette[16];
    std::memcpy(palette, kIdentity, sizeof(palette));
    nativePalette = palette;

    DispatchViewmodel(Render, model, palette);
    CheckThat(usedNativePalette && renderedX == 0);
    palette[12] = 10;
    DispatchViewmodel(Render, model, palette);
    CheckThat(usedNativePalette && renderedX == 10);

    rawClockAvailable = true;
    DispatchViewmodel(Render, model, palette);
    CheckThat(!usedNativePalette && renderedX == 10);
    rawClockAvailable = false;
    palette[12] = 20;
    DispatchViewmodel(Render, model, palette);
    CheckThat(usedNativePalette && renderedX == 20);
    rawClockAvailable = true;
    DispatchViewmodel(Render, model, palette);
    CheckThat(!usedNativePalette && renderedX == 20);

    positionClockAvailable = true;
    palette[12] = 30;
    DispatchViewmodel(Render, model, palette);
    CheckThat(!usedNativePalette && renderedX == 30);
    rotationEnabled = false;
    rawClockAvailable = false;
    DispatchViewmodel(Render, model, palette);
    CheckThat(!usedNativePalette && renderedX == 30);

    positionClockAvailable = false;
    rawClockAvailable = true;
    DispatchViewmodel(Render, model, palette);
    CheckThat(usedNativePalette && renderedX == 30);
}
