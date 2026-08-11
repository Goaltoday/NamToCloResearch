#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ntc {

namespace fs = std::filesystem;

inline constexpr std::uint64_t kExpectedCloSize = 0x2288;
inline constexpr std::uint32_t kExpectedApiReturn = 0x2288;
inline constexpr wchar_t kVersion[] = L"0.6.0";

struct CloInfo {
    bool exists = false;
    std::uint64_t size = 0;
    std::vector<std::uint8_t> prefix;
    std::string magic;
    std::uint32_t declaredSize = 0;
    std::uint32_t payloadSize = 0;
    std::uint32_t modelField = 0;
    std::uint64_t lastNonZero = 0;
    bool hasLastNonZero = false;
};

std::string toUtf8(const std::wstring& value);
std::wstring fromUtf8(const std::string& value);
std::string pathToUtf8(const fs::path& path);
std::wstring quoteWindowsArg(const std::wstring& arg);
fs::path executablePath();
std::string win32ErrorMessage(std::uint32_t code);
std::string hex32(std::uint32_t value);
std::string hexBytes(const std::vector<std::uint8_t>& bytes);
CloInfo inspectClo(const fs::path& path, std::size_t prefixBytes = 16);
void printCloInfo(const fs::path& path, const CloInfo& info);
bool copyFileCreatingParents(const fs::path& source, const fs::path& destination, std::string& error);
bool makeGp200CompactClo(const fs::path& source, const fs::path& destination, std::string& error);

} // namespace ntc
