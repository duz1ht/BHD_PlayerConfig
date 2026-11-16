#include "mousescale_fix.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mousescale_fix {
namespace {
constexpr uintptr_t kParserAddress = 0x00460260;
constexpr uintptr_t kMouseScaleAddress = 0x009F20E4;
constexpr uintptr_t kPlayerConfigAddress = 0x00A34828;
constexpr uintptr_t kStoredMouseScaleOffset = 0x23F8;
constexpr uintptr_t kAppliedFormatCall = 0x00460970;
constexpr uintptr_t kFormatterAddress = 0x005C1C1F;
constexpr size_t kHookLength = 9;
constexpr unsigned char kParserPrologue[kHookLength] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x1C, 0x01, 0x00, 0x00};
constexpr unsigned char kAppliedCall[5] = {0xE8, 0xAA, 0x12, 0x16, 0x00};

using Parser = void(__cdecl *)(char *);
Parser g_originalParser = nullptr;
using Formatter = int(__cdecl *)(char *, const char *, ...);
Formatter g_formatter = reinterpret_cast<Formatter>(kFormatterAddress);
volatile int32_t *g_activeScale = reinterpret_cast<volatile int32_t *>(kMouseScaleAddress);
volatile uintptr_t *g_profile = reinterpret_cast<volatile uintptr_t *>(kPlayerConfigAddress);

enum class CommandKind { Other, Query, Set };

bool IsSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

CommandKind ClassifyCommand(const char *command) {
  if (command == nullptr)
    return CommandKind::Other;
  while (IsSpace(*command))
    ++command;
  constexpr char kName[] = "mousescale";
  for (size_t index = 0; index < sizeof(kName) - 1; ++index) {
    char actual = *command++;
    if (actual >= 'A' && actual <= 'Z')
      actual = static_cast<char>(actual - 'A' + 'a');
    if (actual != kName[index])
      return CommandKind::Other;
  }
  if (*command != '\0' && !IsSpace(*command))
    return CommandKind::Other;
  while (IsSpace(*command))
    ++command;
  return *command == '\0' ? CommandKind::Query : CommandKind::Set;
}

bool IsWritable(const void *address, size_t size) {
  MEMORY_BASIC_INFORMATION memory = {};
  if (VirtualQuery(address, &memory, sizeof(memory)) == 0 ||
      memory.State != MEM_COMMIT ||
      (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    return false;
  const DWORD protection = memory.Protect & 0xff;
  const bool writable = protection == PAGE_READWRITE ||
                        protection == PAGE_WRITECOPY ||
                        protection == PAGE_EXECUTE_READWRITE ||
                        protection == PAGE_EXECUTE_WRITECOPY;
  return writable && reinterpret_cast<uintptr_t>(address) + size <=
      reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
}

bool StoreMouseScale(int32_t scale, uintptr_t playerConfig) {
  if (scale < 1 || scale > 511 || playerConfig == 0)
    return false;
  auto *stored = reinterpret_cast<volatile int32_t *>(
      playerConfig + kStoredMouseScaleOffset);
  if (!IsWritable(const_cast<int32_t *>(stored), sizeof(*stored)))
    return false;
  *stored = scale;
  return true;
}

// Called only by the mousescale executor, after its 1..511 clamp and active
// sensitivity write. The console parser merely queues work and is too early.
int __cdecl FormatAppliedScale(char *output, const char *format, int scale) {
  StoreMouseScale(scale, *g_profile);
  return g_formatter(output, format, scale);
}

void __cdecl ParseCommand(char *command) {
  const CommandKind kind = ClassifyCommand(command);
  if (kind == CommandKind::Query) {
    const int32_t scale = *g_activeScale;
    if (scale >= 1 && scale <= 511) {
      char query[32] = {};
      std::snprintf(query, sizeof(query), "mousescale %d", scale);
      g_originalParser(query);
      return;
    }
  }

  g_originalParser(command);
}

bool WriteBranch(unsigned char *output, const void *source,
                 const void *destination, unsigned char opcode) {
  // x86 relative branches use modulo-2^32 displacement, including high DLL VAs.
  const uint32_t displacement = static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(destination) - reinterpret_cast<uintptr_t>(source) - 5);
  output[0] = opcode;
  const uint32_t relative = displacement;
  std::memcpy(output + 1, &relative, sizeof(relative));
  return true;
}

bool WriteCode(void *address, const unsigned char *code, size_t size) {
  DWORD previousProtection = 0;
  if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE,
                      &previousProtection))
    return false;
  std::memcpy(address, code, size);
  FlushInstructionCache(GetCurrentProcess(), address, size);
  DWORD ignored = 0;
  VirtualProtect(address, size, previousProtection, &ignored);
  return true;
}
} // namespace

bool Install(bool enabled) {
  if (!enabled || g_originalParser != nullptr)
    return false;

  auto *parser = reinterpret_cast<unsigned char *>(kParserAddress);
  auto *appliedCall = reinterpret_cast<unsigned char *>(kAppliedFormatCall);
  if (std::memcmp(parser, kParserPrologue, kHookLength) != 0 ||
      std::memcmp(appliedCall, kAppliedCall, sizeof(kAppliedCall)) != 0)
    return false;

  auto *parserTrampoline = static_cast<unsigned char *>(VirtualAlloc(
      nullptr, kHookLength + 5, MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (parserTrampoline == nullptr) {
    return false;
  }

  std::memcpy(parserTrampoline, parser, kHookLength);
  unsigned char parserPatch[kHookLength] = {0xE9};
  std::memset(parserPatch + 5, 0x90, kHookLength - 5);
  if (!WriteBranch(parserTrampoline + kHookLength,
                   parserTrampoline + kHookLength, parser + kHookLength, 0xE9) ||
      !WriteBranch(parserPatch, parser,
                   reinterpret_cast<const void *>(&ParseCommand), 0xE9)) {
    VirtualFree(parserTrampoline, 0, MEM_RELEASE);
    return false;
  }

  unsigned char appliedPatch[5];
  if (!WriteBranch(appliedPatch, appliedCall,
                   reinterpret_cast<const void *>(&FormatAppliedScale), 0xE8) ||
      !WriteCode(appliedCall, appliedPatch, sizeof(appliedPatch))) {
    VirtualFree(parserTrampoline, 0, MEM_RELEASE);
    return false;
  }

  FlushInstructionCache(GetCurrentProcess(), parserTrampoline, kHookLength + 5);
  g_originalParser = reinterpret_cast<Parser>(parserTrampoline);
  if (!WriteCode(parser, parserPatch, sizeof(parserPatch))) {
    WriteCode(appliedCall, kAppliedCall, sizeof(kAppliedCall));
    g_originalParser = nullptr;
    VirtualFree(parserTrampoline, 0, MEM_RELEASE);
    return false;
  }
  return true;
}
} // namespace mousescale_fix
