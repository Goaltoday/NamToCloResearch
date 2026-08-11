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

    auto readLe32 = [](const std::vector<std::uint8_t>& data, std::size_t offset) -> std::uint32_t {
        if (offset + 4 > data.size()) return 0;
        return static_cast<std::uint32_t>(data[offset])
             | (static_cast<std::uint32_t>(data[offset + 1]) << 8)
             | (static_cast<std::uint32_t>(data[offset + 2]) << 16)
             | (static_cast<std::uint32_t>(data[offset + 3]) << 24);
    };

    std::vector<std::uint8_t> header(0x88);
    stream.clear();
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    header.resize(static_cast<std::size_t>(stream.gcount()));
    info.declaredSize = readLe32(header, 0x04);
    info.payloadSize = readLe32(header, 0x14);
    info.modelField = readLe32(header, 0x84);

    stream.clear();
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> all(static_cast<std::size_t>(info.size));
    if (!all.empty()) {
        stream.read(reinterpret_cast<char*>(all.data()), static_cast<std::streamsize>(all.size()));
        const auto count = static_cast<std::size_t>(stream.gcount());
        for (std::size_t i = count; i > 0; --i) {
            if (all[i - 1] != 0) {
                info.lastNonZero = static_cast<std::uint64_t>(i - 1);
                info.hasLastNonZero = true;
                break;
            }
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
    if (info.magic == "VTSI") {
        std::cout << "  declared-size @0x04: 0x" << std::uppercase << std::hex << info.declaredSize << std::dec << "\n";
        std::cout << "  payload-size  @0x14: 0x" << std::uppercase << std::hex << info.payloadSize << std::dec << "\n";
        std::cout << "  model-field   @0x84: 0x" << std::uppercase << std::hex << info.modelField << std::dec << "\n";
        if (info.hasLastNonZero) {
            std::cout << "  last-nonzero:        0x" << std::uppercase << std::hex << info.lastNonZero << std::dec << "\n";
        } else {
            std::cout << "  last-nonzero:        none\n";
        }
    }
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


static std::uint16_t crc16Modbus(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? static_cast<std::uint16_t>((crc >> 1) ^ 0xA001u)
                             : static_cast<std::uint16_t>(crc >> 1);
        }
    }
    return crc;
}

bool makeGp200CompactClo(const fs::path& source, const fs::path& destination, std::string& error) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        error = "Cannot open source CLO: " + pathToUtf8(source);
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(kExpectedCloSize));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (static_cast<std::size_t>(in.gcount()) != data.size()) {
        error = "Source CLO is not exactly 0x2288 bytes.";
        return false;
    }
    if (!(data[0] == 'V' && data[1] == 'T' && data[2] == 'S' && data[3] == 'I')) {
        error = "Source CLO magic is not VTSI.";
        return false;
    }
    auto readLe32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(data[off])
             | (static_cast<std::uint32_t>(data[off+1]) << 8)
             | (static_cast<std::uint32_t>(data[off+2]) << 16)
             | (static_cast<std::uint32_t>(data[off+3]) << 24);
    };
    auto writeLe32 = [&](std::size_t off, std::uint32_t v) {
        data[off] = static_cast<std::uint8_t>(v & 0xFFu);
        data[off+1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        data[off+2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        data[off+3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };

    // The compact GP-200 shape inferred from same-NAM Valeton captures is:
    // 0x88-byte header + 128 float32 values + 1024 float32 values = 0x1288 useful bytes.
    // The Ampero normal VTSI uses the same offsets but serializes 2048 values in block B.
    if (readLe32(0x84) != 0x800u) {
        error = "Expected normal Ampero model-field 0x800 before compact serialization.";
        return false;
    }

    writeLe32(0x04, 0x1288u);
    writeLe32(0x14, 0x1200u);
    writeLe32(0x84, 0x0400u);
    std::fill(data.begin() + 0x1288, data.end(), 0u);

    // HTUSBTools stores CRC16/MODBUS with bytes swapped, over [0x0C, declaredSize).
    const std::uint16_t crc = crc16Modbus(data.data() + 0x0C, 0x1288u - 0x0Cu);
    data[0x08] = static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
    data[0x09] = static_cast<std::uint8_t>(crc & 0xFFu);

    std::error_code ec;
    if (destination.has_parent_path()) {
        fs::create_directories(destination.parent_path(), ec);
        if (ec) {
            error = "Cannot create compact CLO output directory: " + ec.message();
            return false;
        }
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "Cannot create compact CLO: " + pathToUtf8(destination);
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        error = "Failed writing compact CLO: " + pathToUtf8(destination);
        return false;
    }
    return true;
}

} // namespace ntc
