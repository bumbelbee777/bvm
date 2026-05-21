# VM source files
set(VM_SOURCES
    Main.cpp
    VmConfig.cpp
    Machine.cpp
    Core.cpp
    Interrupts.cpp
    Instructions.cpp
    Atomic.cpp
    FSX.cpp
    SSX.cpp
    SsxCrypto.cpp
    Mmio.cpp
    HostIo.cpp
    Display.cpp
    Ac97.cpp
    Usb3.cpp
    Block.cpp
    Uart.cpp
    Devices.cpp
    PlatformIrq.cpp
    PlatformTimer.cpp
    PlatformRtc.cpp
    GuestMem.cpp
    DmaEngine.cpp
    VirtioMmio.cpp
    L1Cache.cpp
    Matrix.cpp
    Nsx.cpp
    NsxMl.cpp
    Security.cpp
    Compact.cpp
    KBank.cpp
    DaxToken.cpp
    DaxMailbox.cpp
    Dax.cpp
    Skb.cpp
    Cplx.cpp
    RegFile.cpp
    Paging.cpp
    Tlb.cpp
    UserMode.cpp
    Numa.cpp
)

# VM headers
set(VM_HEADERS
    Shared.h
)

# Create VM executable
add_executable(bvm ${VM_SOURCES} ${VM_HEADERS})

# Set include directories
target_include_directories(bvm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/sources)

# Set compiler warnings
if(MSVC)
    target_compile_options(bvm PRIVATE /W4 /WX)
else()
    target_compile_options(bvm PRIVATE -Wall -Wextra -Werror)
endif()

# Install target
if(HONEYCOMB_HOST_IO)
    target_compile_definitions(bvm PRIVATE HONEYCOMB_HOST_IO=1)
    target_link_libraries(bvm PRIVATE SDL3::SDL3)
    if(WIN32 AND TARGET SDL3::SDL3-shared)
        add_custom_command(
            TARGET bvm POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL3::SDL3-shared>
                $<TARGET_FILE_DIR:bvm>
            VERBATIM
        )
    endif()
endif()

install(TARGETS bvm
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
