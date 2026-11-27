#include "../src/remote_player_interpolation.cpp"

#include <cstdio>

int main() {
    using namespace remote_player_interpolation;
    using std::chrono::milliseconds;

    const Identity player{1, 2, 3, 4, 5};
    const Identity respawn{1, 2, 3, 4, 6};
    const Position start{0, 0, 0};
    const Position end{65536, 131072, -65536};
    const Clock::time_point first{milliseconds(1000)};
    const auto second = first + milliseconds(40);

    SnapshotHistory history{};
    history.Observe(player, start, start, first);
    if (history.Remaining(player, start, first) != 0.0f) {
        std::fprintf(stderr, "The first packet must use the native position.\n");
        return 1;
    }

    history.Observe(player, end, start, second);
    const float halfway = history.Remaining(player, end, second + milliseconds(20));
    float bone[16]{};
    bone[0] = 1.0f;
    bone[12] = 10.0f;
    Translate(bone, ToVisualOffset(history, halfway));
    if (halfway != 0.5f || bone[0] != 1.0f || bone[12] != 11.0f ||
        bone[13] != 0.5f || bone[14] != -0.5f ||
        history.Remaining(player, end, second + milliseconds(40)) != 0.0f) {
        std::fprintf(stderr, "Blend timing or render-only translation is incorrect.\n");
        return 1;
    }
    if (history.Remaining(respawn, end, second) != 0.0f ||
        history.Remaining(player, start, second) != 0.0f) {
        std::fprintf(stderr, "A respawn or native position change must invalidate the blend.\n");
        return 1;
    }

    const Position teleport{10 * 65536, 0, 0};
    history.Observe(player, teleport, end, second + milliseconds(40));
    if (history.Remaining(player, teleport, second + milliseconds(40)) != 0.0f) {
        std::fprintf(stderr, "Teleport coordinates must not be interpolated.\n");
        return 1;
    }

    const auto delayed = second + milliseconds(400);
    history.Observe(player, {teleport.x + 1, 0, 0}, teleport, delayed);
    if (history.Remaining(player, history.current, delayed) != 0.0f) {
        std::fprintf(stderr, "A long gap must restart the history.\n");
        return 1;
    }

    const auto next = delayed + milliseconds(150);
    history.Observe(player, teleport, history.current, next);
    if (history.Remaining(player, teleport, next + milliseconds(100)) != 0.0f) {
        std::fprintf(stderr, "The blend must be capped at 100 ms.\n");
        return 1;
    }

    // The actor moves while a limb rotates. The pose blend should use the
    // current actor frame and move the attachment with the blended limb.
    constexpr int bones = kAttachmentBone + 1;
    std::array<float, bones * kMatrixSize> palette{};
    auto* hand = palette.data() + kAttachmentBone * kMatrixSize;
    const Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    Matrix handPose = identity;
    handPose[12] = 1.0f;
    std::copy(handPose.begin(), handPose.end(), hand);
    PoseHistory pose{};
    if (pose.Observe(player, 1, bones, start, 100, first, identity.data(), palette.data()) != 1.0f) {
        std::fprintf(stderr, "The first pose must render without blending.\n");
        return 1;
    }

    Matrix actor = identity;
    actor[14] = 1.0f;
    const Position moved{65536, 0, 0};
    handPose = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 3, 0, 1, 1};
    std::copy(handPose.begin(), handPose.end(), hand);
    Matrix attachment = handPose;
    attachment[13] = 1.0f;
    const auto update = first + milliseconds(16);
    const float begin = pose.Observe(player, 1, bones, moved, 101, update, actor.data(), palette.data());
    const float middle = pose.Observe(player, 1, bones, moved, 101, update + milliseconds(8),
                                      actor.data(), palette.data());
    pose.Apply(middle, actor.data(), palette.data(), attachment.data(), nullptr);
    const float cosine = std::sqrt(0.5f);
    const auto approximatelyEqual = [](float a, float b) { return std::abs(a - b) < 0.0001f; };
    if (begin != 0.0f || middle != 0.5f || !approximatelyEqual(hand[0], cosine) || !approximatelyEqual(hand[1], cosine) ||
        hand[12] != 2.0f || hand[14] != 1.0f ||
        !approximatelyEqual(attachment[12], 2.0f + cosine) || !approximatelyEqual(attachment[13], cosine) ||
        attachment[14] != 1.0f || palette[0] != 0.0f) {
        std::fprintf(stderr, "The pose must preserve actor movement and keep the attachment on its limb.\n");
        return 1;
    }

    std::copy(handPose.begin(), handPose.end(), hand);
    if (pose.Observe(player, 1, bones, moved, 101, update + milliseconds(16),
                     actor.data(), palette.data()) != 1.0f ||
        pose.Observe(player, 1, bones, moved, 103, update + milliseconds(32),
                     actor.data(), palette.data()) != 1.0f ||
        pose.Observe(respawn, 1, bones, moved, 104, update + milliseconds(48),
                     actor.data(), palette.data()) != 1.0f) {
        std::fprintf(stderr, "The pose must stop blending at the update boundary or on a discontinuity.\n");
        return 1;
    }
    return 0;
}
