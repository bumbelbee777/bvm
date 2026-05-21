# Cross-platform smoke test: assemble each example then run it in the VM.
if(NOT BAS OR NOT BVM OR NOT ROOT OR NOT BINDIR)
    message(FATAL_ERROR "RunAllToolchain.cmake requires -DBAS -DBVM -DROOT -DBINDIR")
endif()

file(MAKE_DIRECTORY "${BINDIR}")

set(EXAMPLES ArithmeticDemo ShiftRotateDemo FibonacciDemo CallStackDemo InterruptDemo PushImmediateDemo AtomicDemo MmioBootSequenceDemo PagingDemo PagingSoftwareRefillDemo ExceptionNestDemo NumaDemo FsxDemo FsxFmaDemo FsxMathDemo SsxDemo SsxCryptoDemo HpcLanesDemo VectorOpsDemo FusedNegDemo RecipLanesDemo MatMul2x2Demo GemmAccumDemo MlInferenceDemo NsxDemo KBankDemo DaxDemo DaxStreamDemo DaxCrossCoreDemo SkbDemo CplxDemo CplxVectorDemo SecuritySandboxDemo CompactHybridDemo BlockDemo Usb3Demo Ac97Demo UartDemo DevicePresentDemo PlatformTimerDemo PlatformRtcDemo)

foreach(NAME ${EXAMPLES})
    set(SRC "${ROOT}/examples/${NAME}.s")
    set(OUT "${BINDIR}/${NAME}.bin")
    if(NOT EXISTS "${SRC}")
        message(FATAL_ERROR "Missing example source: ${SRC}")
    endif()

    execute_process(
        COMMAND "${BAS}" "${SRC}" "-o" "${OUT}"
        RESULT_VARIABLE AsmResult
        OUTPUT_VARIABLE AsmOut
        ERROR_VARIABLE AsmErr
        ENCODING UTF-8
    )
    if(NOT AsmResult EQUAL 0)
        message(FATAL_ERROR "bas failed on ${NAME}:\n${AsmErr}\n${AsmOut}")
    endif()

    execute_process(
        COMMAND "${BVM}" "${OUT}"
        RESULT_VARIABLE VmResult
        OUTPUT_VARIABLE VmOut
        ERROR_VARIABLE VmErr
        ENCODING UTF-8
    )
    if(NOT VmResult EQUAL 0)
        message(FATAL_ERROR "bvm failed on ${NAME}:\n${VmErr}\n${VmOut}")
    endif()
endforeach()
