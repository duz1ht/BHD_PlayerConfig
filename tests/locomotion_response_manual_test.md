# Responsive movement direction: in-game validation

Automated tests cover hook installation, direction mapping and excluded states.
Gameplay and multiplayer measurements remain pending.

Use the same executable, map, posture, FPS cap and camera settings for both runs.
Restart the game after changing the option.

| Run | ResponsiveMovementDirection |
|---|---|
| Baseline | 0 |
| Earlier direction selection | 1 |

Record ADAD, clockwise and counterclockwise WASD circles, sustained straight and
diagonal movement, and reversals near walls. Repeat in each supported posture.
Compare response latency, distance and visible discontinuities.

Confirm that starting, stopping, posture changes, jumping, landing, ladders, mounted
movement, death and special actions retain native behavior. The feature deliberately
preserves the game's original animation transition durations.

Validate multiplayer separately by comparing client and observer trajectories,
corrections, hit registration and distance before enabling the option by default.
