#include "Shared.h"
#include "Devices.h"
#include "HostIo.h"
#include "Machine.h"
#include "VmConfig.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>


static Cpu Vm;

static void PrintUsage(const char* ProgramName) {
    std::cout << "Usage: " << ProgramName << " [options] <binary_file>\n"
              << "  -h, --help         Show this help\n"
              << "  -s, --step         Step through execution\n"
              << "  -a, --address N    Load address for the image (default: 0)\n"
              << "  -e, --entry N      Entry IC = load address + offset (default: 0)\n"
              << "      --host-io      Enable SDL host display/input/audio devices\n"
              << "      --firmware PATH Load firmware ROM/BIN (see --firmware-address)\n";
    PrintVmConfigUsage();
}

static void PrintMachineSummary(const VmConfig& Config) {
    std::cout << "Machine: ram=" << Config.RamBytes << " cores=" << Config.CoreCount
              << " kbank=" << Config.KBankBytes << " skb=" << Config.SkbBytes
              << " numa=" << Config.NumaNodes << " mmio=0x" << std::hex << GetMmioWindowBase()
              << " display=0x" << GetFramebufferBase() << std::dec << " ("
              << Config.FramebufferWidth << "x" << Config.FramebufferHeight << " X8R8G8B8)\n";
    if (Config.CoreCount > 1) {
        std::cout << "Note: --cores > 1 is recorded for firmware; BVM still runs one CPU.\n";
    }
}

static bool LoadBinaryFile(const std::string& Filename, uint64_t LoadAddress,
                           uint64_t EntryOffset) {
    std::ifstream File(Filename, std::ios::binary | std::ios::ate);
    if (!File) return false;
    std::streamsize Size = File.tellg();
    File.seekg(0, std::ios::beg);
    const uint64_t Total = static_cast<uint64_t>(LoadAddress) + static_cast<uint64_t>(Size);
    if (Total > GetRamSize()) {
        return false;
    }
    std::vector<uint8_t> Buffer(static_cast<size_t>(Size));
    File.read(reinterpret_cast<char*>(Buffer.data()), Size);
    const uint64_t EntryIc = LoadAddress + EntryOffset;
    Vm.LoadImage(Buffer.data(), Buffer.size(), LoadAddress, EntryIc);
    std::cout << "Loaded " << Size << " bytes at 0x" << std::hex << LoadAddress << std::dec
              << "; entry IC=0x" << std::hex << EntryIc << std::dec << "\n";
    return true;
}

static void RunWithHostIo() {
    while (!Vm.IsHalted()) {
        Devices::Service(Vm);
        if (HostIo::ShouldQuit()) {
            Vm.Halt();
            break;
        }
        Vm.Step();
    }
}

static bool ParseMachineOption(const char* Flag, const char* Value, VmConfig& Config,
                               std::string& Error) {
    size_t Bytes = 0;
    if (std::strcmp(Flag, "--ram") == 0 || std::strcmp(Flag, "--mem") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --ram size";
            return false;
        }
        Config.RamBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--cores") == 0) {
        Config.CoreCount = static_cast<uint32_t>(std::stoul(Value, nullptr, 0));
        return true;
    }
    if (std::strcmp(Flag, "--kbank") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --kbank size";
            return false;
        }
        Config.KBankBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--skb") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --skb size";
            return false;
        }
        Config.SkbBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--numa-nodes") == 0) {
        Config.NumaNodes = static_cast<uint32_t>(std::stoul(Value, nullptr, 0));
        return true;
    }
    if (std::strcmp(Flag, "--remote-k") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --remote-k size";
            return false;
        }
        Config.RemoteKBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--remote-skb") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --remote-skb size";
            return false;
        }
        Config.RemoteSkbBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--remote-dram") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --remote-dram size";
            return false;
        }
        Config.RemoteDramBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--fb-width") == 0) {
        Config.FramebufferWidth = static_cast<uint32_t>(std::stoul(Value, nullptr, 0));
        return true;
    }
    if (std::strcmp(Flag, "--fb-height") == 0) {
        Config.FramebufferHeight = static_cast<uint32_t>(std::stoul(Value, nullptr, 0));
        return true;
    }
    if (std::strcmp(Flag, "--audio-gain") == 0) {
        Config.HostAudioGain = std::stof(Value);
        return true;
    }
    if (std::strcmp(Flag, "--disk") == 0) {
        if (!ParseByteSize(Value, Bytes)) {
            Error = "invalid --disk size";
            return false;
        }
        Config.DiskBytes = Bytes;
        return true;
    }
    if (std::strcmp(Flag, "--timer-freq") == 0 || std::strcmp(Flag, "--timer-frequency") == 0) {
        Config.TimerFrequencyHz = std::stoull(Value, nullptr, 0);
        return true;
    }
    if (std::strcmp(Flag, "--firmware-address") == 0) {
        Config.FirmwareLoadAddress = std::stoull(Value, nullptr, 0);
        return true;
    }
    if (std::strcmp(Flag, "--firmware-entry") == 0) {
        Config.FirmwareEntryOffset = std::stoull(Value, nullptr, 0);
        return true;
    }
    Error = std::string("unknown option: ") + Flag;
    return false;
}

int main(int Argc, char** Argv) {
    if (Argc < 2) {
        PrintUsage(Argv[0]);
        return 1;
    }

    VmConfig Config = VmConfigDefaults();
    std::string Filename;
    bool StepMode = false;
    bool HostIoMode = false;
    bool ShowHelp = false;
    uint64_t LoadAddress = 0;
    uint64_t EntryOffset = 0;

    for (int I = 1; I < Argc; ++I) {
        if (std::strcmp(Argv[I], "-h") == 0 || std::strcmp(Argv[I], "--help") == 0) {
            ShowHelp = true;
        } else if (std::strcmp(Argv[I], "-s") == 0 || std::strcmp(Argv[I], "--step") == 0) {
            StepMode = true;
        } else if (std::strcmp(Argv[I], "-a") == 0 || std::strcmp(Argv[I], "--address") == 0) {
            if (++I >= Argc) return 1;
            LoadAddress = std::stoull(Argv[I], nullptr, 0);
        } else if (std::strcmp(Argv[I], "-e") == 0 || std::strcmp(Argv[I], "--entry") == 0) {
            if (++I >= Argc) return 1;
            EntryOffset = std::stoull(Argv[I], nullptr, 0);
        } else if (std::strcmp(Argv[I], "--host-io") == 0) {
            HostIoMode = true;
        } else if (std::strcmp(Argv[I], "--no-ac97") == 0) {
            Config.EnableAc97 = false;
        } else if (std::strcmp(Argv[I], "--no-usb3") == 0) {
            Config.EnableUsb3 = false;
        } else if (std::strcmp(Argv[I], "--no-block") == 0) {
            Config.EnableBlock = false;
        } else if (std::strcmp(Argv[I], "--no-uart") == 0) {
            Config.EnableUart = false;
        } else if (std::strcmp(Argv[I], "--no-dma") == 0) {
            Config.EnableDma = false;
        } else if (std::strcmp(Argv[I], "--no-virtio") == 0) {
            Config.EnableVirtio = false;
        } else if (std::strcmp(Argv[I], "--firmware") == 0) {
            if (++I >= Argc) {
                PrintUsage(Argv[0]);
                return 1;
            }
            Config.FirmwarePath = Argv[I];
        } else if (std::strcmp(Argv[I], "--disk-image") == 0) {
            if (++I >= Argc) {
                PrintUsage(Argv[0]);
                return 1;
            }
            Config.DiskImagePath = Argv[I];
        } else if (Argv[I][0] == '-' && Argv[I][1] != '\0') {
            if (++I >= Argc) {
                PrintUsage(Argv[0]);
                return 1;
            }
            std::string Error;
            if (!ParseMachineOption(Argv[I - 1], Argv[I], Config, Error)) {
                std::cerr << Error << "\n";
                PrintUsage(Argv[0]);
                return 1;
            }
        } else if (Argv[I][0] != '-') {
            Filename = Argv[I];
        } else {
            PrintUsage(Argv[0]);
            return 1;
        }
    }

    if (ShowHelp) {
        PrintUsage(Argv[0]);
        return 0;
    }

    if (!HostIoMode && std::getenv("HOST_IO") != nullptr) {
        HostIoMode = true;
    }

    std::string ConfigError;
    if (!ValidateVmConfig(Config, ConfigError)) {
        std::cerr << ConfigError << "\n";
        return 1;
    }

    InitMachine(Config);
    Devices::ResetAll();
    Vm.Reset();
    PrintMachineSummary(Config);

    bool LoadedImage = false;
    if (!Config.FirmwarePath.empty()) {
        if (!LoadBinaryFile(Config.FirmwarePath, Config.FirmwareLoadAddress,
                            Config.FirmwareEntryOffset)) {
            std::cerr << "Failed to load firmware: " << Config.FirmwarePath << "\n";
            return 1;
        }
        LoadedImage = true;
    }

    if (!Filename.empty()) {
        if (!LoadBinaryFile(Filename, LoadAddress, EntryOffset)) {
            std::cerr << "Failed to load: " << Filename << "\n";
            return 1;
        }
        LoadedImage = true;
    }

    if (!LoadedImage) {
        std::cerr << "No binary or firmware image specified.\n";
        PrintUsage(Argv[0]);
        return 1;
    }

    if (HostIoMode) {
        HostIo::Init();
    }

    try {
        if (StepMode) {
            while (!Vm.IsHalted()) {
                if (HostIoMode) {
                    Devices::Poll(Vm);
                    if (HostIo::ShouldQuit()) {
                        break;
                    }
                }
                Vm.Step();
                Vm.DumpRegisters();
                std::cout << "Enter=step q=quit > ";
                std::string Line;
                if (!std::getline(std::cin, Line) || Line == "q") break;
            }
        } else if (HostIoMode) {
            RunWithHostIo();
        } else {
            Vm.Run();
        }
    } catch (const std::exception& Ex) {
        std::cerr << "VM error: " << Ex.what() << "\n";
        Vm.DumpRegisters();
        if (HostIoMode) {
            HostIo::Shutdown();
        }
        return 1;
    }

    if (HostIoMode) {
        HostIo::Shutdown();
    }

    if (!Config.DiskImagePath.empty()) {
        if (!Devices::SaveDiskImage(Config.DiskImagePath)) {
            std::cerr << "Warning: failed to save disk image: " << Config.DiskImagePath << "\n";
        }
    }

    std::cout << "Done.\n";
    Vm.DumpRegisters();
    return 0;
}
