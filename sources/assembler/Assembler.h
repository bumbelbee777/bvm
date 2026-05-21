#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <cstdint>
#include <string>
#include <vector>

struct AssemblyImage {
    std::vector<uint8_t> Bytes;
    uint64_t TextSize = 0;
    uint64_t EntryOffset = 0;
};

void AssembleFile(const std::string& InputFile, const std::string& OutputFileName);
AssemblyImage AssembleSourceFile(const std::string& InputFile);
std::vector<uint64_t> AssembleInstructions(const std::vector<std::string>& Lines);

#endif // ASSEMBLER_H
