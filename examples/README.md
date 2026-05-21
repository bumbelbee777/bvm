# Examples

Honeycomb assembly samples used by `hello_test`, CTest `toolchain_examples`, and manual runs with **`bas`** then **`bvm`**.

```text
bas examples/ArithmeticDemo.s -o out.bin
bvm out.bin
```

Shared opcode and encoding definitions live in [`sources/Isa.h`](../sources/Isa.h).

| File | Purpose |
|------|---------|
| `ArithmeticDemo.s` | Arithmetic, `.data` loads, compare, `jgt`, `dec`/`jnz` loop. |
| `ShiftRotateDemo.s` | Shift-integrated ALU (`rt<<n`, `>>>`, `Accum`). |
| `CallStackDemo.s` | `call`/`ret`, **`[sp]`**, **`[sp+8]`**, `push`/`pop`. |
| `FibonacciDemo.s` | `cmp`/`je`, `copy`, iterative Fibonacci. |
| `InterruptDemo.s` | `lea ivb` (**IVT base VMA**), `ivl`, `ei`/`int`, `iret`, `perfmon`. |
| `PushImmediateDemo.s` | `push` with an immediate 64-bit constant, `pop` into a register. |
| `AtomicDemo.s` | Atomic RMW (`atadd`/`atinc`/`atcas`), `satadd`/`satmul`, `in`/`out`, `prefetch`. |
| `MmioBootSequenceDemo.s` | MMIO bring-up, IVT post, supervisor **`syscall`**, **`eu`** + user **`syscall`**, **`FR.UserMode`**. |
| `PagingSoftwareRefillDemo.s` | **`ESR`**, TLB miss vector **15**, **`TlbInsert`**, **`WireTlbEntry`**. |
| `ExceptionNestDemo.s` | **3-deep** hardware exception stack, **`ZERO`**, **`PAUSE`**. |
| `FsxDemo.s` | FSX **`fload`/`fadd`/`fstore`** (FP64 **`F0..F7`**). |
| `FsxFmaDemo.s` | FSX **`fma`** fused multiply-add into accumulator. |
| `FsxMathDemo.s` | FSX **`fsqrt`/`fabs`/`fneg`/`rsqrt`** unary math. |
| `SsxDemo.s` | SSX **`xload`/`vadd`/`xstore`** (128-bit pack, FP32 lanes via **`opmode` 100**). |
| `HpcLanesDemo.s` | SSX **`vbroadcast`/`vmul`/`vfma`/`vsqrt`** lane pipeline. |
| `VectorOpsDemo.s` | SSX **`vdiv`/`vmin`/`vmax`/`vneg`/`vabs`** on FP32 lanes. |
| `FusedNegDemo.s` | SSX **`vfnmadd`** fused negated multiply-add. |
| `RecipLanesDemo.s` | SSX **`vrcp`/`vrsqrt`** via **`vload`/`vstore`**. |
| `MatMul2x2Demo.s` | **`matmul`** 2×2 FP32 tile via **32-byte descriptor**. |
| `GemmAccumDemo.s` | **`gemm`** (`GEMM_OFFLOAD`) with **α=β=1** accumulate. |
| `MlInferenceDemo.s` | ML pipeline + **transformer training** on **`sin(x²)`** via **`mse_loss`/`fwd_end`/`sgd`**. |
| `NsxDemo.s` | NSX: **`relu`/`gelu`/`softmax`/`nconv2d`** (legacy **`conv2d`** = `0x184`; NSX = `0x621`). See **`Honeycomb_Implementation.md`** for full baseline NSX list (`layernorm`, `pool_*`, `gru_cell`, …). |
| `KBankDemo.s` | K-Bank **`kwind`/`kstorei`/`kloadi`** flat scratchpad smoke test. |
| `DaxDemo.s` | DAX dataflow **`dax_token`/`dax_add`/`dax_mul`/`dax_wait`** — `(5+3)×(2+4)=48`. |
| `DaxStreamDemo.s` | **`dax_stream`** SKB line → K token burst. |
| `DaxCrossCoreDemo.s` | **`dax_send`/`dax_recv`** simulated cross-core mailbox. |
| `SkbDemo.s` | SKB **`skbload`/`skbstore`/`skbatadd`/`skbbind`** shared line + K-Bank alias (use **`add rd, r12, #imm`** for constants; `load rd, #imm` assumes **`R0` is zero). |
| `CplxDemo.s` | CPLX **`cpack`/`cmul`/`cextract`/`cstore`/`cload`** — `(3+4i)×(1+2i)` in **C_FP64** (`CRk` → `R2k`, `R2k+1`). |
| `CplxVectorDemo.s` | **`vcadd`/`cfma`/`vcfft2`/`vcfft4`** — C_FP32 in SSX (`cx*`), use **`SsxVectorLength=4`** in tests. |
| `SecuritySandboxDemo.s` | Security: **`sandbox_allow`/`sandbox_base`**, user **`eu`** + **`syscall`**, fault vector **`0x3E`** (`R10 = 0xC0FEC0DE`). See **`docs/Honeycomb_Security.md`**. |
| `CompactHybridDemo.s` | **`.section` compact/full** — **`call full_init`** enters full mode, **`ret`** returns (`R10 = 0xC0FE0001`). |
