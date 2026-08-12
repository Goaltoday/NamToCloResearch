#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ntc {

namespace fs = std::filesystem;

struct RuntimePaths {
    fs::path dll;
    fs::path stimulus;
};

struct ConversionResult {
    bool ok = false;
    int exitCode = 0;
    std::string error;
    fs::path inputNam;
    fs::path ampero2048;
    fs::path gp2001024;
};

struct BatchConversionResult {
    bool ok = false;
    std::size_t total = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::vector<ConversionResult> items;
};

using StatusCallback = std::function<void(const std::wstring&)>;

RuntimePaths resolveDefaultRuntime();
bool validateRuntime(const RuntimePaths& runtime, std::string& error);
ConversionResult convertNamToBoth(const fs::path& inputNam,
                                  const fs::path& outputDirectory,
                                  const StatusCallback& status = {});
BatchConversionResult convertNamFolder(const fs::path& inputDirectory,
                                       const fs::path& outputDirectory,
                                       const StatusCallback& status = {});

// Internal worker entry used by the same GUI executable in a hidden child process.
int runWorkerCommandLine(int argc, wchar_t** argv);

} // namespace ntc
