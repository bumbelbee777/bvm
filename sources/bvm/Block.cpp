#include "Block.h"
#include "DeviceSpecs.h"
#include "GuestMem.h"
#include "Machine.h"
#include "MmioMap.h"
#include "PlatformIrq.h"



#include <algorithm>

#include <cstring>

#include <fstream>

#include <vector>



namespace {



bool Enabled = true;

std::vector<uint8_t> Storage;

uint64_t CapacitySectors = 0;

uint32_t BlockSize = DeviceSpecs::VirtioBlock::BlockSizeBytes;

uint32_t Command = DeviceSpecs::VirtioBlock::CommandIdle;

uint64_t Lba = 0;

uint64_t BufferPtr = 0;

uint32_t SectorCount = 1;

uint32_t Status = DeviceSpecs::VirtioBlock::StatusIdle;



void FinishCommand(uint32_t FinalStatus) {
    Status = FinalStatus;
    Command = DeviceSpecs::VirtioBlock::CommandIdle;
    if (FinalStatus == DeviceSpecs::VirtioBlock::StatusComplete) {
        PlatformIrq::SignalLine(DeviceSpecs::IrqLine::Block);
    }
}



void ExecuteCommand() {

    if (SectorCount == 0 ||

        SectorCount > DeviceSpecs::VirtioBlock::MaxSectorsPerCommand) {

        FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

        return;

    }



    if (Command == DeviceSpecs::VirtioBlock::CommandFlush) {

        FinishCommand(DeviceSpecs::VirtioBlock::StatusComplete);

        return;

    }



    if (Lba + SectorCount > CapacitySectors || BufferPtr == 0) {

        FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

        return;

    }



    const size_t TotalBytes = static_cast<size_t>(SectorCount) * BlockSize;

    if (!GuestMem::RegionValid(BufferPtr, TotalBytes)) {

        FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

        return;

    }



    for (uint32_t Index = 0; Index < SectorCount; ++Index) {

        const uint64_t SectorLba = Lba + Index;

        const size_t StorageOffset = static_cast<size_t>(SectorLba) * BlockSize;

        const uint64_t GuestOffset = BufferPtr + static_cast<uint64_t>(Index) * BlockSize;



        if (StorageOffset + BlockSize > Storage.size()) {

            FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

            return;

        }



        if (Command == DeviceSpecs::VirtioBlock::CommandRead) {

            if (!GuestMem::WriteBytes(GuestOffset, Storage.data() + StorageOffset, BlockSize)) {

                FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

                return;

            }

        } else if (Command == DeviceSpecs::VirtioBlock::CommandWrite) {

            if (!GuestMem::ReadBytes(GuestOffset, Storage.data() + StorageOffset, BlockSize)) {

                FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

                return;

            }

        } else {

            FinishCommand(DeviceSpecs::VirtioBlock::StatusError);

            return;

        }

    }



    FinishCommand(DeviceSpecs::VirtioBlock::StatusComplete);

}



} // namespace



namespace Block {



void ConfigureStorage(size_t Bytes) {

    const size_t Aligned = std::max<size_t>(Bytes, BlockSize);

    Storage.assign(Aligned, 0);

    CapacitySectors = Aligned / BlockSize;

}



bool LoadImage(const std::string& Path) {

    if (Path.empty()) {

        return true;

    }

    std::ifstream File(Path, std::ios::binary | std::ios::ate);

    if (!File) {

        return false;

    }

    const std::streamsize Size = File.tellg();

    if (Size <= 0) {

        return false;

    }

    File.seekg(0, std::ios::beg);

    const size_t Aligned =

        std::max<size_t>(static_cast<size_t>(Size), BlockSize);

    Storage.assign(Aligned, 0);

    CapacitySectors = Aligned / BlockSize;

    File.read(reinterpret_cast<char*>(Storage.data()), Size);

    return static_cast<bool>(File);

}



bool SaveImage(const std::string& Path) {

    if (Path.empty() || Storage.empty()) {

        return true;

    }

    std::ofstream File(Path, std::ios::binary | std::ios::trunc);

    if (!File) {

        return false;

    }

    File.write(reinterpret_cast<const char*>(Storage.data()),

               static_cast<std::streamsize>(Storage.size()));

    return static_cast<bool>(File);

}



void Reset() {

    Command = DeviceSpecs::VirtioBlock::CommandIdle;

    Lba = 0;

    BufferPtr = 0;

    SectorCount = 1;

    Status = DeviceSpecs::VirtioBlock::StatusIdle;

}



void SetEnabled(bool IsEnabled) { Enabled = IsEnabled; }



bool IsEnabled() { return Enabled; }



uint64_t ReadMmioQuad(uint64_t Offset) {

    if (!Enabled) {

        return 0;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::BlockCapacitySectors:

            return CapacitySectors;

        case Quad::BlockSizeBytes:

            return BlockSize;

        case Quad::BlockCommand:

            return Command;

        case Quad::BlockLba:

            return Lba;

        case Quad::BlockBufferPtr:

            return BufferPtr;

        case Quad::BlockStatus:

            return Status;

        case Quad::BlockSectorCount:

            return SectorCount;

        default:

            return 0;

    }

}



void WriteMmioQuad(uint64_t Offset, uint64_t Value) {

    if (!Enabled) {

        return;

    }



    using Quad = MmioWindow::RegisterQuadWord;

    switch (static_cast<Quad>(Offset)) {

        case Quad::BlockCommand:

            Command = static_cast<uint32_t>(Value);

            if (Command == DeviceSpecs::VirtioBlock::CommandRead ||

                Command == DeviceSpecs::VirtioBlock::CommandWrite ||

                Command == DeviceSpecs::VirtioBlock::CommandFlush) {

                Status = DeviceSpecs::VirtioBlock::StatusBusy;

                ExecuteCommand();

            }

            break;

        case Quad::BlockLba:

            Lba = Value;

            break;

        case Quad::BlockBufferPtr:

            BufferPtr = Value;

            break;

        case Quad::BlockStatus:

            Status = static_cast<uint32_t>(Value & 0xFF);

            break;

        case Quad::BlockSectorCount:

            SectorCount = static_cast<uint32_t>(

                std::min<uint64_t>(Value, DeviceSpecs::VirtioBlock::MaxSectorsPerCommand));

            if (SectorCount == 0) {

                SectorCount = 1;

            }

            break;

        default:

            break;

    }

}



} // namespace Block

