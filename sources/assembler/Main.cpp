#include <cstring>
#include <iostream>
#include <string>
#include "Assembler.h"

static void PrintUsage(const char* ProgramName) {
    std::cout << "Usage: " << ProgramName << " [options] <input_file> -o <output_file>\n"
              << "  Honeycomb assembler driver (bas).\n"
              << "  -o <file>    Specify output file\n"
              << "  -v           Enable verbose output\n"
              << "  -h, --help   Show this help message\n";
}

int main(int Argc, char** Argv) {
    std::string InputFile;
    std::string OutputFile;
    bool Verbose = false;

    for (int I = 1; I < Argc; ++I) {
        std::string Arg = Argv[I];

        if (Arg == "-h" || Arg == "--help") {
            PrintUsage(Argv[0]);
            return 0;
        }
        if (Arg == "-v") {
            Verbose = true;
            continue;
        }
        if (Arg == "-o") {
            if (I + 1 < Argc) {
                OutputFile = Argv[++I];
            } else {
                std::cerr << "Error: Missing output file after -o\n";
                return 1;
            }
            continue;
        }
        if (Arg[0] == '-') {
            std::cerr << "Error: Unknown option " << Arg << "\n";
            PrintUsage(Argv[0]);
            return 1;
        }
        if (InputFile.empty()) {
            InputFile = Arg;
        } else {
            std::cerr << "Error: Multiple input files specified\n";
            return 1;
        }
    }

    if (InputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        PrintUsage(Argv[0]);
        return 1;
    }

    if (OutputFile.empty()) {
        size_t LastDot = InputFile.find_last_of('.');
        OutputFile = (LastDot != std::string::npos)
            ? InputFile.substr(0, LastDot) + ".bin"
            : InputFile + ".bin";
        if (Verbose) {
            std::cout << "No output file specified, using default: " << OutputFile << "\n";
        }
    }

    try {
        if (Verbose) {
            std::cout << "Assembling " << InputFile << " -> " << OutputFile << "...\n";
        }
        AssembleFile(InputFile, OutputFile);
        if (Verbose) {
            std::cout << "Assembly completed successfully.\n";
        }
        return 0;
    } catch (const std::exception& Ex) {
        std::cerr << "Assembly error: " << Ex.what() << "\n";
        return 1;
    }
}
