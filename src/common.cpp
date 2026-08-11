#include "common.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace ntc {

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

std::string pathToUtf8(const fs::path& path) {
    return toUtf8(path.wstring());
}

std::wstring quoteWindowsArg(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }
    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
    }

    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(ch);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

fs::path executablePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0 || len >= buffer.size()) {
        return {};
    }
    buffer.resize(len);
    return fs::path(buffer);
}

std::string win32ErrorMessage(const std::uint32_t code) {
    LPWSTR raw = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD count = FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&raw), 0, nullptr);
    if (count == 0 || raw == nullptr) {
        return "Win32 error " + std::to_string(code);
    }
    std::wstring message(raw, count);
    LocalFree(raw);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return toUtf8(message);
}

std::string hex32(const std::uint32_t value) {
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return os.str();
}

std::string hexBytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream os;
    os << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            os << ' ';
        }
        os << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return os.str();
}

CloInfo inspectClo(const fs::path& path, const std::size_t prefixBytes) {
    CloInfo info;
    std::error_code ec;
    info.exists = fs::exists(path, ec) && !ec;
    if (!info.exists) {
        return info;
    }
    info.size = fs::file_size(path, ec);
    if (ec) {
        info.size = 0;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return info;
    }
    info.prefix.resize(prefixBytes);
    stream.read(reinterpret_cast<char*>(info.prefix.data()), static_cast<std::streamsize>(info.prefix.size()));
    info.prefix.resize(static_cast<std::size_t>(stream.gcount()));

    const std::size_t magicLen = std::min<std::size_t>(4, info.prefix.size());
    info.magic.assign(reinterpret_cast<const char*>(info.prefix.data()), magicLen);
    for (char& ch : info.magic) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 0x20 || uch > 0x7E) {
            ch = '.';
        }
    }
    return info;
}

void printCloInfo(const fs::path& path, const CloInfo& info) {
    std::cout << "CLO: " << pathToUtf8(path) << "\n";
    std::cout << "  exists: " << (info.exists ? "yes" : "no") << "\n";
    if (!info.exists) {
        return;
    }
    std::cout << "  size:   " << info.size << " bytes (0x" << std::uppercase << std::hex << info.size
              << std::dec << ")\n";
    std::cout << "  magic:  " << info.magic << "\n";
    std::cout << "  prefix: " << hexBytes(info.prefix) << "\n";
    std::cout << "  expected-size: " << (info.size == kExpectedCloSize ? "yes" : "NO") << "\n";
}

bool copyFileCreatingParents(const fs::path& source, const fs::path& destination, std::string& error) {
    std::error_code ec;
    if (destination.has_parent_path()) {
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            error = "Cannot create output directory: " + ec.message();
            return false;
        }
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = "Cannot copy '" + pathToUtf8(source) + "' to '" + pathToUtf8(destination) + "': " + ec.message();
        return false;
    }
    return true;
}

} // namespace ntc
