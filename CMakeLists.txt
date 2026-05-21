cmake_minimum_required(VERSION 3.16)
project(HoneycombVM VERSION 0.1.0 LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/cmake/Dependencies.cmake)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add subdirectories
add_subdirectory(sources/bvm)
add_subdirectory(sources/assembler)
add_subdirectory(sources/hyperc)

# Create build directory for output
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Enable testing
enable_testing()

set(HONEYCOMB_TEST_SOURCES
    tests/HelloTest.cpp
    sources/assembler/Assembler.cpp
    sources/bvm/Core.cpp
    sources/bvm/Interrupts.cpp
    sources/bvm/Instructions.cpp
    sources/bvm/Atomic.cpp
    sources/bvm/FSX.cpp
    sources/bvm/SSX.cpp
    sources/bvm/SsxCrypto.cpp
    sources/bvm/Mmio.cpp
    sources/bvm/HostIo.cpp
    sources/bvm/Display.cpp
    sources/bvm/Ac97.cpp
    sources/bvm/Usb3.cpp
    sources/bvm/Block.cpp
    sources/bvm/Uart.cpp
    sources/bvm/Devices.cpp
    sources/bvm/PlatformIrq.cpp
    sources/bvm/PlatformTimer.cpp
    sources/bvm/PlatformRtc.cpp
    sources/bvm/GuestMem.cpp
    sources/bvm/DmaEngine.cpp
    sources/bvm/VirtioMmio.cpp
    sources/bvm/L1Cache.cpp
    sources/bvm/VmConfig.cpp
    sources/bvm/Machine.cpp
    sources/bvm/Matrix.cpp
    sources/bvm/Nsx.cpp
    sources/bvm/NsxMl.cpp
    sources/bvm/Security.cpp
    sources/bvm/Compact.cpp
    sources/bvm/KBank.cpp
    sources/bvm/DaxToken.cpp
    sources/bvm/DaxMailbox.cpp
    sources/bvm/Dax.cpp
    sources/bvm/Skb.cpp
    sources/bvm/Cplx.cpp
    sources/bvm/RegFile.cpp
    sources/bvm/Paging.cpp
    sources/bvm/Tlb.cpp
    sources/bvm/UserMode.cpp
    sources/bvm/Numa.cpp
)

add_executable(hello_test ${HONEYCOMB_TEST_SOURCES} tests/HyperCTest.cpp)
target_include_directories(hello_test PRIVATE
    ${CMAKE_SOURCE_DIR}/sources
    ${CMAKE_SOURCE_DIR}/sources/assembler
    ${CMAKE_SOURCE_DIR}/sources/bvm
    ${CMAKE_SOURCE_DIR}/sources/hyperc
    ${CMAKE_SOURCE_DIR}/tests
)
target_link_libraries(hello_test PRIVATE hyperc)
target_compile_features(hello_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(hello_test PRIVATE /W4)
else()
    target_compile_options(hello_test PRIVATE -Wall -Wextra)
endif()

add_test(NAME hello_test COMMAND hello_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(
    NAME toolchain_examples
    COMMAND ${CMAKE_COMMAND}
        -DBAS=$<TARGET_FILE:bas>
        -DBVM=$<TARGET_FILE:bvm>
        -DROOT=${CMAKE_SOURCE_DIR}
        -DBINDIR=${CMAKE_BINARY_DIR}/Testing/toolchain
        -P ${CMAKE_SOURCE_DIR}/cmake/RunAllToolchain.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
