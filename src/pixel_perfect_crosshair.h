#pragma once

namespace pixel_perfect_crosshair {

// Keeps only the crosshair draw rectangle at the texture's native pixel size.
// Position, texture, color and the rest of the HUD remain under native game control.
bool Install(bool enabled);

}  // namespace pixel_perfect_crosshair
