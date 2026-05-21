# Honeycomb — Implementation Notes

**Companion to:** [Honeycomb.md](Honeycomb.md) (normative ISA)  
**Scope:** Baseline `bas` / `bvm` behavior, verification, and hardware planning — not opcode semantics.

---

## Baseline BVM (v0.7-SKB aligned)

| Feature | Baseline `bvm` | Silicon (multi-core) |
|---------|----------------|----------------------|
| GPR file | `R0..R31` (codes `0x00..0x1F`) | Same |
| Special regs | `SP`…`KMASK`; **`WINSZ` @ `0x27`** merges window size + complex tag (no `CTYPE` @ `0x5C`) | + window/rotation specials |
| K-Bank | **16384 × 64-bit** (128 KB), domain **`0000`** | AWP/VSP/ISP/S$ ports |
| K access | `0x072..0x08F` per opcode map (`STREAM*`, `NUMA*`, `HINT*`) | AWP ring hardware |
| **SKB** | **32 MB**, **512K lines**, domain **`0010`** @ `0x2000_0000_0000_0000` | 16 banks + directory + XBAR |
| SKB opcodes | Full `0x090..0x0A4` including `SKB_MOVE`, `SKBAT_REDUCE`, bulk by tag | Hardware DMA engines |
| SKB coherence | Per-line metadata + directory fields; single-core | Cross-core snoop / directory protocol |
| Physical map | `PhysMap.h` domain decode | Same nibbles |
| Paging / TLB | `PagingProfile.h`, `Tlb.h`, `Tlb.cpp`, `Paging.cpp`; **`EP`/`DP`**, **`ESR`/`DSR`**, **`TlbInsert`**, **`WireTlbEntry`**, wired slots | Full MMU |
| Exception stack | `ExceptionProfile.h`; **3** hardware `{IC, FR}` frames; **`IRET`** pops | Nested IRQ |
| User mode | `UserMode.cpp`; **`EU`/`DU`**, **`FR.UserMode` (bit 9)**; MMIO/privileged insn traps | Two-ring |
| Zero register | **`ZERO` @ `0x5C`**; reads 0, writes ignored | Always zero |
| FSX / SSX | `FRegs[16]`, `XRegs[16]`; shift/rotate packs; **`XCRYPT`/`XDECRYPT`/`XGEN`** (`SsxCrypto.cpp`) | Predicates / gather |
| NUMA | Simulated remote pools (`Numa.cpp`, 4 nodes) | Full mesh |
| Legacy DRAM | Domain **`0000`**, addr `< RamSize` → flat RAM | Firmware uses domain **`0100`** |

**Source layout**

| Component | Files |
|-----------|--------|
| ISA constants | `sources/Isa.h` |
| Physical address map | `sources/bvm/PhysMap.h` |
| K-Bank profile | `sources/bvm/KBankProfile.h` |
| K-Bank execution | `sources/bvm/KBank.cpp` |
| SKB profile | `sources/bvm/SkbProfile.h` |
| SKB execution | `sources/bvm/Skb.cpp` |
| Assembler K / SKB mnemonics | `sources/assembler/Assembler.cpp` |
| ALU shift integration | `sources/bvm/AluShiftProfile.h`; **`disp[8:0]`** on **`ADD`/`SUB`/…/`CMP`** |
| Indexed `LOAD`/`STORE` | `sources/bvm/MemAddrProfile.h`; **`[base+off]`**, **`[base+index+off]`** |
| CPLX profile | `sources/bvm/CplxProfile.h` |
| CPLX execution (scalar subset) | `sources/bvm/Cplx.cpp` |
| SSX crypto (AES-128 block, XGEN) | `sources/bvm/SsxCrypto.cpp` |
| Paging profile | `sources/bvm/PagingProfile.h` |
| Paging / TLB | `sources/bvm/Tlb.h`, `sources/bvm/Paging.cpp` |
| NUMA / hints | `sources/bvm/NumaProfile.h`, `sources/bvm/Numa.cpp` |
| NSX profile | `sources/bvm/NsxProfile.h` |
| NSX execution | `sources/bvm/Nsx.cpp` |
| Assembler NSX mnemonics | `sources/assembler/Assembler.cpp` (`EncodeNsxMnemonic`) |
| Examples | `examples/KBankDemo.s`, `examples/SkbDemo.s`, `examples/NumaDemo.s`, `examples/CplxDemo.s`, `examples/CplxVectorDemo.s`, `examples/PagingDemo.s`, `examples/NsxDemo.s` |

### NSX baseline limits (`NsxProfile.h`)

| Limit | Value |
|-------|-------|
| Conv / tensor H,W,C,K | ≤ 4 |
| Attention `N`, `D` | ≤ 4; `H` heads ≤ 2 |
| LSTM `hidden`, `input` | ≤ 4 |
| SSX lane ops | 4 FP32 lanes per `Xd` |
| Element types | FP32 only (other `opmode` traps) |

**Implemented in baseline `bvm`:** activations/conv/pool/RNN ops (see prior list), plus **`mse_loss`/`cross_entropy`/`l2_loss`**, **`transformer`** (96 B desc), **`fwd_start`/`fwd_end`** (tape backprop), **`sgd`**. Remaining NSX opcodes trap with a clear message.

**Verification:** `examples/NsxDemo.s` — **`R10 == 0x41200000`**. `examples/MlInferenceDemo.s` — legacy matmul/conv/qdot smoke, then **32-step** transformer training on **`sin(x²)`** (4 samples); **`LossScalar < 0.35`** after run (`hello_test`).

### Security (v1.0)

See [Honeycomb_Security.md](Honeycomb_Security.md). Baseline **`bvm`** implements PAC/CFI/SSP stubs, **syscall sandbox** (`SANDBOX_*` CSRs + `sandbox_allow`), **protected user memory regions**, encrypted load/store via SSX, simplified PCR **`MEASURE`**, and EL2 **VMCS** (`VMLAUNCH`/`VMRESUME`/`VMEXIT`/`VMREAD`/`VMWRITE` with guest/host fields in `SecurityProfile.h`). User violations trap to vector **`0x3E`**. Verified by **`examples/SecuritySandboxDemo.s`** (`R10 = 0xC0FEC0DE`).

### Compact encoding (16-bit)

**`FR.CompactMode` (bit 11)** selects 16-bit fetch (`sources/bvm/Compact.cpp`). **`CALL`/`RET`** save/restore mode on the stack; callee mode is **`target & 1`** (LSB from section encoding). Assembler **`.section …, compact|full`**. Verified by **`examples/CompactHybridDemo.s`** (`R10 = 0xC0FE0001`).

---

## Physical address routing (`PhysMap`)

| `[63:60]` | Region | BVM behavior |
|-----------|--------|--------------|
| `0000` | Local K-Bank | `LOAD`/`STORE` if offset `< 128 KB` and **not** legacy flat DRAM |
| `0000` | Legacy DRAM | Same nibble, offset `< RamSize` (16 MB) → `Ram[]` for compat |
| `0010` | Local SKB | `SkbData[]` at `0x2000_0000_0000_0000` |
| `0100` | Local DRAM | `Ram[]` at stripped offset |
| `0001`/`0011`/`0101` | Remote | Trap on single-core BVM |
| MMIO | Above RAM | `MmioWindow` aperture unchanged |

---

## Minimum viable silicon (reference)

| Feature | Single-Core MVP | Multi-Core Full |
|---------|-----------------|-----------------|
| K-Bank | 128 KB | 128 KB per core |
| SKB | 32 MB (can ship 4 MB fuse) | 32 MB + directory |
| NUMA | `NumaProfile.h` + `Numa.cpp` | Full XBAR mesh |
| Hints | SKB + advisory `HINT` | Scheduler integration |

---

## Device I/O (BVM)

All platform devices are **MMIO-only** (no programmed-I/O shims). Constants and layouts live in `DeviceSpecs.h` and `MmioMap.h`. Guest firmware talks to devices exclusively through 64-bit MMIO quads in the 4 KiB window above RAM.

Optional SDL3 integration (`-DHONEYCOMB_HOST_IO=ON`, runtime `--host-io`) backs the GOP display, xHCI HID, and AC97 audio paths.

### Machine configuration (CLI)

| Flag | Default | Purpose |
|------|---------|---------|
| `--ram` / `--mem` | `16M` | Flat DRAM size (MMIO base = RAM end) |
| `--fb-width` / `--fb-height` | `640`×`480` | Default GOP mode |
| `--audio-gain` | `1.0` | AC97 master gain |
| `--timer-period` | `100` | Platform timer ticks between periodic IRQs |
| `--disk` | `4M` | Block device backing store |
| `--disk-image` | _(none)_ | Persistent block image (load at start, save on exit) |
| `--no-ac97` | off | Disable AC97 |
| `--no-usb3` | off | Disable xHCI |
| `--no-block` | off | Disable block storage |
| `--no-uart` | off | Disable NS16550 UART |

### MMIO layout (quad byte offsets from window base)

| Offset range | Device | Standard |
|--------------|--------|----------|
| `0`–`32` | Platform | Boot control, identity, syscall |
| `40` | Platform | Device presence bitmask |
| `64`–`112` | AC97 | Intel AC'97 DMA + master volume + format |
| `128`–`184` | USB3 | xHCI 1.x compact + HID event ring |
| `256`–`352` | Display | UEFI GOP mode + linear framebuffer |
| `384`–`432` | Block | VirtIO-style 512-byte sectors |
| `448`–`464` | UART | NS16550 data + line status + control |
| `480`–`504` | IRQ ctrl | Pending/enable/vector map for device lines |
| `512`–`544` | Timer | HPET-style counter, compare, periodic tick |
| `560`–`592` | RTC | Unix wall clock + alarm placeholder |

#### Platform IRQ controller (`480+`)

Seven device lines (timer, RTC, UART, block, AC97, USB, display) raise a shared pending bitmask. Guest firmware unmasks lines in `IrqEnable`, maps per-line vectors in `IrqVectors`, and clears handled bits by writing `IrqPending`. When `EI` is set in `FR`, the VM delivers the mapped vector automatically during `WAIT` / the device service loop.

Default vectors are `0x30` + line index (see `DeviceSpecs::IrqLine`).

#### Platform timer (`512+`)

`TimerCounter` advances from retired CPU cycles (`TimerCyclesPerTick` in `VmConfig`, default 100000). Program `TimerPeriod` for periodic scheduler ticks (default 100). One-shot compare uses `TimerCompare` + `TimerControl.OneShot`.

#### Platform RTC (`560+`)

`RtcSeconds` / `RtcNanoseconds` expose wall time. With `--host-io`, the clock tracks the host; headless runs advance simulated seconds from the timer. `RtcAlarmSeconds` + `RtcControl.AlarmEnable` fire an RTC IRQ when due.

#### UEFI GOP display (`256+`)

| Offset | Field |
|--------|-------|
| `256` | Version (`0x00010000`) |
| `264` | HorizontalResolution |
| `272` | VerticalResolution |
| `280` | PixelFormat (UEFI `EFI_GRAPHICS_PIXEL_FORMAT`) |
| `288` | PixelsPerScanLine |
| `296` | FrameBufferBase |
| `304` | FrameBufferSize |
| `312` | Control (`Enable`, `Redraw`, `VsyncIrq`) |
| `320` | Status (`Ready`, `VerticalSync`) |

Pixel format `0` = `PixelRedGreenBlueReserved8BitPerColor` (X8R8G8B8); `1` = B8G8R8X8.

#### USB3 xHCI HID (`128+`)

Program `EventRingPtr`, set `Command.Run`, poll `EventPending` or advance `EventReadIndex`. Each 32-byte ring entry: `{ type, data0, data1, reserved }`.

| Type | Event |
|------|-------|
| `1` | Key down |
| `2` | Key up |
| `3` | Mouse buttons |
| `4` | Mouse motion |

#### Block storage (`384+`)

Write `Lba`, `BufferPtr`, optional `SectorCount` (default 1, max 128), then `Command` (`1`=read, `2`=write, `3`=flush). Poll `Status` (`1`=complete, `3`=busy during transfer).

#### UART (`448+`)

Write characters to `UartData`; read when `UartLineStatus.DataReady`. TX always goes to stdout; RX is fed from the host keyboard when `--host-io` is active, otherwise from stdin (non-blocking).

Guest firmware can use `WAIT` to yield while polling device status; the VM services devices every 512 instructions and on each `WAIT`.

Implementation: `DeviceSpecs.h`, `MmioMap.h`, `GuestMem.h`, `Devices.cpp`, `PlatformIrq.cpp`, `PlatformTimer.cpp`, `PlatformRtc.cpp`, `Display.cpp`, `Usb3.cpp`, `Ac97.cpp`, `Block.cpp`, `Uart.cpp`, `HostIo.cpp`.

---

## Verification checklist

- K-Bank wrap at `KMASK` = `0x3FFF`
- SKB line index `< 524288` (32 MB / 64 B)
- `SKB_BIND` aliases bound window to SKB line data
- `SKB_MOVE` remaps metadata without copying bytes twice
- Remote domain access traps with clear error
- Legacy examples still run (domain-0 low addresses → DRAM)
- Re-encode binaries after register migration (`SP` = `0x20`)
- **`WINSZ`** carries complex tag in upper bits; **`ctype`** asm alias; code **`0x5C`** reserved
- Opcode map lists all **`0x072..0x08F`** K/streaming ops with **✓** vs **stub** footnotes

---

*Normative opcode and register definitions live in [Honeycomb.md](Honeycomb.md).*
