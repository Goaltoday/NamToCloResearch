#include "common.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <limits>
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


static constexpr std::array<std::uint8_t,256> kCrcLoOfficial = {
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
    0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
};
static constexpr std::array<std::uint8_t,256> kCrcHiOfficial = {
    0x00,0xc0,0xc1,0x01,0xc3,0x03,0x02,0xc2,0xc6,0x06,0x07,0xc7,0x05,0xc5,0xc4,0x04,
    0xcc,0x0c,0x0d,0xcd,0x0f,0xcf,0xce,0x0e,0x0a,0xca,0xcb,0x0b,0xc9,0x09,0x08,0xc8,
    0xd8,0x18,0x19,0xd9,0x1b,0xdb,0xda,0x1a,0x1e,0xde,0xdf,0x1f,0xdd,0x1d,0x1c,0xdc,
    0x14,0xd4,0xd5,0x15,0xd7,0x17,0x16,0xd6,0xd2,0x12,0x13,0xd3,0x11,0xd1,0xd0,0x10,
    0xf0,0x30,0x31,0xf1,0x33,0xf3,0xf2,0x32,0x36,0xf6,0xf7,0x37,0xf5,0x35,0x34,0xf4,
    0x3c,0xfc,0xfd,0x3d,0xff,0x3f,0x3e,0xfe,0xfa,0x3a,0x3b,0xfb,0x39,0xf9,0xf8,0x38,
    0x28,0xe8,0xe9,0x29,0xeb,0x2b,0x2a,0xea,0xee,0x2e,0x2f,0xef,0x2d,0xed,0xec,0x2c,
    0xe4,0x24,0x25,0xe5,0x27,0xe7,0xe6,0x26,0x22,0xe2,0xe3,0x23,0xe1,0x21,0x20,0xe0,
    0xa0,0x60,0x61,0xa1,0x63,0xa3,0xa2,0x62,0x66,0xa6,0xa7,0x67,0xa5,0x65,0x64,0xa4,
    0x6c,0xac,0xad,0x6d,0xaf,0x6f,0x6e,0xae,0xaa,0x6a,0x6b,0xab,0x69,0xa9,0xa8,0x68,
    0x78,0xb8,0xb9,0x79,0xbb,0x7b,0x7a,0xba,0xbe,0x7e,0x7f,0xbf,0x7d,0xbd,0xbc,0x7c,
    0xb4,0x74,0x75,0xb5,0x77,0xb7,0xb6,0x76,0x72,0xb2,0xb3,0x73,0xb1,0x71,0x70,0xb0,
    0x50,0x90,0x91,0x51,0x93,0x53,0x52,0x92,0x96,0x56,0x57,0x97,0x55,0x95,0x94,0x54,
    0x9c,0x5c,0x5d,0x9d,0x5f,0x9f,0x9e,0x5e,0x5a,0x9a,0x9b,0x5b,0x99,0x59,0x58,0x98,
    0x88,0x48,0x49,0x89,0x4b,0x8b,0x8a,0x4a,0x4e,0x8e,0x8f,0x4f,0x8d,0x4d,0x4c,0x8c,
    0x44,0x84,0x85,0x45,0x87,0x47,0x46,0x86,0x82,0x42,0x43,0x83,0x41,0x81,0x80,0x40,
};
static std::uint16_t crc16Gp200Official(const std::uint8_t* p, std::size_t n) {
    // GP-200.exe 0x553150/0x553190.  dl/bl both start at 0xff.  The two
    // 256-byte tables are copied verbatim from VA 0x7722d0/0x7723d0.
    std::uint8_t lo=0xff,hi=0xff;
    for(std::size_t i=0;i<n;++i){
        const std::uint8_t idx=static_cast<std::uint8_t>(p[i]^lo);
        lo=static_cast<std::uint8_t>(kCrcLoOfficial[idx]^hi);
        hi=kCrcHiOfficial[idx];
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(lo)<<8)|hi);
}

bool makeGp200CompactClo(const fs::path& source, const fs::path& destination, std::string& error) {
    // Literal data path of GP-200.exe 0x4818f0.  The original routine receives
    // an already-loaded VTSI object; here the file is only the transport for
    // that same byte buffer.
    std::ifstream in(source, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Cannot open source CLO: " + pathToUtf8(source);
        return false;
    }
    const auto endPos=in.tellg();
    if(endPos<0){error="Cannot determine source CLO size.";return false;}
    std::vector<std::uint8_t> src(static_cast<std::size_t>(endPos));
    in.seekg(0,std::ios::beg);
    if(!src.empty())in.read(reinterpret_cast<char*>(src.data()),static_cast<std::streamsize>(src.size()));
    if(static_cast<std::size_t>(in.gcount())!=src.size() || src.size()<0x88u){
        error="Source CLO is truncated.";
        return false;
    }
    auto read32=[&](std::size_t off)->std::uint32_t{
        return static_cast<std::uint32_t>(src[off])
             | (static_cast<std::uint32_t>(src[off+1])<<8)
             | (static_cast<std::uint32_t>(src[off+2])<<16)
             | (static_cast<std::uint32_t>(src[off+3])<<24);
    };
    const std::uint32_t declared=read32(0x04);
    if(declared<0x0cu || declared>src.size()){
        error="Source CLO declared size is invalid.";
        return false;
    }

    // 0x481929 calls 0x553150 before doing anything else and compares AX with
    // WORD [src+8].  0x553150 derives its byte count from DWORD [src+4].
    const std::uint16_t sourceCrc=crc16Gp200Official(src.data()+0x0c,declared-0x0cu);
    const std::uint16_t stored=static_cast<std::uint16_t>(src[0x08])
                             | (static_cast<std::uint16_t>(src[0x09])<<8);
    if(sourceCrc!=stored){
        error="Source CLO CRC is invalid.";
        return false;
    }

    // 0x481971/0x48197b: this compact path is accepted only when both format
    // flag bytes are zero.
    if(src[0x12]!=0 || src[0x13]!=0){
        error="Source CLO format flags are not accepted by the GP-200 compact path.";
        return false;
    }

    std::vector<std::uint8_t> dst(static_cast<std::size_t>(kExpectedCloSize),0);
    std::memcpy(dst.data(),src.data(),0x88u); // exact 0x88-byte header copy

    const std::uint32_t sourcePayload=read32(0x14);
    const std::size_t availablePayload=src.size()>0x88u?src.size()-0x88u:0u;
    const std::size_t copyPayload=std::min<std::size_t>(
        0x1200u,std::min<std::size_t>(sourcePayload,availablePayload));
    if(copyPayload)std::memcpy(dst.data()+0x88u,src.data()+0x88u,copyPayload);

    auto write32=[&](std::size_t off,std::uint32_t v){
        dst[off]=static_cast<std::uint8_t>(v);
        dst[off+1]=static_cast<std::uint8_t>(v>>8);
        dst[off+2]=static_cast<std::uint8_t>(v>>16);
        dst[off+3]=static_cast<std::uint8_t>(v>>24);
    };
    write32(0x14,0x1200u);
    const std::uint32_t sourceB=read32(0x84);
    write32(0x84,std::min<std::uint32_t>(sourceB,0x400u));
    write32(0x04,0x1288u);

    // 0x481a18 -> 0x553150, followed by MOV WORD PTR [dst+8],AX.
    const std::uint16_t crc=crc16Gp200Official(dst.data()+0x0c,0x1288u-0x0cu);
    dst[0x08]=static_cast<std::uint8_t>(crc);
    dst[0x09]=static_cast<std::uint8_t>(crc>>8);

    std::error_code ec;
    if(destination.has_parent_path()){
        fs::create_directories(destination.parent_path(),ec);
        if(ec){error="Cannot create compact CLO output directory: "+ec.message();return false;}
    }
    std::ofstream out(destination,std::ios::binary|std::ios::trunc);
    if(!out){error="Cannot create compact CLO: "+pathToUtf8(destination);return false;}
    out.write(reinterpret_cast<const char*>(dst.data()),static_cast<std::streamsize>(dst.size()));
    if(!out){error="Failed writing compact CLO: "+pathToUtf8(destination);return false;}
    return true;
}

static bool readWholeFile(const fs::path& path, std::vector<std::uint8_t>& data, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        error = "Cannot open file: " + pathToUtf8(path);
        return false;
    }
    const auto end = in.tellg();
    if (end < 0) {
        error = "Cannot determine file size: " + pathToUtf8(path);
        return false;
    }
    data.resize(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (static_cast<std::size_t>(in.gcount()) != data.size()) {
            error = "Short read: " + pathToUtf8(path);
            return false;
        }
    }
    return true;
}

static std::uint32_t readLe32Raw(const std::vector<std::uint8_t>& data, std::size_t off) {
    if (off + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[off])
         | (static_cast<std::uint32_t>(data[off + 1]) << 8)
         | (static_cast<std::uint32_t>(data[off + 2]) << 16)
         | (static_cast<std::uint32_t>(data[off + 3]) << 24);
}

static float readLeFloatRaw(const std::vector<std::uint8_t>& data, std::size_t off) {
    const std::uint32_t bits = readLe32Raw(data, off);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static BlockCompareStats compareFloatBlock(const std::vector<std::uint8_t>& a,
                                           const std::vector<std::uint8_t>& b,
                                           std::size_t offset,
                                           std::size_t count) {
    BlockCompareStats out;
    if (offset + count * 4 > a.size() || offset + count * 4 > b.size()) {
        return out;
    }
    out.count = count;
    long double sumA = 0.0L, sumB = 0.0L;
    long double sumAA = 0.0L, sumBB = 0.0L, sumAB = 0.0L;
    long double sumAbs = 0.0L, sumSq = 0.0L;
    double maxAbs = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t off = offset + i * 4;
        const std::uint32_t bitsA = readLe32Raw(a, off);
        const std::uint32_t bitsB = readLe32Raw(b, off);
        if (bitsA == bitsB) ++out.exactFloatMatches;
        const double va = static_cast<double>(readLeFloatRaw(a, off));
        const double vb = static_cast<double>(readLeFloatRaw(b, off));
        const double d = va - vb;
        const double ad = std::abs(d);
        maxAbs = std::max(maxAbs, ad);
        sumA += va; sumB += vb;
        sumAA += va * va; sumBB += vb * vb; sumAB += va * vb;
        sumAbs += ad; sumSq += d * d;
    }
    const long double n = static_cast<long double>(count);
    const long double covN = n * sumAB - sumA * sumB;
    const long double varAN = n * sumAA - sumA * sumA;
    const long double varBN = n * sumBB - sumB * sumB;
    const long double denom = std::sqrt(std::max(0.0L, varAN * varBN));
    out.correlation = denom > std::numeric_limits<long double>::epsilon()
        ? static_cast<double>(covN / denom) : 0.0;
    out.mae = static_cast<double>(sumAbs / n);
    out.rmse = std::sqrt(static_cast<double>(sumSq / n));
    out.maxAbsError = maxAbs;
    return out;
}

static void crcInfo(const std::vector<std::uint8_t>& data, bool& valid,
                    std::uint16_t& stored, std::uint16_t& calculated) {
    valid = false; stored = 0; calculated = 0;
    if (data.size() < 0x0C) return;
    const std::uint32_t declared = readLe32Raw(data, 0x04);
    if (declared < 0x0C || declared > data.size()) return;
    stored = static_cast<std::uint16_t>(data[0x08] | (static_cast<std::uint16_t>(data[0x09]) << 8));
    calculated = crc16Gp200Official(data.data() + 0x0C, declared - 0x0C);
    valid = stored == calculated;
}

Gp200CompareResult compareGp200Clo(const fs::path& aPath, const fs::path& bPath) {
    Gp200CompareResult result;
    result.a = inspectClo(aPath, 32);
    result.b = inspectClo(bPath, 32);
    std::vector<std::uint8_t> a, b;
    if (!readWholeFile(aPath, a, result.error)) return result;
    if (!readWholeFile(bPath, b, result.error)) return result;
    if (a.size() != kExpectedCloSize || b.size() != kExpectedCloSize) {
        result.error = "Both files must be exactly 0x2288 bytes.";
        return result;
    }
    if (a.size() < 4 || b.size() < 4 || std::memcmp(a.data(), "VTSI", 4) != 0 || std::memcmp(b.data(), "VTSI", 4) != 0) {
        result.error = "Both files must have VTSI magic.";
        return result;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) ++result.byteMatches;
        else ++result.byteDifferences;
    }
    constexpr std::size_t usefulEnd = 0x1288;
    for (std::size_t i = 0; i < usefulEnd; ++i) {
        if (a[i] != b[i]) ++result.usefulByteDifferences;
    }
    for (std::size_t i = usefulEnd; i < a.size(); ++i) {
        if (a[i] != b[i]) ++result.paddingByteDifferences;
    }
    crcInfo(a, result.crcAValid, result.storedCrcA, result.calculatedCrcA);
    crcInfo(b, result.crcBValid, result.storedCrcB, result.calculatedCrcB);
    // GP-200.exe 0x4818f0 compact layout: A is 128 float32 at 0x88 and B
    // starts immediately after A at 0x288, with the compact count clamped to 1024.
    result.blockA = compareFloatBlock(a, b, 0x88, 128);
    result.blockB = compareFloatBlock(a, b, 0x288, 1024);
    result.ok = true;
    return result;
}

static void printBlockStats(const char* name, const BlockCompareStats& s) {
    std::cout << "  " << name << ":\n";
    std::cout << "    floats:        " << s.count << "\n";
    std::cout << "    exact matches: " << s.exactFloatMatches << "/" << s.count << "\n";
    std::cout << std::fixed << std::setprecision(9);
    std::cout << "    correlation:   " << s.correlation << "\n";
    std::cout << "    MAE:           " << s.mae << "\n";
    std::cout << "    RMSE:          " << s.rmse << "\n";
    std::cout << "    max abs error: " << s.maxAbsError << "\n";
    std::cout.unsetf(std::ios::floatfield);
}

void printGp200Compare(const fs::path& aPath, const fs::path& bPath, const Gp200CompareResult& r) {
    std::cout << "GP-200 CLO comparison\n";
    std::cout << "  A: " << pathToUtf8(aPath) << "\n";
    std::cout << "  B: " << pathToUtf8(bPath) << "\n";
    if (!r.ok) {
        std::cout << "  ERROR: " << r.error << "\n";
        return;
    }
    std::cout << "\nStructure\n";
    std::cout << "  A declared/payload/model: 0x" << std::hex << std::uppercase << r.a.declaredSize
              << " / 0x" << r.a.payloadSize << " / 0x" << r.a.modelField << std::dec << "\n";
    std::cout << "  B declared/payload/model: 0x" << std::hex << std::uppercase << r.b.declaredSize
              << " / 0x" << r.b.payloadSize << " / 0x" << r.b.modelField << std::dec << "\n";
    std::cout << "  A GP200 shape: " << ((r.a.declaredSize == 0x1288 && r.a.payloadSize == 0x1200 && r.a.modelField == 0x400) ? "yes" : "NO") << "\n";
    std::cout << "  B GP200 shape: " << ((r.b.declaredSize == 0x1288 && r.b.payloadSize == 0x1200 && r.b.modelField == 0x400) ? "yes" : "NO") << "\n";

    std::cout << "\nCRC16/MODBUS\n";
    std::cout << "  A stored/calculated: 0x" << std::hex << std::uppercase << r.storedCrcA
              << " / 0x" << r.calculatedCrcA << std::dec << " -> " << (r.crcAValid ? "valid" : "INVALID") << "\n";
    std::cout << "  B stored/calculated: 0x" << std::hex << std::uppercase << r.storedCrcB
              << " / 0x" << r.calculatedCrcB << std::dec << " -> " << (r.crcBValid ? "valid" : "INVALID") << "\n";

    std::cout << "\nByte comparison\n";
    std::cout << "  equal:              " << r.byteMatches << "/" << kExpectedCloSize << "\n";
    std::cout << "  different:          " << r.byteDifferences << "/" << kExpectedCloSize << "\n";
    std::cout << "  different <0x1288:  " << r.usefulByteDifferences << "\n";
    std::cout << "  different padding:  " << r.paddingByteDifferences << "\n";

    std::cout << "\nFloat blocks\n";
    printBlockStats("Block A @0x88, 128 float32", r.blockA);
    printBlockStats("Block B @0x288, 1024 float32", r.blockB);
}


bool readFileBytes(const fs::path& path, std::vector<std::uint8_t>& data, std::string& error) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { error = "Cannot open file: " + pathToUtf8(path); return false; }
    const auto end = in.tellg();
    if (end < 0) { error = "Cannot determine file size: " + pathToUtf8(path); return false; }
    data.resize(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (static_cast<std::size_t>(in.gcount()) != data.size()) {
            error = "Short read: " + pathToUtf8(path); return false;
        }
    }
    return true;
}

bool writeFileBytes(const fs::path& path, const std::uint8_t* data, std::size_t size, std::string& error) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) { error = "Cannot create output directory: " + ec.message(); return false; }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Cannot create file: " + pathToUtf8(path); return false; }
    out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) { error = "Failed writing file: " + pathToUtf8(path); return false; }
    return true;
}

} // namespace ntc
