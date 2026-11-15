#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pff_archive {

bool LoadEntry(const std::wstring& archivePath, const std::string& entryName,
               std::vector<uint8_t>& data, std::wstring& error);

bool ReplaceEntry(const std::wstring& archivePath, const std::string& entryName,
                  const std::vector<uint8_t>& replacement, std::wstring& error);

}  // namespace pff_archive
