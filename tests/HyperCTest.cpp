#include "HyperCTest.h"



#include "../sources/hyperc/HyperC.h"

#include "../sources/bvm/Shared.h"

#include "../sources/bvm/VmConfig.h"

#include "../sources/bvm/Machine.h"



#include <cassert>

#include <filesystem>



void RunHyperCTests() {

    const std::filesystem::path Root =

        std::filesystem::path(__FILE__).parent_path().parent_path();



    {

        const HyperCImage Image = CompileFile((Root / "examples" / "HyperCDemo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 12ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM2Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 14ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM2AsmDemo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 15ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM3Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 6ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM4Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 30ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM5Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 313ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM6Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 40ULL);

    }



    {

        const HyperCImage Image =

            CompileFile((Root / "examples" / "HyperCM7Demo.hc").string());

        assert(!Image.Bytes.empty());



        InitMachine(VmConfigDefaults());

        Cpu Vm;

        Vm.LoadImage(Image.Bytes.data(), Image.Bytes.size(), 0, Image.EntryOffset);

        Vm.Run();



        assert(Vm.IsHalted());

        assert(Vm.R15 == 5ULL);

    }

}

