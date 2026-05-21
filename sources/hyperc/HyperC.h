#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HyperCImage {
    std::vector<uint8_t> Bytes;
    uint64_t TextSize = 0;
    uint64_t EntryOffset = 0;
};

HyperCImage CompileSource(const std::string& Source, const std::string& SourceName = "<input>");
HyperCImage CompileFile(const std::string& InputPath);
void WriteImage(const HyperCImage& Image, const std::string& OutputPath);
