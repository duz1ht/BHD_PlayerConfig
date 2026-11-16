#include <windows.h>

// Keep checks active in the Release build distributed to the user.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../src/mousescale_fix.cpp"

namespace {
std::vector<std::string> queued;
void __cdecl QueueCommand(char *command) { queued.emplace_back(command); }
}

int main() {
  using mousescale_fix::ClassifyCommand;
  using mousescale_fix::CommandKind;
  using mousescale_fix::StoreMouseScale;

  assert(ClassifyCommand(nullptr) == CommandKind::Other);
  assert(ClassifyCommand("") == CommandKind::Other);
  assert(ClassifyCommand("mousescale") == CommandKind::Query);
  assert(ClassifyCommand("  MOUSESCALE\t") == CommandKind::Query);
  assert(ClassifyCommand("mousescale 25") == CommandKind::Set);
  assert(ClassifyCommand("mousescale\t511") == CommandKind::Set);
  assert(ClassifyCommand("mousescale_extra 25") == CommandKind::Other);
  assert(ClassifyCommand("other mousescale 25") == CommandKind::Other);

  void *config = VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
  assert(config != nullptr);
  auto *stored = reinterpret_cast<int32_t *>(
      reinterpret_cast<uintptr_t>(config) + 0x23f8);
  *stored = 16;
  assert(StoreMouseScale(25, reinterpret_cast<uintptr_t>(config)));
  assert(*stored == 25);
  assert(!StoreMouseScale(0, reinterpret_cast<uintptr_t>(config)));
  assert(!StoreMouseScale(512, reinterpret_cast<uintptr_t>(config)));
  assert(!StoreMouseScale(25, 0));
  assert(*stored == 25);
  // Reproduce native ordering: parsing queues the command, then the executor
  // updates active sensitivity and formats its confirmation on a later update.
  int32_t active = 24;
  uintptr_t profile = reinterpret_cast<uintptr_t>(config);
  mousescale_fix::g_activeScale = &active;
  mousescale_fix::g_profile = &profile;
  mousescale_fix::g_originalParser = QueueCommand;
  mousescale_fix::g_formatter = std::sprintf;
  *stored = 24;
  char command[] = "mousescale 111";
  mousescale_fix::ParseCommand(command);
  assert(queued.back() == "mousescale 111");
  assert(active == 24 && *stored == 24);
  active = 111; // native executor has now applied the queued command
  char message[32] = {};
  assert(mousescale_fix::FormatAppliedScale(message, "%d", active) == 3);
  assert(std::strcmp(message, "111") == 0 && *stored == 111);
  active = 0;
  active = *stored; // native mission initialization: profile -> active value
  assert(active == 111);

  char query[] = "mousescale";
  mousescale_fix::ParseCommand(query);
  assert(queued.back() == "mousescale 111");
  assert(active == 111 && *stored == 111);
  // Persistence uses the final clamped value supplied by the native executor.
  active = 511;
  mousescale_fix::FormatAppliedScale(message, "%d", active);
  assert(*stored == 511);
  // Menu changes still take precedence on the next native profile load.
  *stored = 37;
  active = *stored;
  assert(active == 37);
  VirtualFree(config, 0, MEM_RELEASE);
  return 0;
}
