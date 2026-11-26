#pragma once

#include <windows.h>

#include <cstdint>

namespace raw_input {

bool Install(bool enabled);
bool AttachWindow(HWND window);
void HandleRawInput(HRAWINPUT input);
void HandleFocusLost();
bool HandleFocusGained();
void HandleDestroy();
bool GetProjectedNativeMovement(LONG* x, LONG* y);
bool TryGetSimulationSerial(uint32_t& serial);

}  // namespace raw_input
