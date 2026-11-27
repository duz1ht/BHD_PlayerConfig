#pragma once

#include "camera_position.h"

namespace camera_fov {

bool Install(bool enabled, bool highFrequencyCameraEnabled,
             camera_position::Mode cameraPositionMode);

} // namespace camera_fov
