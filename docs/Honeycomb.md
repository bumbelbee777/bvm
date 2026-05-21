# Honeycomb v0.7-SKB Reference

Normative ISA for Honeycomb v0.7-SKB (HPC edition): v0.6 opcode groups plus **K-Bank**, **SKB** (Shared Secondary K-Bank), **CPLX** complex aliases, expanded GPRs, and index registers. **Baseline `bvm`** implements the full v0.7-SKB memory map on a single core — see [Honeycomb_Implementation.md](Honeycomb_Implementation.md).

---

## Architectural overview (v0.7)

Honeycomb v0.7-SKB adds a **four-tier memory hierarchy**: hot GPRs, warm per-core **K-Bank**, shared **SKB**, and **DRAM**. The fixed **64-bit** instruction word and **6-bit** register fields are unchanged from v0.6.

| Principle | Summary |
|-----------|---------|
| Size irrelevance | Data larger than K-Bank streams in chunks (`STREAM*`) |
| Zero-copy binding | `SKB_BIND` aliases SKB lines into K-Bank slots (no copy) |
| Explicit SKB | Software places working sets in SKB; not a hidden cache |
| Coherent SKB | Socket-wide coherence on SKB lines; K-Bank remains private |
| Explicit NUMA | Remote socket SKB/DRAM via address domain + **`NUMA*`** ops (simulated pools in baseline BVM) |

**Memory tiers (latency model)**

| Tier | Resource | Size | Latency (typical) |
|------|----------|------|-------------------|
| 0 Hot | `R0..R31` | 256 B | 1 cycle (private) |
| 1 Warm | `X0..X15` + per-core K-Bank | **256 KB** token slots (DAX) + SSX | 1–3 cycles (private) |
| 2 Shared | **SKB** | 32 MB (socket) | 5–15 cycles (coherent) |
| 3 Memory | DRAM | — | 100+ cycles |

---

# 1) Opcode map

**Notes:** opcode field is 11 bits (`[63:53]`). Values shown are 11-bit hex (0x000..0x7FF). I grouped related operations and left gaps for future expansion.

**Operand notation**

* **`reg`** — a scalar register coded in the base word (`rd`, `rs`, or `rt` per instruction). GPRs (`R0..R31`) plus `SP`, `IC`, `FR`, `PTB`, **`IVB`**, **`IVL`**, **`WIND`**, **`I0..I3`**, etc., where wired.
* **`imm`** — a **follow-on 64-bit word** after the base encoding when **`imm1_flag`** (and optionally **`imm2_flag`**) is set in the instruction. Not the same thing as **`disp`**.
* **`imm128`** — two follow words (`imm1` then `imm2`), high 64 bits first (big-endian style at the ISA boundary).
* **`addr`** — either a short offset in **`disp`**, a full address in **`imm1`**, or assembler syntax like `[symbol]` that lowers to one of those.
* **`reg128` / `regf`** — SSX / FSX operand classes (separate opcode groups).
* **`immf`** — 32-bit float value carried in a 64-bit follow word (upper bits reserved/zero unless specified).

**Three-operand ALU pattern**

Many integer ops use **`rd, rs, op3`** where **`op3`** is either register **`rt`** (**`imm1_flag = 0`**) or an immediate in **`imm1`** (**`imm1_flag = 1`**, **`rt` unused**). Assemblers may write **`ADD rd, rs, rt`** or **`ADD rd, rs, #imm`**.

**Instruction classes (overview)**

| Class | Typical `opmode` | Operand registers | Notes |
| ----- | ---------------- | ----------------- | ----- |
| **Scalar GPR** | `000` (int64), `001`–`010` (FP lanes) | `rd`, `rs`, `rt` / `imm1` | § Integer ALU, branches, `LOAD`/`STORE`. |
| **Atomic memory** | often `110` + microcode | address + value fields | § Atomic; RMW to coherent memory. |
| **SSX vector** | `011` / `100` / `101` | logical **`X*`** (see § SSX encoding) | Lane-wise ops; **VL** and **element type** from `opmode` + **CSR/profile**. |
| **FSX scalar FP** | `001` / `010` | **`F*`** | Same **RRR / RRI** pattern as integers with **`immf`**. |

---

## System & misc

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x000 | NOP      | —        | No-op |
|  0x001 | HLT      | —        | Halt processor |
|  0x002 | WAIT     | —        | Spin / yield hint (implementation-defined) |
|  0x022 | PAUSE    | —        | **Baseline `bvm`:** pause / spin hint (distinct from **`WAIT`** for profiling) |
|  0x023 | ESR      | —        | Enable **software TLB refill** — set **`FR.SoftwareTlbRefill` (bit 10)** (supervisor-only) |
|  0x024 | DSR      | —        | Disable software TLB refill (supervisor-only) |
|  0x025 | TlbInsert | —       | Walk **`PTB`** for **`TlbFaultAddr`** / **`TlbFaultKind`** and insert into TLB (supervisor-only) |
|  0x026 | WireTlbEntry | `#slot` | Mark TLB slot **`slot`** (0..63) **wired** — not evicted by **`FlushVirtual`** (supervisor-only) |
|  0x027..0x028 | *reserved* | — | (was compact mode ops; use **CALL**/**RET** + section encoding) |
|  0x003 | EI       | —        | Set interrupt-enable bit in **`FR`** |
|  0x004 | DI       | —        | Clear interrupt-enable bit in **`FR`** |
|  0x005 | INT      | `#vec` (`imm`) | Software interrupt vector **`vec`** (**`imm1`**, 8‑bit semantics; larger immediate encodings reserved) |
|  0x006 | IRET     | —        | Return from interrupt — pop one **hardware exception frame** (see § Interrupts) |
|  0x007 | SYSCALL  | `#num` (`imm`) | **Baseline `bvm`:** **`Imm1`** records **`MmioLastSyscallNumberRegister`** and vectors to **`MmioSyscallProgramCounter`** (also wired at MMIO quad **SyscallHandlerIc**). Traps if that PC was never programmed. |
|  0x008 | SYSRET   | —        | **Baseline `bvm`:** Pops **`FR`** then **`IC`** from the **`SYSCALL`** frame (must not be mixed with **`IRET`**). |
|  0x009 | PERFMON  | **`rd`, `#counter` (`imm`)** | `rd ← perf_counter(sel)`; selector in **`imm1`**, destination in **`rd`** |
|  0x00A | EP       | —        | Enable paging: set **`FR.Paging`**, flush TLB; requires non-zero **`PTB`** (supervisor-only in baseline `bvm`) |
|  0x00B | DP       | —        | Disable paging: clear **`FR.Paging`**, flush TLB (supervisor-only) |
|  0x020 | EU       | —        | **Baseline `bvm`:** enter user mode — set **`FR.UserMode` (bit 9)**; traps if already user |
|  0x021 | DU       | —        | **Baseline `bvm`:** return to supervisor — clear **`FR.UserMode`**; supervisor-only |
|  0x00C | SAVWIN   | —        | Save register window (`CWP++`) — *not baseline BVM* |
|  0x00D | RESWIN   | —        | Restore window (`CWP--`) — *not baseline BVM* |
|  0x00E | FLUSHWIN | `#count` | Flush oldest windows to memory — *future* |
|  0x00F | RESTWIN  | `#count` | Restore windows from memory — *future* |

---

## K-Bank, spill, streaming (0x072..0x08F)

**K address (baseline BVM):** `K_addr = f(WIND, I[Is], disp) & KMASK` where **`disp[29:28]`** selects the mode:

| `disp[29:28]` | Mode | Address |
| ------------- | ---- | ------- |
| `00` | Flat | `WIND + index + disp[27:0]` |
| `01` | Windowed | `(CWP × WINSZ[15:0]) + ((WIND + index + offset) mod WINSZ[15:0])` |
| `10` | Rotating | `ROTBAS + ((WIND + index + offset) mod ROTSZ)` |
| `11` | Reserved | — |

**Implementation legend:** **✓** = baseline `bvm` executes semantics; **stub** = decodes, no-op or trap on single-core; **future** = not in baseline `bvm`.

### Indirect GPR & SSX half-spill (0x072..0x076) — ✓

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
| 0x072 | IGLOAD | **`rd`, `[Is]`** | **`GPR[rd] ← GPR[(WIND + I[Is] + disp) mod 32]`** (indirect scalar load) |
| 0x073 | IGSTORE | **`rd`, `[Is]`** | **`GPR[(WIND + I[Is] + disp) mod 32] ← GPR[rd]`** |
| 0x074 | IWIND | **`rs` / `#imm`** | Alias of **`KWIND`** — update **`WIND`** |
| 0x075 | XSPILL | **`Xd`, `rs`, #which`** | Low/high **64** bits of **`Xd`** → **`GPR[rs]`** (`which` ∈ {0,1}) |
| 0x076 | XRELOAD | **`Xd`, `Xs`, #which`** | Merge **`GPR`** half into **`Xd`** from **`Xs`** |

### K-Bank load/store (0x077..0x07B) — ✓

| Opcode | Mnemonic | Operands | imm1 | Semantics |
| -----: | -------- | -------- | ---- | --------- |
| 0x077 | KLOAD | **`rd`, `[Is]`** | No | **`GPR[rd] ← K[addr]`** |
| 0x078 | KSTORE | **`rd`, `[Is]`** | No | **`K[addr] ← GPR[rd]`** |
| 0x079 | KLOADI | **`rd`, #imm5`** | Yes | **`GPR[rd] ← K[(WIND + imm5) & KMASK]`** |
| 0x07A | KSTOREI | **`rd`, #imm5`** | Yes | **`K[(WIND + imm5) & KMASK] ← GPR[rd]`** |
| 0x07B | KWIND | **`rs` / `#imm`** | Yes | **`WIND ← GPR[rs]`** or **`WIND ← imm1`** |

### Burst moves & rotate (0x07C..0x07E) — ✓

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
| 0x07C | KRELOAD | **`Xd`, `[Is]`** | **128-bit** `X[Xd] ← K[addr]‖K[addr+1]` (two K-slots) |
| 0x07D | KSPILL | **`Xd`, `[Is]`** | **`K[addr]`, `K[addr+1] ←` halves of **`X[Xd]`** |
| 0x07E | KROT | **`rd`, `[Is]`** | **`GPR[rd] ← K[addr]`** with **rotating** `disp[29:28]=10` (requires **`ROTSZ ≠ 0`**) |

### Window spill metadata (0x080..0x081) — ✓

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
| 0x080 | RDWIN | **`rd`, #sel`** | Read window CSR: **`sel`** 0=`CWP`, 1=`CANSAVE`, 2=`CANRESTORE`, 3=`OTHERWIN`, 4=`ROTBAS`, 5=`WINSZ` |
| 0x081 | WRWIN | **`rd`, #sel`** | Write selected window CSR from **`GPR[rd]`** |

(`SAVWIN`/`RESWIN`/`FLUSHWIN`/`RESTWIN` remain in § System & misc at **`0x00C..0x00F`**.)

### Streaming DMA (0x082..0x088) — ✓ sync / stub bind

| Opcode | Mnemonic | Operands | Baseline `bvm` |
| -----: | -------- | -------- | -------------- |
| 0x082 | STREAM | **`#tag`, `#len`, `addr` (`imm1`)** | **✓** Synchronous **`memcpy`** DRAM→K-Bank; marks tag done |
| 0x083 | STREAMOUT | **`#tag`, `#len`, `addr` (`imm1`)** | **✓** K-Bank→DRAM |
| 0x084 | STREAMADV | — | **✓** **`WIND ← (WIND + WINSZ[15:0]) & KMASK`** (ring advance) |
| 0x085 | STREAMWAIT | **`#tag`** | **✓** Traps if tag incomplete (sync model always complete) |
| 0x086 | STREAMFENCE | — | **✓** Marks all stream tags complete |
| 0x087 | STREAMBIND | **`#tag`, `#slot`** | **✓** Bind stream tag → K-Bank window (`WIND = slot × WINSZ[15:0]`) |
| 0x088 | STREAMUNBIND | **`#tag`** | **✓** Clear stream bind for tag |

### NUMA & cross-socket (0x089..0x08D) — ✓ simulated

Remote domains **`0001`/`0011`/`0101`** are backed per-node pools in baseline **`bvm`** (see `NumaProfile.h`). Address form: **`[63:60]=domain`**, **`[59:56]=node`**, **`[55:0]=offset`**.

| Opcode | Mnemonic | Operands | Baseline `bvm` |
| -----: | -------- | -------- | -------------- |
| 0x089 | NUMABIND | **`#remote_line`, `#slot` [, `#node`]** | **✓** Zero-copy K-Bank view of remote SKB line |
| 0x08A | NUMASTREAM | **`#tag`, `#len`, remote_addr`** / **`numastreamout`** | **✓** Remote DRAM ↔ K-Bank (`opmode=1` = out) |
| 0x08B | NUMAFENCE | — | **✓** Complete NUMA/stream tags + hint barrier |
| 0x08C | NUMAADV | **`#node`** (optional) | **✓** Advance per-node remote ring pointer |
| 0x08D | NUMAATOMIC | **`rd`, `#remote_line`** | **✓** Remote SKB word atomic add |

### Software hints (0x08E..0x08F) — ✓

| Opcode | Mnemonic | Operands | Baseline `bvm` |
| -----: | -------- | -------- | -------------- |
| 0x08E | HINT | **`#type`, `#line`** | **✓** SKB/NUMA advisory (`imm1 = type«24 \| target«16 \| line«6`) |
| 0x08F | HINTBARRIER | — | **✓** Commit hint epoch + fence streams |

See [Honeycomb_Implementation.md](Honeycomb_Implementation.md) and `examples/KBankDemo.s`.

**K-Bank size (v0.7-DAX):** **256 KB** = **16,384** token slots × **16 B** (baseline `bvm`). Legacy **`KLOAD`/`KSTORE`** read/write the **64-bit data** field only; DAX metadata (`valid`, `tag`, `refcnt`) is separate.

---

## DAX — DAtaflow eXtensions (0x400..0x46F)

**DAX** turns the K-Bank into a **token dataflow engine**: instructions **fire when input tokens are ready**, not strictly when the program counter reaches them. Software constructs a DAG; hardware (or baseline `bvm` simulation) schedules nodes as tokens arrive.

| | Baseline Honeycomb | With DAX |
|---|-------------------|----------|
| Execution | Control flow (`IC`) | Control + dataflow |
| K-Bank | 128 KB flat (`kload`) | **256 KB** token slots |
| Parallelism | SSX vectors | Token graphs + SSX |
| Cross-core | Coherent `LOAD`/`STORE` | `DAX_SEND` / `DAX_RECV` (simulated mailboxes in `bvm`) |

**Design:** *explicit beats implicit* — same ISA family and **`k0..k63`** operands (**WIND-relative**, like **`kloadi`** indices).

### Token slot (16 B per K-index)

| Field | Bits | Role |
|-------|------|------|
| `data` | 64 | Payload at bytes **0..7** |
| `valid` | 1 | Token present (byte **8** bit 0) |
| `tag` | 8 | Debug / coherence tag (byte **8** `[15:8]`) |
| `refcnt` | 8 | Outstanding consumers (byte **8** `[23:16]`) |

**Consumption:** single-consumer ops decrement **`refcnt`** and invalidate at zero; **`DAX_FORK`** copies and bumps source **`refcnt`**; **`DAX_PEEK`** reads without consuming.

**Baseline `bvm` (Phase 1):** token metadata in software; on execute, if operands are not **valid**, **`IC`** stalls on the same instruction (synchronous dataflow). Wake-up scanning is not cycle-accurate.

### DAX base word encoding

Same 64-bit header as scalar ops; **`imm1_flag`/`imm2_flag`** are usually **0**. Operands are **WIND-relative K-slots** in **`rd`/`rs`/`rt`** (6 bits each). **`disp[29:24]`** = **`dax_subop`**; **`disp[23:0]`** = **`dax_extra`** (third K-source low 6 bits, compare **rel**, wait **mask**, branch true PC low 24 bits, etc.). **`opmode`**: `000` int64, `001` FP64, `010` FP32.

### Dataflow arithmetic (0x400..0x40F) — ✓ baseline

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
| 0x400 | `DAX_ADD` | `kD`, `kS`, `kT` | `kD ← kS + kT` when both valid |
| 0x401 | `DAX_SUB` | `kD`, `kS`, `kT` | Subtract |
| 0x402 | `DAX_MUL` | `kD`, `kS`, `kT` | Multiply |
| 0x403 | `DAX_DIV` | `kD`, `kS`, `kT` | Divide (trap div0) |
| 0x404 | `DAX_FMA` | `kD`, `kS`, `kT`, `kU` | `kD ← kS×kT + kU` (`kU` in **`dax_extra[5:0]`**) |
| 0x405 | `DAX_CMP` | `kD`, `kS`, `kT`, `#rel` | `kD ← (kS rel kT)`; **rel**: 0=EQ,1=NE,2=LT,3=GT,4=LE,5=GE |
| 0x406 | `DAX_SEL` | `kD`, `kC`, `kS`, `kT` | `kD ← kC ? kS : kT` (`kT` in **`dax_extra[5:0]`**) |
| 0x407 | `DAX_ADDI` | `kD`, `kS`, `#imm` | `imm1` word |
| 0x408 | `DAX_MULI` | `kD`, `kS`, `#imm` | |
| 0x409 | `DAX_CMPI` | `kD`, `kS`, `#imm`, `#rel` | |

### Dataflow vector (0x410..0x41F) — ✓ baseline

| Opcode | Mnemonic | Operands | Baseline `bvm` |
| -----: | -------- | -------- | -------------- |
| 0x410 | `DAX_VADD` | `kD`, `kS`, `kT` | **✓** FP32 lanes (`VL` from `SsxVectorLength`) |
| 0x411 | `DAX_VMUL` | `kD`, `kS`, `kT` | **✓** |
| 0x412 | `DAX_VFMA` | `kD`, `kS`, `kT`, `kU` | **✓** |
| 0x413 | `DAX_VLOAD` | `kD`, `#addr` | **✓** `VL` tokens |
| 0x414 | `DAX_VSTORE` | `kS`, `#addr` | **✓** |

Each lane uses consecutive WIND-relative K-slots with independent `valid` bits.

### Dataflow control (0x420..0x42F)

| Opcode | Mnemonic | Operands | Baseline `bvm` |
| -----: | -------- | -------- | -------------- |
| 0x420 | `DAX_FORK` | `kD1`, `kD2`, `kS` | **✓** duplicate token |
| 0x421 | `DAX_JOIN` | `kD`, `kS`, `kT` | **✓** sync when both valid |
| 0x422 | `DAX_MERGE` | `kD`, `kS`, `kT` | **✓** OR-join (either) |
| 0x423 | `DAX_BRANCH` | `kC`, `#true`, `#false` | **✓** `imm1` = false PC |
| 0x424 | `DAX_CALL` | `kC`, `#target` | **✓** |
| 0x425 | `DAX_RET` | `kC` | **✓** |
| 0x426 | `DAX_LOOP` | `kCount`, `kInit`, `kBody` | **✓** (`kBody` in `dax_extra[5:0]`) |
| 0x427 | `DAX_SERIALIZE` | `kD`, `kS` | **✓** ordered pass-through |

### Token management (0x430..0x43F) — ✓

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
| 0x430 | `DAX_TOKEN` | `kD`, `Rs` / `#imm` | Create token |
| 0x431 | `DAX_CONSUME` | `kD` | Invalidate |
| 0x432 | `DAX_PEEK` | `Rd`, `kS` | Read data to GPR |
| 0x433 | `DAX_PROMOTE` | `kS`, `#line` | **✓** SKB line word 0 + meta |
| 0x434 | `DAX_DEMOTE` | `kD`, `#line` | **✓** Import SKB → token |
| 0x435 | `DAX_REFILL` | `kD`, `Rs` / `#imm` | Set **`refcnt`** |
| 0x436 | `DAX_VALID` | `Rd`, `kS` | `Rd ← valid` |
| 0x437 | `DAX_REFCNT` | `Rd`, `kS` | |
| 0x438 | `DAX_TAG` | `Rd`, `kS` | |
| 0x439 | `DAX_WAIT` | `kS` | Block until valid |
| 0x43A | `DAX_WAIT_ALL` | `#mask` | Low 24 bits of **`dax_extra`** |
| 0x43B | `DAX_WAIT_ANY` | `Rd`, `#mask` | `Rd ← first set bit index |

### Dataflow memory (0x440..0x44F)

| Opcode | Mnemonic | Baseline `bvm` |
| -----: | -------- | -------------- |
| 0x440 | `DAX_LOAD` | **✓** sync load → token |
| 0x441 | `DAX_STORE` | **✓** store + consume |
| 0x442 | `DAX_ATOMIC` | `kD`, `#addr`, `#op` [, operand] | **✓** RMW → token (old value) |
| 0x443 | `DAX_STREAM` | `kD`, `#line`, `#len` | **✓** SKB burst → token sequence |
| 0x444 | `DAX_GATHER` | `kD`, `#line`, `kI` | **✓** Indexed SKB word |

### Dataflow matrix / cross-core (0x450..0x46F) — ✓ baseline

| Opcode | Mnemonic | Baseline `bvm` |
| -----: | -------- | -------------- |
| 0x450 | `DAX_MATMUL` | **✓** Descriptor matmul → one token per output element |
| 0x451 | `DAX_CONV` | **✓** Descriptor conv2d → output tokens |
| 0x452 | `DAX_REDUCE` | **✓** Pairwise tree step |
| 0x453 | `DAX_SCAN` | **✓** Prefix scan over `VL` lanes |
| 0x460 | `DAX_SEND` | **✓** Mailbox `[core][slot]` |
| 0x461 | `DAX_RECV` | **✓** Blocking mailbox receive |
| 0x462 | `DAX_BCAST` | **✓** `rt`=dest slot, mask in `dax_extra` |
| 0x463 | `DAX_REDUCE_SCATTER` | **✓** Reduce across mask, scatter per core |

### Example (baseline)

```asm
kwind #0
dax_token k0, #5
dax_token k1, #3
dax_add   k4, k0, k1      ; fires when k0,k1 ready
dax_wait  k6
dax_peek  r0, k6
```

See `examples/DaxDemo.s` (**`(5+3)×(2+4) = 48`**).

---

## SKB — Shared Secondary K-Bank (0x090..0x0A4)

The **SKB** is a **32 MB** on-chip SRAM pool shared by all cores in a socket. It is an explicitly managed tier between per-core **K-Bank** (**256 KB** token slots with DAX) and off-chip **DRAM**. Lines are **64 bytes** with **8-byte metadata** and a co-located directory entry per line. Baseline **`bvm`** models the full **32 MB** pool and address map plus **simulated remote** K/SKB/DRAM per node (`Numa.cpp`).

**Physical organization (architected)**

| Block | Size | Role |
|-------|------|------|
| Banked data | 16 × 2 MB | 512K lines × 64 B |
| Directory | 4 MB | Per-line sharer/owner state (co-located metadata) |
| Tag store | 512K × 40-bit tags | Line lookup by physical tag |

**Line metadata (8 B per line, architected):** `state`, `owner_mask`, `pending_mask`, `version`, `lru_age`, `home_node`, `lock_count`, `flags`.

**Coherence states**

| State | Code | Meaning |
|-------|------|---------|
| `INVALID` | 0 | Not present |
| `SHARED` | 1 | Read-only, multi-core may cache |
| `EXCLUSIVE` | 2 | Writable, one core |
| `MODIFIED` | 3 | Dirty; writeback on evict |
| `PINNED` | 4 | Software-pinned |
| `TRANSITION` | 5 | In-flight transaction |

### Address space (`[63:60]` domain)

| Domain | Meaning |
|--------|---------|
| `0000` | Local per-core K-Bank (**256 KB** token slots with DAX, private) |
| `0001` | Remote core K-Bank (XBAR) |
| `0010` | **Local SKB** (32 MB, coherent) |
| `0011` | Remote socket SKB |
| `0100` | Local DRAM |
| `0101` | Remote DRAM |
| `0110`–`1111` | MMIO / reserved |

**SKB window (domain `0010`):** base **`0x2000_0000_0000_0000`**, size **32 MB** (`0x2000_0000_0000_0000` … `0x2000_0000_0200_0000`).  
`imm1` for SKB opcodes: `[63:60]=0010`, `[59:6]=line index`, `[5:0]=byte-in-line (sub-line ops may RMW)`.

**`LOAD`/`STORE`** to domain **`0010`** use the architected SKB window. Domain **`0000`** below **`RamSize`** remains **legacy flat DRAM** for existing binaries; canonical DRAM is domain **`0100`** (`0x4000_0000_0000_0000` + offset).

### SKB data movement

| Opcode | Mnemonic | Operands | Description |
| -----: | -------- | -------- | ----------- |
| 0x090 | `SKB_LOAD` | `Rd`, `#line` | Load from SKB line (`opmode`: GPR 8 B, SSX 16 B, burst 64 B) |
| 0x091 | `SKB_STORE` | `Rd`, `#line` | Store to SKB line |
| 0x092 | `SKB_BIND` | `#line`, `Rs=slot` | Zero-copy alias SKB line → K-Bank slot |
| 0x093 | `SKB_UNBIND` | `#line`, `#slot` | Release binding |
| 0x094 | `SKB_FLUSH` | `#line` | Writeback `MODIFIED` → DRAM |
| 0x095 | `SKB_INVALIDATE` | `#line` | Evict line |
| 0x096 | `SKB_PREFETCH` | `#line` | Non-binding prefetch |
| 0x097 | `SKB_ZERO` | `#line` | Allocate zeroed line |

### SKB atomics

| Opcode | Mnemonic | Operands |
| -----: | -------- | -------- |
| 0x098 | `SKBAT_LOAD` | `Rd`, `#line` |
| 0x099 | `SKBAT_STORE` | `Rd`, `#line` |
| 0x09A | `SKBAT_ADD` | `Rd`, `#line` |
| 0x09B | `SKBAT_CAS` | `Rd`, `Rs`, `#line` |
| 0x09C | `SKBAT_SWAP` | `Rd`, `#line` |
| 0x09D | `SKBAT_REDUCE` | `Rd`, `#line`, `#op` |

### SKB management

| Opcode | Mnemonic | Operands |
| -----: | -------- | -------- |
| 0x09E | `SKB_PIN` | `#line` |
| 0x09F | `SKB_UNPIN` | `#line` |
| 0x0A0 | `SKB_MOVE` | `#src`, `#dst` | Remap without copy |
| 0x0A1 | `SKB_BULK_LOAD` | `#tag`, `#count`, `#addr` |
| 0x0A2 | `SKB_BULK_STORE` | `#tag`, `#count`, `#addr` |
| 0x0A3 | `SKB_BULK_ZERO` | `#tag`, `#count`, `#addr` |
| 0x0A4 | `SKB_STATUS` | `Rd`, `#line` | Read line state code |

### SKB software hints (`HINT` **opcode `0x08E`** — type subfield in `imm1`)

These are **not opcodes** — they are the **`type`** byte inside **`imm1`** for mnemonic **`HINT`**. Do not confuse with atomic **`ATAND (0x060)`** or register **`CTYPE (0x5C)`**.

| Type | Name | Target |
| ---- | ---- | ------ |
| 0x60 | `HINT_SKB_TOUCH` | Line |
| 0x61 | `HINT_SKB_PIN` | Line |
| 0x62 | `HINT_SKB_UNPIN` | Line |
| 0x63 | `HINT_SKB_PREFETCH` | Line |
| 0x64 | `HINT_SKB_EVICT` | Line |
| 0x65 | `HINT_SKB_MIGRATE` | Node |
| 0x70–0x72 | `HINT_SKB_BULK_*` | Count |
| 0x80–0x83 | acquire / release / fence / flush | Line / scope |
| 0x90–0x92 | NUMA SKB place / affinity / replicate | Node / mask |

Example: `examples/SkbDemo.s`.

---

## Complex numbers (v0.7-CPLX, opcodes `0x0B0`..`0x0DA`, vectors `0x1A0`..`0x1BF`)

Complex values **alias** existing GPRs (and SSX/K-Bank in full silicon). No extra physical register file in baseline BVM.

### Type modes (`opmode = 101`, `disp[29:27] = 100`)

| `disp[26:24]` | Mode | Container (GPR) |
| ------------- | ---- | ----------------- |
| `000` | **C_FP64** (default) | `CRk = { R(2k) real, R(2k+1) imag }`, `k = 0..15` |
| `001` | **C_FP32** | `CRk = { Rk[63:32] real, Rk[31:0] imag }` |
| `010`–`111` | FP16 / BF16 / INT32 / INT64 | Profile-defined packing |

**Complex type tag (merged into `WINSZ`, code `0x27`):** there is no separate **`CTYPE`** register. Former **`CTYPE`** fields live in the upper bits of **`WINSZ`**; mnemonic **`ctype`** is an assembler alias for **`winsz`**. Do not use GPR code **`0x60`** for a type register — **`0x060`** is opcode **`ATAND`**.

### Scalar complex (baseline BVM)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
| 0x0B0 | `CADD` | `CRd, CRs, CRt` | |
| 0x0B1 | `CSUB` | `CRd, CRs, CRt` | |
| 0x0B2 | `CMUL` | `CRd, CRs, CRt` | |
| 0x0B5 | `CMAG` | `Rd, CRs` | Real scalar result in `Rd` |
| 0x0B7 | `CCONJ` | `CRd, CRs` | |
| 0x0B8 | `CNEG` | `CRd, CRs` | |
| 0x0C0 | `CFMA` | `CRd, CRs, CRt` | `CRd += CRs × CRt` |
| 0x0C1 | `CFMS` | `CRd, CRs, CRt` | `CRd -= CRs × CRt` |
| 0x0C2 | `CNFMA` | `CRd, CRs, CRt` | `CRd = -CRd + CRs × CRt` |
| 0x0C3 | `CNFMS` | `CRd, CRs, CRt` | `CRd = -CRd - CRs × CRt` |
| 0x0C4 | `CMAC` | `CRd, CRs, CRt` | Alias of `CFMA` |
| 0x0C5 | `CMSQ` | `Rd, CRs` | \|z\|² in `Rd` |
| 0x0C8 | `CLOAD` | `CRd, [<addr>]` | Interleaved FP64: real @ addr, imag @ addr+8 |
| 0x0C9 | `CSTORE` | `CRd, [<addr>]` | |
| 0x0CC | `CMOVE` | `CRd, CRs` | |
| 0x0CE | `CEXTRACT` | `Rd, CRs, #which` | `#0` real, `#1` imag |
| 0x0D0 | `CPACK` | `CRd, Rs, Rt` | Build complex from two GPR scalars |

Additional scalar opcodes (`CDIV`, `CARG`, `CSCALE`, `CROT`, `CEXP`, `CLOG`, `CPOW`, `CSQRT`, `CMAGMUL`, `CDOT`, `CNORM`, SoA load/store, `CSWAP`, `CINSERT`, `CUNPACK`, `CSPLAT`, compares, `CSEL`, `CMIN`/`CMAX`, **`CKLOAD`/`CKSTORE`**) are implemented in baseline BVM. Complex strides use **`WINSZ[23:16]`** (GPR) and **`WINSZ[39:32]`** (K-Bank); precision tag in **`WINSZ[31:24]`**.

### Vector complex (baseline BVM, C_FP32 in SSX)

**Layout:** each `CXk` aliases `Xk` with two complexes per 128-bit register (four FP32 lanes: real₀, imag₀, real₁, imag₁). Use **`SsxVectorLength = 4`** (four FP32 elements) so `xload` fills both complexes.

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
| 0x1A0 | `VCADD` | `CXd, CXs, CXt` | |
| 0x1A1 | `VCSUB` | `CXd, CXs, CXt` | |
| 0x1A2 | `VCMUL` | `CXd, CXs, CXt` | |
| 0x1A4 | `VCFMA` | `CXd, CXs, CXt` | Per-lane `CXd += CXs × CXt` |
| 0x1A5 | `VCFMS` | `CXd, CXs, CXt` | Per-lane `CXd -= CXs × CXt` |
| 0x1A6 | `VCMAC` | `CXd, CXs, CXt` | Alias of `VCFMA` |
| 0x1A9 | `VCCONJ` | `CXd, CXs` | |
| 0x1AE | `VCFFT2` | `CXd, CXs` | Radix-2 butterfly, W=1+0i |
| 0x1AF | `VCFFT4` | `CXd, CXs, CXt` | Radix-4 on four complexes (`CXs` + `CXt`) |
| 0x1A3 | `VCDIV` | `CXd, CXs, CXt` | |
| 0x1A7 | `VCMAG` | `CXd, CXs` | Magnitude per complex lane |
| 0x1A8 | `VCARG` | `CXd, CXs` | |
| 0x1AA | `VCSCALE` | `CXd, CXs, Xt` | Scale by real in `Xt` lane 0 |
| 0x1AB | `VCROT` | `CXd, CXs, Xt` | Rotate by angle in `Xt` |
| 0x1AC | `VCDOT` | `CRd, CXs, CXt` | |
| 0x1AD | `VCNORM` | `Rd, CXs` | |
| 0x1B0 | `VCLOAD` | `CXd, [<addr>]` | Interleaved FP32 complex |
| 0x1B1 | `VCSTORE` | `CXd, [<addr>]` | |
| 0x1B2 | `VCGATHER` | `CXd, [<base>], Ii` | |
| 0x1B3 | `VCSCATTER` | `CXd, [<base>], Ii` | |
| 0x1B4 | `VCBROADCAST` | `CXd, CRs` | |
| 0x1B5 | `VCSPLAT` | `CXd, #imm` | |
| 0x1B6 | `VCREDUCE` | `CRd, CXs, #op` | `#0` sum, `#2` norm |
| 0x1B7 | `VCCMP` | `CXs, CXt, #rel` | Sets **`KMASK`** lane bits |
| 0x1B8 | `VCSEL` | `CXd, CXs, CXt` | Uses **`KMASK`** bit 0 |
| 0x1B9–0x1BA | `VCMIN`/`VCMAX` | `CXd, CXs, CXt` | By magnitude |
| 0x1BB–0x1BD | `VCZIP`/`VCUNZIP`/`VCSWAP` | | Pack/unpack/swap lanes |
| 0x1BE | `VCREVERSE` | `CXd, CXs` | Bit-reverse complex lane index |
| 0x1BF | `VCTWIDDLE` | `CXd, CXs, #k` | Multiply by \(W_N^k\) |

SKB complex uses existing **`SKB_BIND`** + complex ALU on aliased K-Bank (no separate SKB complex opcodes).

Examples: `examples/CplxDemo.s` (scalar), `examples/CplxVectorDemo.s` (vector + `CFMA` + FFT).

---

## Integer ALU (group 0x010..0x02F)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x010 | ADD      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | `rd ← rs + op3` (optional **shift-integrated** op3; see below) |
|  0x011 | SUB      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | `rd ← rs − op3` |
|  0x012 | MUL      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | `rd ← rs × op3` (unsigned lower 64; high part optional profile) |
|  0x013 | DIV      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | **Signed** `int64` division |
|  0x014 | MOD      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | **Signed** remainder |
|  0x015 | INC      | `rd` | **Baseline / BVM:** increment **`GPR[rd]`** (**`imm1_flag = 0`**, **`rs`/`rt` unused**). *Profile:* memory increment uses explicit **`LOAD`/`STORE`/`ATINC` (0x06C)** or RMW helpers—not this opcode reinterpreted silently. |
|  0x016 | DEC      | `rd` | **Baseline / BVM:** decrement **`GPR[rd]`**. *Profile:* memory decrement uses **`ATDEC` (0x06D)** or explicit **`LOAD`/modify/`STORE`**. |
|  0x017 | CMP      | `rs`, `rt` / `rs`, `#imm` | Compare **`rs`** vs **`op3`**: **`rt`** if **`imm1_flag = 0`**, else **`imm1`**. **`rd` = 0`**. Updates **`FR`**. |
|  0x018 | AND      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Bitwise AND |
|  0x019 | OR       | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Bitwise OR |
|  0x01A | XOR      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Bitwise XOR |
|  0x01B | NOT      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | **`rd ← ~rs`**. **`rt`/immediate operand ignored.** |
|  0x01C | RSH      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Logical right shift: amount from **`rt`** or **`imm1`** (typically **`0…63`**) |
|  0x01D | LSH      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Left shift |
|  0x01E | ROR      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Rotate right |
|  0x01F | ROL      | `rd`, `rs`, `rt` / `rd`, `rs`, `#imm` | Rotate left |

**Shift-integrated ALU (baseline `bvm`)** — For **`ADD`/`SUB`/`MUL`/`AND`/`OR`/`XOR`/`CMP`**, the base word **`disp[8:0]`** may pre-shift the **third operand** before the operation:

| `disp[8:6]` | Shift on op3 |
| ----------- | ------------ |
| `000` | none |
| `001` | logical left (`<<`) |
| `010` | logical right (`>>`) |
| `011` | rotate left (`<<<`) |
| `100` | rotate right (`>>>`) |

| `disp[5:0]` | Shift amount (**0..63**; **0** with kind **000** = no shift) |

Assembler syntax: **`add rd, rs, rt<<4`**, **`add rd, rs, #imm>>2`**, **`cmp rs, rt>>1`**. Standalone **`LSH`/`RSH`/`ROL`/`ROR`** opcodes remain for **`rd ← shift(rs)`** moves.

---

## Branch, Call, Control Flow (0x030..0x04F)

**Branch targets:** baseline uses **PC-relative `disp`** in the base word (sign-extended to `IC + disp`), or a **full 64-bit target** via **`imm1`** when **`imm1_flag = 1`** (assembler “far jump” profile). **`CALL`/`RET`** use the **stack** (`SP`); see ABI section.

| Opcode | Mnemonic | Operands             | Notes                                             |
| -----: | -------- | -------------------- | ------------------------------------------------- |
|  0x030 | JMP      | `addr` (disp or imm) | unconditional jump                                |
|  0x031 | JE       | `addr`               | jump if EQ flag set                               |
|  0x032 | JNE      | `addr`               | jump if EQ not set                                |
|  0x033 | JZ       | `addr`               | jump if Zero flag set                             |
|  0x034 | JNZ      | `addr`               | jump if Zero flag not set                         |
|  0x035 | JC       | `addr`               | jump if Carry set                                 |
|  0x036 | JNC      | `addr`               | jump if Carry not set                             |
|  0x037 | JLT      | `addr`               | jump if Less-Than                                 |
|  0x038 | JGT      | `addr`               | jump if Greater-Than                              |
|  0x039 | JO       | `addr`               | jump if Overflow                                  |
|  0x03A | JNO      | `addr`               | jump if No Overflow                               |
|  0x03B | CALL     | `addr`               | Push **`IC`** (return / next PC), then branch |
|  0x03C | RET      | —                    | Pop **`IC`** |
|  0x03D | SWAP     | **`rd`, `rs`** | **Swap two GPRs:** `tmp ← GPR[rd]; GPR[rd] ← GPR[rs]; GPR[rs] ← tmp`. *Not* an immediate‑immediate opcode; **`rt`/`imm` unused.** Memory or vector exchange uses **`LOAD`/`STORE`/`ATSWAP`** / SSX **`XSWAP`** (opcode **`0x110`**), not scalar **`SWAP`**. |
|  0x03E | COPY     | **`rd`, `rs`** | **Register move:** `GPR[rd] ← GPR[rs]` (mnemonic **`MOV`** in some assemblers). **`rt`/`imm` unused.** |
|  0x03F | PUSH     | **`rs` (`rd` field)** / **`#imm`** | **`imm1_flag=0`:** push **`GPR[rd]`** (convention: source reg encoded in **`rd`**). **`imm1_flag=1`:** push **64‑bit immediate** **`imm1`**. |
|  0x040 | POP      | **`rd`** | Pop **one 64‑bit word** into **`GPR[rd]`** (stack disciplined with **`CALL`/`RET`**). **`imm` form** (“pop into immediate”) is **not** baseline scalar; reserve for tooling or privileged profiles. |

---

## Memory & I/O (0x050..0x06F)

**Scalar `LOAD`/`STORE`:** destination or source **GPR** is always in **`rd`**.

| Mode | Encoding | Effective address |
| ---- | -------- | ----------------- |
| **Absolute** | **`imm1_flag=1`**, **`imm1`** | **`imm1`** |
| **Indexed** | **`imm1_flag=0`**, **`rs`** = base, optional **`rt`** = index, **`disp`** = signed offset | **`GPR[rs] + GPR[rt] + disp`** (**`rt=0`** → omit index) |

Assembler syntax: **`[Symbol]`**, **`[#imm]`**, **`[sp+16]`**, **`[sp+r5]`**, **`[sp+r5+16]`**. Memory is **byte-addressed**; baseline **`LOAD`/`STORE`** use **64-bit aligned** words.

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
|  0x050 | LOAD | **`rd`, `addr`** | **`GPR[rd] ← Mem[addr]`** (64-bit word, big-endian at memory interface). |
|  0x051 | STORE | **`rd`, `addr`** | **`Mem[addr] ← GPR[rd]`**. |
|  0x052 | XLOAD | **`Xd`, `addr`** | SSX: load **one logical vector register** from **`addr`** (length = **VL×ES**; see § SSX). |
|  0x053 | XSTORE | **`Xd`, `addr`** | SSX: store **`Xd`** to **`addr`**. |
|  0x054 | FLOAD | **`Fd`, `addr`** | **`FSX[Fd] ← Mem[addr]`** (64-bit FP64 container in baseline). |
|  0x055 | FSTORE | **`Fd`, `addr`** | **`Mem[addr] ← FSX[Fd]`**. |
|  0x056 | PREFETCH | **`addr`, `#hint`** | Hint only; **`hint`** in **`imm1`** or **`disp` low bits** (cache level / temporal locality). |
|  0x057 | IN | **`rd`, `#port`** | **`GPR[rd] ← ZeroExtend8( IORead(port) )`**; **`port`** in **`imm1`** (baseline). |
|  0x058 | OUT | **`rs`, `#port`** | **`IOWrite(port, low8(GPR[rs]))`**; port in **`imm1`**. |
|  0x059 | XSWAP | **SSX / I/O class** | **Not** scalar `SWAP`. Reserved for **vector port DMA** or **typed atomic exchange** in an SSX profile; prefer mnemonic **`VXSWAP`** in assemblers to avoid confusion with **`0x110` XSWAP**. |

### Memory-mapped I/O (baseline `bvm`)

Besides programmed I/O via **`IN`/`OUT`**, the reference VM exposes a **single MMIO aperture** starting at **`MmioWindow::WindowBasePhysicalAddress`**, which equals **RAM size** (`1 << 24` bytes in the stock build). Only **aligned 64-bit** quad loads/stores are accepted; byte/half/word accesses to this range trap.

| Byte offset from window base | PascalCase register | Access | Semantics (baseline `bvm`) |
| -----: | --- | --- | --- |
| `0` | **DeviceIdentity** | RO | Returns **`DeviceIdentityMagicValue`** so firmware can fingerprint the platform. |
| `8` | **BootControl** | RW | General-purpose bring-up latch (firmware may write a non-zero handshake). |
| `16` | **InterruptPost** | WO | Writing **`Value & 0xFF`** queues hardware-style delivery of that vector (same path as external interrupts) when **`EI`** is armed. |
| `24` | **SyscallHandlerIc** | RW | Host programs the **`IC`** used by **`SYSCALL`** before issuing a syscall. |
| `32` | **LastSyscallNumber** | RO | Mirrors the last **`SYSCALL`** immediate recorded in **`MmioLastSyscallNumberRegister`** (software must not STORE here—it traps). |

---

## Atomic & Concurrency (0x060..0x07F)

**Atomic semantics (normative)**  
Every **`AT*`** completes as a single **atomic RMW** or **atomic simple access** at the coherence point for **`Addr`**. **`Addr`** is a **64-bit byte address** aligned to the access **granularity** (32/64/128-bit per opcode profile). **`ATCAS`** additionally compares **`Mem[Addr]`** to an **expected** value before storing **new**.

**Operand template** — unless a row overrides:

* **`GPR[rd]`** — primary data value (store / operate value / output).
* **`GPR[rs]`** — operand B for bitwise / arithmetic ops, or **expected** half for **`ATCAS`** (profile-dependent split across **`imm1`**).
* **`Addr`** — in **`imm1`** with **`imm1_flag=1`**, or **base+disp** per implementation.

For brevity, **“`(rd, Addr)` RMw”`** means **`T ← Mem[Addr]; Mem[Addr] ← f(T, GPR[rd]); GPR[rd] ← T_old`** unless stated otherwise (**fetch-old** convention for **`ATADD`**, **`ATXOR`**, etc.).

| Opcode | Mnemonic | Operands | Atomic effect (informative) |
| -----: | -------- | -------- | --------------------------- |
|  0x060 | ATAND | **`rd`, `Addr`** | **`Mem[Addr] ← Mem[Addr] ∧ GPR[rd]`** |
|  0x061 | ATOR | **`rd`, `Addr`** | bitwise **OR** |
|  0x062 | ATNOT | **`Addr`** (**unary**) | **`Mem[Addr] ← ¬Mem[Addr]`** (operand in **`imm`/`disp`** profile); **`rd`** unused baseline. |
|  0x063 | ATXOR | **`rd`, `Addr`** | bitwise **XOR** |
|  0x064 | ATADD | **`rd`, `Addr`** | **`Mem ← Mem + GPR[rd]`** (numeric type per **`opmode`**) |
|  0x065 | ATSUB | **`rd`, `Addr`** | **`Mem ← Mem − GPR[rd]`** |
|  0x066 | ATMUL | **`rd`, `Addr`** | **`Mem ← Mem × GPR[rd]`** (**profile:** low 64 bits or saturating variant) |
|  0x067 | ATDIV | **`rd`, `Addr`** | **`Mem ← Mem ÷ GPR[rd]`** (trap on **div0** unless soft-float profile) |
|  0x068 | ATFADD | **`rd`, `Addr`** | synonym / alias of **`ATADD`** for FP atomics (**`opmode`** selects FP element) |
|  0x069 | ATFSUB | **`rd`, `Addr`** | FP subtract RMW |
|  0x06A | ATCAS | **`rd`, `rs`, `Addr`** **`/ imm expected`** | **CAS:** if **`Mem[Addr]=expected`** then **`Mem[Addr]←GPR[rd]`**, else **`GPR[rd]←Mem`**. **`expected`** in **`GPR[rs]`** or **`imm1`**. |
|  0x06B | ATCMP | **`Addr`** | **`FR ← flags(Mem[Addr] − GPR[rd])`** (atomic read-compare without write) |
|  0x06C | ATINC | **`Addr`** | **`Mem[Addr] ← Mem[Addr] + 1`** (**integer width** profile) |
|  0x06D | ATDEC | **`Addr`** | **`Mem[Addr] ← Mem[Addr] − 1`** |
|  0x06E | ATLOAD | **`rd`, `Addr`** | **`GPR[rd] ← Mem[Addr]`** with acquire / seq_cst semantics (**fence** qualifier in **`disp`** profile) |
|  0x06F | ATSTORE | **`rd`, `Addr`** | **`Mem[Addr] ← GPR[rd]`** with release semantics profile |
|  0x070 | ATSWAP | **`rd`, `Addr`** | **`t ← Mem[Addr]; Mem[Addr] ← GPR[rd]; GPR[rd] ← t`** (exchange) |
|  0x071 | ATREDUCE | **`rd`, `Addr`, `#op`** | **`Mem[Addr] ← reduce(Mem[Addr], GPR[rd], op)`** where **`op`** encodes sum/min/max/and/or in **`imm1`/`disp`** |

Implementations document exact **ordering** (**RLX/ACQ/REL/SEQ** flags) via **`CSR`/`disp`**; this table states **functional** semantics only.

---

## SSX — fixed 128-bit pack (0x100..0x12F)

**Scope:** operations on a **single 128-bit logical operand** (crypto / narrow SIMD). Distinct from **VL-scalable** SSX (**0x140+**).  
**Operands:** **`Xd`, `Xs`, `Xt`** denote **SSX registers** (implementation may use **prefix decode** or **banked `rd/rs/rt`**).  
**Immediates:** **`imm128`** means **`imm1`||`imm2`** (high then low 64-bit words). **RRR** uses **`imm1_flag=imm2_flag=0`**.

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
|  0x100 | XAND | **`Xd`, `Xs`, `Xt`** / **`Xd`, `Xs`, `imm128`** | 128-bit bitwise **AND** |
|  0x101 | XXOR | **`Xd`, `Xs`, `Xt`** / **`Xd`, `Xs`, `imm128`** | 128-bit bitwise **XOR** (**mnemonic `XOR128` retired**—use **`XXOR`**) |
|  0x102 | XNOT | **`Xd`, `Xs`** | **`Xd ← ¬Xs`** (**`Xt` unused**) |
|  0x103 | XADD | **`Xd`, `Xs`, `Xt`** | 128-bit **unsigned** carry / **two’s-complement** per profile |
|  0x104 | XSUB | **`Xd`, `Xs`, `Xt`** | subtract |
|  0x105 | XMUL | **`Xd`, `Xs`, `Xt`** | **low 128** product profile |
|  0x106 | XDIV | **`Xd`, `Xs`, `Xt`** | **unsigned** / **signed** per **`opmode`** |
|  0x107 | XMOD | **`Xd`, `Xs`, `Xt`** | remainder |
|  0x108 | XCMP | **`Xs`, `Xt`** | **`FR` / SSX-flags CSR** from **`Xs−Xt`**; **`rd=0`**. |
|  0x109 | XRSH | **`Xd`, `Xs`, `#u6`** | logical right (**`imm`** shift count) |
|  0x10A | XLSH | **`Xd`, `Xs`, `#u6`** | left shift |
|  0x10B | XROR | **`Xd`, `Xs`, `#u6`** | rotate right |
|  0x10C | XROL | **`Xd`, `Xs`, `#u6`** | rotate left |
|  0x10D | XLOAD | **`Xd`, `Addr`** | **`Xd ← Mem[Addr..Addr+15]`** (big-endian lane order profile) |
|  0x10E | XSTORE | **`Xd`, `Addr`** | store **16 bytes** |
|  0x10F | XCOPY | **`Xd`, `Xs`** | **`Xd ← Xs`** |
|  0x110 | XSWAP | **`Xd`, `Xs`** | exchange **128-bit** contents |
|  0x111 | XCRYPT | **`Xd`, `Xs`, `Xt`** | **AES-128** single-block encrypt: **`Xd ← AES128(Xs, key=Xt)`**; optional **`#rounds`** in **`imm1`** (baseline: **10** only). **✓** |
|  0x112 | XDECRYPT | **`Xd`, `Xs`, `Xt`** | **AES-128** decrypt (inverse of **`XCRYPT`**). **✓** |
|  0x113 | XGEN | **`Xd`, `#seed`** / **`Xd`, `Xs`** | **`#seed`:** deterministic **128-bit** PRNG into **`Xd`**; **`Xs`:** KDF-lite expand. **✓** |

---

## SSX — scalable vectors & HPC (0x140..0x17F)

**Normative vector model**

* **VL** — number of **active elements** (from **`VLEN` CSR** or **static profile**). **i ∈ {0,…,VL−1}**.
* **ES** — **element size** (**1/2/4/8** bytes) from **`opmode`** (see § Opmode) and optional **`disp` extension**.
* **Pₖ** — **predicate mask**; **Pₖ[i]=1** enables update of **lane i** of the destination. **`k`** encoded in **`disp[8:5]`** (implementation-defined width if **>8** predicates).
* **Masked op:** **`∀i : if Pk[i] then dst[i] ← f(src…)`**; inactive lanes keep **dst[i]** **unchanged** unless opcode defines **zeroing** (**`disp[9]=1`** profile).

**Operand shorthand:** **`Xd,Xs,Xt`** = **dest, src1, src2** for ternary; unary uses **`Xt=0`**.

### A) Lane-wise floating arithmetic (0x140..0x147)

| Opcode | Mnemonic | Operands | Per-lane semantics (FP, IEEE except where noted) |
| -----: | -------- | -------- | ------------------------------------------------ |
|  0x140 | VADD | **`Xd`, `Xs`, `Xt`** | **`Xd[i] ← Xs[i] + Xt[i]`** |
|  0x141 | VSUB | **`Xd`, `Xs`, `Xt`** | **`Xd[i] ← Xs[i] − Xt[i]`** |
|  0x142 | VMUL | **`Xd`, `Xs`, `Xt`** | **`Xd[i] ← Xs[i] × Xt[i]`** |
|  0x143 | VDIV | **`Xd`, `Xs`, `Xt`** | **`Xd[i] ← Xs[i] ÷ Xt[i]`** |
|  0x144 | VFMA | **`Xd`, `Xs`, `Xt`** | **Fused** **`Xd[i] ← Xs[i]×Xt[i] + Xd[i]`** (`ab+c`) |
|  0x145 | VFMSUB | **`Xd`, `Xs`, `Xt`** | **Fused** **`Xd[i] ← Xs[i]×Xt[i] − Xd[i]`** (`ab−c`) |
|  0x146 | VFNMADD | **`Xd`, `Xs`, `Xt`** | **Fused** **`Xd[i] ← −(Xs[i]×Xt[i]) + Xd[i]`** (`−ab+c`) |
|  0x147 | VFNMSUB | **`Xd`, `Xs`, `Xt`** | **Fused** **`Xd[i] ← −(Xs[i]×Xt[i]) − Xd[i]`** (`−ab−c`) |

### B) Memory, gather/scatter, broadcast, compress (0x148..0x14F)

| Opcode | Mnemonic | Operands | Semantics |
| -----: | -------- | -------- | --------- |
|  0x148 | VLOAD | **`Xd`, `Addr`** / **strided** profile | Contiguous **VL×ES** load; **`imm1`** may hold **stride** bytes. |
|  0x149 | VSTORE | **`Xd`, `Addr`** | Contiguous store |
|  0x14A | VGATHER | **`Xd`, `base`, `Ii`** | **`Xd[i] ← Mem[base + scale×Index[Ii[i]]]`** (**`scale`/`ES`** in **`imm`**) |
|  0x14B | VSCATTER | **`Xd`, `base`, `Ii`** | store indexed |
|  0x14C | VBROADCAST | **`Xd`, `Rs`/`#imm`** | **Splat** scalar GPR or **immediate** encoded in **`imm1`** to **all lanes** |
|  0x14D | VPREFETCH | **`Addr`, `#hint`** | Multi-line prefetch |
|  0x14E | VCOMPRESS | **`Xd`, `Xs`, `Pk`** | **Compress** lanes where **Pk[i]=1** into **contiguous low lanes** of **`Xd`** |
|  0x14F | VEXPAND | **`Xd`, `Xs`, `Pk`** | **Expand** masked scatter into **`Xd`** |

**`XLOAD`/`XSTORE` vs `VLOAD`/`VSTORE`:** Opcodes **`0x052`/`0x053`** and **`0x148`/`0x149`** name the **same architected operation** (**contiguous load/store of `VL×ES`** bytes into **`Xd`**). **`0x148+`** keeps memory ops **adjacent** to SSX arithmetic in documentation and some decoders; **`0x052+`** groups them with the scalar LSU. The reference **BVM** enum currently lists **`XLOAD`/`XSTORE`** only (**`Isa.h`**).

### C) Math & compare / selection (0x150..0x15B)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x150 | VMIN | **`Xd`, `Xs`, `Xt`** | Lane-wise minimum (FP **IEEE minNum** vs **IEEE min** selectable via **`disp`**) |
|  0x151 | VMAX | **`Xd`, `Xs`, `Xt`** | Lane-wise maximum |
|  0x152 | VABS | **`Xd`, `Xs`** | **`Xd[i] ← \|Xs[i]\|`** (lane-wise magnitude) |
|  0x153 | VNEG | **`Xd`, `Xs`** | **`−Xs[i]`** |
|  0x154 | VSQRT | **`Xd`, `Xs`** | **IEEE√** |
|  0x155 | VRCP | **`Xd`, `Xs`** | **Fast reciprocal** (may be **11-bit** estimate + **REFINE** op) |
|  0x156 | VRSQRT | **`Xd`, `Xs`** | **Fast rsqrt** |
|  0x157 | VROUND | **`Xd`, `Xs`, `#mode`** | **`mode`:** **NEAR/UP/DOWN/ZERO** + **MXCSR**-like |
|  0x158 | VCMP | **`Pk`, `Xs`, `Xt`, `#rel`** | **Predicate destination:** **`Pk[i] ← rel(Xs[i], Xt[i])`** (boolean lane); **`rel` ∈ {EQ, NE, LT, LE, ordered, …}** in **`imm`/`disp`**. **Scalar `rd`/`rs`/`rt`** may map **`Xs`,`Xt`** only—the **mask register id** **`k`** remains in **`disp`**. |
|  0x159 | VSEL | **`Xd`, `Xs`, `Xt`, `Pk`** | **`Xd[i] ← Pk[i] ? Xs[i] : Xt[i]`** (blend) |
|  0x15A | VSPLAT | **`Xd`, `#imm`** | **Immediate splat** (narrow **imm** replicated) |
|  0x15B | VDOTP | **`Xd`, `Xs`, `Xt`** | **Dot / partials:** **`Xd ← reduce( map(×, Xs, Xt), + )`** over **active lanes** (exact **fold tree** implementation-defined); **`imm1`/`disp`** may select **intra-vector** vs **accumulate-to-`A#`**. |

### D) Integer reductions & bitwise lanes (0x15C..0x15F)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x15C | VREDUCE | **`Xd`, `Xs`, `#op`** | **`Xd[0] ← reduce(Xs, op)`** over **active** lanes; **`op`** in **`imm`/`disp`** (**sum/min/max/and/or/…**). Upper lanes **zero** or **unspecified** unless **`Z`** in **`disp`**. |
|  0x15D | VAND | **`Xd`, `Xs`, `Xt`** | **Integer** lane AND |
|  0x15E | VOR | **`Xd`, `Xs`, `Xt`** | OR |
|  0x15F | VXOR | **`Xd`, `Xs`, `Xt`** | XOR |

### E) Shifts, permute, pack / unpack (0x160..0x167)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x160 | VSHL | **`Xd`, `Xs`, `Xt`/`#u8`** | Left shift; amount from **per-lane Xt** or **uniform imm** |
|  0x161 | VSHR | **`Xd`, `Xs`, `Xt`/`#u8`** | **Logical** right |
|  0x162 | VSRA | **`Xd`, `Xs`, `Xt`/`#u8`** | **Arithmetic** right |
|  0x163 | VPERM | **`Xd`, `Xs`, `#imm`** | **Lane permute** / **control table** in **`imm`** |
|  0x164 | VSHUF | **`Xd`, `Xs`, `Xt`** | **Two-source shuffle** (pattern in **Xt** profile) |
|  0x165 | VZIP | **`Xd`, `Xs`** | **Interleave** low halves |
|  0x166 | VUNZIP | **`Xd`, `Xs`** | De-interleave |
|  0x167 | VEXT | **`Xd`, `Xs`, `#pair`** | **Widen** / **narrow** / **repack** lanes (**`pair`** in **`imm`**) |

### F) Reserved (0x168..0x17F)

**0x168..0x17F** — reserved (**VFORK**, **VMASK** stack, **faulting gather**, **segmented loads**, etc.).

---

## Tensor & Mat Units (0x180..0x19F)

| Opcode | Mnemonic     | Operands                      | Notes                                             |
| -----: | ------------ | ----------------------------- | ------------------------------------------------- |
|  0x180 | MATMUL       | `imm128(desc)`, `reg128(dst)` | tile descriptor in imm128; offload to matrix unit |
|  0x181 | GEMM_OFFLOAD | `imm128(desc)`                | offload GEMM with descriptor                      |
|  0x182 | TENSOR_LOAD  | `imm128(desc)`, `reg128`      | multi-dim load descriptor                         |
|  0x183 | TENSOR_STORE | `reg128`, `imm128(desc)`      | store descriptor                                  |
|  0x184 | CONV2D       | `imm128(desc)`, `reg128(dst)` | conv offload                                      |
|  0x185 | DOTP_ACC     | `reg128`, `reg128`, `A#`      | dot-product into accumulator A#                   |

**Baseline `bvm` descriptor summary** (memory pointed to by **`imm1`**):

| Op | Bytes | Layout |
|----|-------|--------|
| `matmul` / `gemm` | 32 (+8 α/β for `gemm`) | `AddrA`, `AddrB`, `AddrC`, meta `Elem\|M\|K\|N` |
| `tensor_load` / `tensor_store` | 32 | `AddrMem`, —, —, meta `Elem\|H\|W\|Stride` |
| `conv2d` | 48 | `AddrIn`, `AddrK`, `AddrOut`, meta in/out + `KH\|KW` |
| `qdot` | 40 | `AddrA`, `AddrB`, `AddrScore`, meta `Length`, scale @ +36 |

Mnemonics: `tensor_load`, `tensor_store`, `conv2d`, `vdotp`, `dotp_acc`, `qdot`, `qgemm`. See **`examples/MlInferenceDemo.s`**.

---

## FSX Floating (0x200..0x22F)

**Semantics:** **`F*`** operands use **`opmode`** to select FP64 vs packed FP32 lanes in the **`64-bit`** container (**`.H`/`.L`**). Unless noted, **`RRI`** uses **`immf`** in **`imm1`**.

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x200 | FADD | `Fd`, `Fs`, `Ft` / `Fd`, `Fs`, **`#immf`** | **IEEE-754** add. |
|  0x201 | FSUB | same | subtract |
|  0x202 | FMUL | same | multiply |
|  0x203 | FDIV | same | divide (**trap**/Inf rules per **`FR`** FP mask profile) |
|  0x204 | FMOD | same | remainder |
|  0x205 | FMA | `Fd`, `Fs`, `Ft` | **Fused multiply-add:** **`Fd ← Fs × Ft + Fd`** (IEEE-754 **fused** when **`opmode`** selects strict mode). Assembler may expose **`FMADD` / `FMSUB`** aliases with explicit operand order. |
|  0x206 | FCMP | **`Fs`, `Ft` / `Fs`, `#immf`** | **`FR`** / dedicated **FP FLAGS** (**`rd=0`**). |
|  0x207 | FRSH | bitwise treat float bits as **`uint`** | non-IEEE math (graphics profile) |
|  0x208 | FLSH | bitwise left |  |
|  0x209 | FROL | rotate bits in container | |
|  0x20A | FROR | rotate bits in container | |
|  0x20B | FLOAD | **`Fd`, `Addr`** | |
|  0x20C | FSTORE | **`Fd`, `Addr`** | |
|  0x20D | FCOPY | **`Fd`, `Fs`** (**`MOV.F`**) | **`Fs`/`immf`** profile; **`rt`** unused |

---

## Quantized / ML / Special (0x300..0x33F)

| Opcode | Mnemonic | Operands | Notes |
| -----: | -------- | -------- | ----- |
|  0x300 | QDOT | **`Xd`, `Xs`, `Xt`** **`/#desc`** | quantized dot (**`imm1`** packs scale/zero-point/ZP datatype) |
|  0x301 | QGEMM | **`#desc`** (`imm128`) | asynchronous tile offload mailbox |
|  0x302 | SATADD | **`rd`, `rs`, `#imm`** etc. | **Saturating** **`int`** add (operand pattern same as **`ADD`**) |
|  0x303 | SATMUL | **`rd`, `rs`, `#imm`** | saturating **`int`** multiply |
|  0x304 | RECIP | **`Fd`, `Fs`** | **`Fd ← ~1/Fs`** (fast path) |
|  0x305 | RSQRT | **`Fd`, `Fs`** | **`Fd ← ~1/√Fs`** |
|  0x306 | RCP_ITER | **`Fd`, `Fs`, `#n`** | **Newton** step count **`n`** in **`imm`** |
|  0x307 | TRANSPOSE | **`Xd`, `#tile`** | **`imm`** selects **NxN tile** packing |
|  0x308 | PACK | **`Xd`, `Xs`, `#lanefmt`** | pack lanes narrower→**`Xd`** container |
|  0x309 | UNPACK | **`Xd`, `Xs`, `#lanefmt`** | inverse |
|  0x30A | XCRYPT_EXT | **`imm`, imm, imm`** profile | authenticated encryption extensions |
|  0x30B | XDECRYPT_EXT | profile | decryption |

---

## NSX — Neural Support eXtensions (0x600..0x6FF)

**NSX** adds neural primitives on the existing **K-Bank / SKB / SSX** infrastructure — no separate NPU. Tensors live in scratchpad or shared memory; vector lanes follow **SSX** width; predication uses **`P0..P7`** where a profile applies lane masks.

**Design:** *explicit beats implicit* — complex ops take a **memory descriptor** in **`imm1`** (`imm1_flag=1`), same pattern as **`matmul`/`conv2d`** (legacy tensor unit).

### NSX base word encoding

Same 64-bit header as scalar ops. Inside NSX, **`opmode`** selects element type:

| `opmode` | Type |
|----------|------|
| `000` | int |
| `001` | FP64 |
| `010` | FP32 (baseline default) |
| `011` | BF16 |
| `100` | INT8 |

**`WINSZ[47:32]`** holds a 16-bit **dropout PRNG** state (baseline `bvm` LCG); seeded on first **`dropout`**.

**Mnemonic collision:** legacy tensor **`conv2d`** = **`0x184`**; NSX **`CONV2D`** = **`0x621`** — assemblers expose **`nconv2d`** and **`xconv2d`** aliases.

### Neural primitives (0x600..0x61F) — baseline

| Opcode | Mnemonic | Operands | BVM |
| -----: | -------- | -------- | --- |
| 0x600 | SOFTMAX | `Xd`, `Xs` | ✓ 4 FP32 lanes |
| 0x601 | LAYERNORM | `Xd`, `Xs`, `gamma`, `beta` | ✓ 4 FP32 lanes |
| 0x602 | BATCHNORM | `Xd`, `Xs`, `gamma`, `beta` | ✓ (same as layernorm on lanes) |
| 0x603 | DROPOUT | `Xd`, `Xs`, `#rate` | ✓ |
| 0x604 | RELU | `Xd`, `Xs` | ✓ |
| 0x605 | GELU | `Xd`, `Xs` | ✓ |
| 0x606 | SWISH | `Xd`, `Xs` / `,#beta` | ✓ |
| 0x607 | SILU | `Xd`, `Xs` | ✓ |
| 0x608 | TANH | `Xd`, `Xs` | ✓ |
| 0x609 | SIGMOID | `Xd`, `Xs` | ✓ |
| 0x60A | MSE_LOSS | descriptor (`pred`, `target`, `loss_out`) | ✓ |
| 0x60B | CROSS_ENTROPY | descriptor + class index in meta | ✓ |
| 0x60C | L2_LOSS | descriptor (`weights`, `loss_out`, `lambda`) | ✓ |

### Convolution (0x620..0x63F)

| Opcode | Mnemonic | Notes | BVM |
| -----: | -------- | ----- | --- |
| 0x621 | CONV2D | **`nconv2d`**, **`xconv2d`** | ✓ tiles ≤4×4 |
| 0x620 | CONV1D | descriptor (`W=1` or `H=1`) | ✓ |
| 0x623 | DWCONV2D | descriptor | ✓ per-channel kernel |
| 0x622,0x624 | CONV3D, SEPCONV | descriptor | stub |
| 0x625..0x627 | POOL_MAX, POOL_AVG, POOL_GLOBAL | descriptor / lanes | ✓ |
| 0x628 | UPSAMPLE | descriptor (2× nearest) | ✓ |

**Conv descriptor (56 B, `imm1`):** `input`, `kernel`, `output`, `meta` (`H|W|C|KH:KW`), `bias`, `flags` (1=RELU), `reserved`.

### Attention (0x640..0x65F)

| Opcode | Mnemonic | BVM |
| -----: | -------- | --- |
| 0x640 | ATTENTION | ✓ `N,D` ≤4 |
| 0x642 | SELF_ATTN | ✓ alias |
| 0x641,0x643,0x644 | CROSS_ATTN, FLASH_ATTN, PAGED_ATTN | stub |

**Attention descriptor (64 B):** `Q`, `K`, `V`, `output`, `meta` (`B|H|N|D`), `mask`, `flags`, `reserved`.

### Transformer / RNN (0x660..0x67F)

| Opcode | Mnemonic | BVM |
| -----: | -------- | --- |
| 0x660 | TRANSFORMER | 96 B desc; self-attn + `Wout`; `fwd_end` backprop | ✓ |
| 0x671 | LSTM_CELL | ✓ `hidden,input` ≤4 |
| 0x670 | RNN_CELL | ✓ |
| 0x672 | GRU_CELL | ✓ (LSTM descriptor layout) |
| 0x673..0x676 | RNN/GRU/SEQ/BIDIR | stub |

**LSTM cell descriptor (64 B):** `ct`, `ht`, `ct_1`, `ht_1`, `xt`, `W`, `b`, `meta`.

### Autodiff / optimizers / scientific (0x680..0x6DF)

| 0x680 | FWD_START | tape descriptor (`imm1`) | ✓ |
| 0x681 | FWD_END | backprop through tape (MSE + transformer) | ✓ |
| 0x6A0 | SGD | weights/grads/`lr` descriptor | ✓ |

Other **`bwd_*`**, **`adam`/`lamb`**, **`laplacian_*`/`hessian_*`/`newton_step`** trap in baseline **`bvm`**.

### Reserved

**`0x6E0..0x6FF`** — future NSX extensions.

See **`examples/NsxDemo.s`**, **`sources/bvm/NsxProfile.h`**.

---

## Compact encoding (16-bit)

Optional **16-bit** instruction stream selected by **`FR.CompactMode` (bit 11)**. Mode changes happen on **`CALL`** / **`RET`**, not explicit mode instructions:

- **`CALL` target**: push return **`IC`** and saved mode; **`IC ← target & ~1`**; **`FR.CompactMode ← target[0]`** (LSB **0** = compact callee, **1** = full callee).
- **`RET`**: restore **`FR.CompactMode`** then **`IC`** from the stack.

Assembler: **`.section name, "x", compact|full`** (aliases **`.mode compact`** / **`.mode full`**). Symbols in **full** sections get **LSB=1** on **`CALL`** targets. **Full 64-bit** encoding is unchanged.

| Field | Bits | Role |
|-------|------|------|
| `opcode_c` | `[15:10]` | 6-bit compact opcode (`0x00` NOP … `0x10` HLT) |
| `rd` | `[9:5]` | Destination GPR **R0..R31** only |
| `imm5` / `rs` | `[4:0]` | Second GPR or **5-bit signed** immediate (`-16..15`) |

| `c` | Mnemonic | Full analogue |
|-----|----------|---------------|
| 0x01 | `add` | `ADD` (`rd ← rd + rs`) |
| 0x03 | `mov` | `COPY` |
| 0x04 | `ldi` | `ADD rd, #imm5` |
| 0x05/0x06 | `lw`/`sw` | `LOAD`/`STORE` via **`SP + imm5×8`** |
| 0x07/0x08 | `bz`/`bnz` | Test **`rd==0`** / **`rd!=0`**, **±32 B** branch |
| 0x09 | `call` | **`CALL`**, **10-bit** PC-relative (±1 KiB slots) |
| 0x0E | `jmp` | **`JMP`**, **±32 B** |
| 0x0F | `movs` | `COPY rd, sp` |

Assembler: **`.mode compact`** / **`.mode full`** (pads text to 8 bytes before full regions). Example: **`examples/CompactHybridDemo.s`**.

Restrictions: **GPR-only**, no FSX/SSX/K-Bank in compact form; use **`call`** into a **full** section for wide ops.

---

## Security & isolation (`0x500..0x521`)

Full architecture: **[Honeycomb_Security.md](Honeycomb_Security.md)**. Baseline **`bvm`** implements PAC/CFI/SSP stubs, **syscall sandbox** (`sandbox_allow` / `sandbox_deny` / `sandbox_reset`), **protected user memory** (`sandbox_base` / `sandbox_limit` on EL0 `LOAD`/`STORE`), encrypted load/store via SSX, simplified **`measure`**, and EL2 **VMCS stub** (`vmlaunch` / `vmexit`). User violations deliver interrupt vector **`0x3E`**. Verified by **`examples/SecuritySandboxDemo.s`**.

| Opcode | Mnemonic | BVM |
| -----: | -------- | --- |
| 0x500..0x505 | `PACIA`/`PACDA`/`PACIB`/`AUTIA`/`AUTDA`/`XPACI` | ✓ (XGEN-seeded keys) |
| 0x506..0x508 | `CFI_LABEL`/`CFI_CALL`/`CFI_JUMP` | ✓ |
| 0x509..0x50B | `SSP_PUSH`/`SSP_POP`/`SSP_CHECK` | ✓ |
| 0x50C..0x50E | `SANDBOX_ALLOW`/`SANDBOX_DENY`/`SANDBOX_RESET` | ✓ |
| 0x50F..0x513 | `CAP_*` | stub (trap) |
| 0x514..0x517 | `ENC_LOAD`/`ENC_STORE`/`ENC_GENKEY`/`ENC_SWITCH` | ✓ (SSX AES) |
| 0x518..0x51B | `MEASURE`/`QUOTE`/`SEAL`/`UNSEAL` | `MEASURE` ✓; others stub |
| 0x51C..0x520 | `VMLAUNCH`/`VMRESUME`/`VMEXIT`/`VMREAD`/`VMWRITE` | full VMCS guest/host ✓ |
| 0x521 | `PAC_KEY_INIT` | ✓ |

Security CSRs **`0x60..0x6F`** and IOMMU **`0x80..0x82`** are readable/writable from supervisor via `copy`/`load`/`store` register aliases (`sandbox_base`, `pac_key_h`, …).

---

## Reserved & Future ranges

Unassigned gaps include **`0x470..0x4FF`** and **`0x522..0x5FF`**, **`0x700..0x7FF`**. **`0x500..0x521`** is **security**; **`0x600..0x6FF`** is **NSX**. Other groups retain holes for expansion (2048-entry opcode space).

---

# 2) Honeycomb Reference ISA

## # Overview

Honeycomb v0.7 (HPC edition) is a 64-bit big-endian RISC-ish ISA with a fixed 64-bit base instruction word and optional follow-on immediate words. It targets HPC, scientific computing, aerospace and ML workloads by adding a scalable vector (SSX) register file, a rich floating-point (FSX) register file, predicate registers, accumulators, **K-Bank scratchpad**, index registers, and dedicated tensor/matrix offload opcodes. Mixed precision is implemented via microcode that operates on upper/lower halves of vector registers (packing/unpacking lanes).

---

## # Register file (summary & encodings)

Scalar codes in **`rd`/`rs`/`rt`** (6-bit fields):

| Code | Register | Notes |
|------|----------|-------|
| `0x00..0x1F` | `R0..R31` | GPR |
| `0x20` | `SP` | Stack pointer |
| `0x21` | `IC` | Instruction counter |
| `0x22` | `FR` | Flags |
| `0x23` | `PTB` | Page table base |
| `0x24` | `IVB` | Interrupt vector table base |
| `0x25` | `IVL` | IVT length (entries); **`IVL == 0`** → interrupt delivery traps |
| `0x26` | `WIND` | K-Bank window base / offset |
| `0x27` | `WINSZ` | **Merged:** `[15:0]` window size (default **32**); `[31:24]` complex precision; `[23:16]` GPR stride; `[39:32]` K stride. Assembler alias **`ctype`** → **`winsz`**. **`RDWIN #5`** reads/writes full merged value (window field preserved on partial **`WRWIN #5`** if only low 16 bits change) |
| `0x28..0x2B` | `I0..I3` | K-Bank / gather index |
| `0x2C` | `TlbFaultAddr` | Faulting virtual address on **TLB miss** (vector **15**) |
| `0x2D` | `TlbFaultKind` | Access kind: **0** read, **1** write, **2** execute |
| `0x58` | `CWP` | Current window pointer — **✓** `SAVWIN`/`RESWIN` |
| `0x59` | `CANSAVE` | Windows that may still be saved |
| `0x5A` | `CANRESTORE` | Windows that may be restored |
| `0x5B` | `OTHERWIN` | Spilled-window count ( **`FLUSHWIN`** ) |
| **`0x5C`** | **`ZERO`** | Architected **zero register** — reads **0**, writes ignored |
| `0x5D` | `ROTBAS` | Rotating K-Bank base |
| `0x5E` | `ROTSZ` | Rotating K-Bank span |
| `0x5F` | `KMASK` | K index mask (default **`0x3FFF`**) |

**Profile operand aliases (not separate baseline scalar codes):** In full silicon, the same 6-bit field may name **`F0..F15`**, **`X0..X15`**, **`A0..A3`**, or **`P0..P7`** via **opcode-class decode** (typical profile assignments **`0x2C..0x57`**). Baseline **`bas`/`bvm`** use dedicated opcode groups (**`0x140+`**, **`0x200+`**, …) with **`f*`/`x*`** mnemonics instead of overloading **`rd`** at those codes.

**K-Bank:** architected **`K0..K16383`** (128 KB per core). Baseline **`bvm`** holds a flat **`uint64_t[16384]`** array; **`KLOAD`/`KSTORE`** use **`WIND`**, **`I0..I3`**, and **`KMASK`**. Physical domain **`0000`** maps the same storage for **`LOAD`/`STORE`** (except legacy flat DRAM compat — see implementation notes).

Register fields in the base instruction word are **6 bits** (`rd`, `rs`, `rt`) → numeric range **`0..63` (`0x00..0x3F`)** for encodings that pack only into the base word. Named specials **`SP`…`KMASK`**, **`WINSZ`/`ctype`**, and **`I0..I3`** are accepted by the baseline assembler when written explicitly (`load ptb`, `load winsz`, …).

> **Do not use `0x60` as a register code** — **`0x060` is opcode `ATAND`**. **`0x61` is not a valid 6-bit register index** (max **`0x3F`**); it appears only as an SKB **`HINT`** type byte.

---

## # Base instruction word (64 bits, big-endian)

Bit layout `[63:0]`:

```
[63:53] opcode (11)
[52] imm1_flag (1)   ; fetch 1 × 64-bit immediate after base
[51] imm2_flag (1)   ; fetch 2nd × 64-bit immediate after base (for imm128)
[50:45] rd (6)
[44:39] rs (6)
[38:33] rt (6)
[32:30] opmode (3)   ; data type / vector / mixed
[29:0] disp/misc (30) ; short signed imm or misc flags
```

**Immediate rules**

* If `imm1_flag` set → next 64-bit word is imm1.
* If `imm2_flag` set → next 64-bit word after imm1 is imm2.
* `imm128` uses both imm1 (high 64) then imm2 (low 64) — consistent with big-endian layout.
* `immf` (32-bit float) is encoded in a follow 64-bit word; upper 32 bits reserved/zero.

**Branches**

* Prefer using `disp` (signed 30-bit PC-relative). For farther targets set `imm1_flag` and provide a full 64-bit target.

---

## # Opmode semantics (3 bits)

```
000 -> integer 64-bit scalar
001 -> FP64 scalar
010 -> FP32 scalar lanes (packed 32-bit in 64-bit reg)
011 -> SSX vector generic (lanes per VL)
100 -> SSX vector FP32 lane mode
101 -> mixed-precision / microcode path
110 -> atomic / privileged variant
111 -> reserved
```

When `opmode == 101` (mixed) read `disp[1:0]`:

* `00` → `.H` only
* `01` → `.L` only
* `10` → both halves (packed lanes)
* `11` → both halves with widen-accumulate semantics

---

## # SSX (scalable vector) model

* **VL (vector length)** — number of **elements** operated on by **0x140..0x167** SSX opcodes; established at reset and/or via **`VLEN` CSR** (see § SSX — scalable vectors & HPC). Minimum **architected** logical register width is **128 bits**; **VL×ES** may exceed that when **VL>1**.
* **Lane types** — **INT8/16/32/64**, **FP16/BF16/FP32/FP64** selected by **`opmode`** and optional **`disp`** extensions (see § Opmode).
* **Predication** — **`P0..P7`** (or more) are per-lane masks. For **masked ALU**, **predicate register index `k`** is encoded in **`disp`** (see **SSX — scalable vectors** preamble: typically **`disp[8:5]`** for **`Pk`**, with low bits for **subop / zeroing**). **Assembler syntax** like **`, P0`** sets these fields.
* **Halves & mixed-precision** — **`Xn.H`** / **`Xn.L`** and **`opmode==101`** (`disp[1:0]`) select packed-half and **widen-accumulate** behavior (§ Mixed-precision microcode rules).
---

## # Mixed-precision microcode rules (high level)

* Entry: `opmode == 101` → mixed microcode engine invoked.
* Half selector: `disp[1:0]` chooses `.H`/`.L`/both/widen.
* Widening: narrower inputs are widened for accumulation by default (FP16/BF16→FP32; FP32→FP64) unless misc flags request otherwise.
* Atomicity: destination register halves updated atomically on instruction completion.
* Determinism: microcode obeys FP control registers (rounding, denorms). A reproducibility control bit forces deterministic rounding and behavior across implementations.
* Fast path / accurate path selectable via control register (compiler hints).

---

## # Memory model & atomics

* Cache line: 64 bytes.
* Default memory model: weak (release consistency). Provide `MFENCE` (full fence) instruction (not listed above — assignable in reserved opcodes) and use atomic ops for synchronization.
* AT* family: strong atomic RMW semantics. `ATREDUCE` atomically applies reduction op to memory location (sum/min/max, etc).
### Paging & TLB (baseline `bvm`)

* **`PTB`** — physical address of the **root page table** (4 KiB aligned). A write to **`PTB`** flushes the TLB.
* **`PTB`** — physical address of the **root page table** (4 KiB aligned). A write to **`PTB`** flushes the TLB.
* **`FR.Paging` (bit 8)** — when set (via **`EP`**), **`LOAD`/`STORE`**, instruction **fetch**, and **atomics** use **virtual addresses** translated through **`PTB`** and a **64-entry fully associative TLB**.
* **`FR.SoftwareTlbRefill` (bit 10)** — when set (via **`ESR`**), a **TLB miss** delivers vector **`15`** instead of a hardware page walk. Firmware reads **`TlbFaultAddr`** / **`TlbFaultKind`**, issues **`TlbInsert`**, then **`IRET`**. **`DSR`** clears the bit. With the bit clear, misses are refilled in hardware (default).
* **`DP`** clears **`FR.Paging`** and flushes the TLB. With paging off, addresses are **physical** (legacy behavior).
* **Page tables** — four levels, **512 × 8-byte PTEs** per table (4 KiB). VPN index slices: `[47:39]`, `[38:30]`, `[29:21]`, `[20:12]`; offset `[11:0]`.
* **PTE flags** — **`Present`**, **`Read`**, **`Write`**, **`Execute`**, optional **`User`**, **`Global`**, **`Huge`**. PFN in bits **`[63:12]`**. **`Huge`** at level 1 → **1 GiB**; at level 2 → **2 MiB**; leaf level → **4 KiB**.
* **Wired TLB entries** — **`WireTlbEntry #slot`** pins a valid slot so **`FlushVirtual`** does not invalidate it; only a full flush (**`EP`**, **`PTB`** write, **`DP`**) clears wired slots.
* **Page fault** — vector **`14`** when translation fails or permissions deny the access.
* **TLB miss (software refill)** — vector **`15`** when **`ESR`** is active and the VPN is not cached. While already in an exception frame (**`ExceptionFrameDepth > 0`**), misses use the **hardware walk** so handlers can run without nested TLB traps.
* See `examples/PagingDemo.s` (hardware refill) and `examples/PagingSoftwareRefillDemo.s` (vector **15** + wired slot).

### MMIO coherency (baseline `bvm`)

`LOAD`/`STORE`/`AT*` that target the MMIO aperture route to device side-effect logic instead of DRAM. There is **no** automatic DMA snoop; ordering follows the weak memory model unless firmware issues explicit atomics or future fence opcodes. MMIO writes may raise asynchronous interrupts through **InterruptPost** (see **Memory-mapped I/O** under § Memory & I/O).

### User mode & privilege (baseline `bvm`)

* **`FR.UserMode` (bit 9)** — when set, the core is in **user mode**. **`EU`** sets the bit (supervisor-only); **`DU`** clears it (supervisor-only). **`SYSCALL`** clears **`UserMode`** on entry so the handler always runs in supervisor mode; **`SYSRET`** restores the saved **`FR`**, including **`UserMode`** for returns to user code.
* **Traps in user mode** — **`EI`/`DI`**, **`EP`/`DP`**, **`EU`**, and **MMIO quad `LOAD`/`STORE`** (aperture at RAM size). Ordinary DRAM **`LOAD`/`STORE`** and **`SYSCALL`** remain available to user code.
* See `examples/MmioBootSequenceDemo.s` for supervisor bring-up, **`EU`**, and a user **`SYSCALL`**.

### Syscall kernel entry (baseline `bvm`)

`SYSCALL` pushes **`IC`** then **`FR`**, clears **`FR.Interrupt`** and **`FR.UserMode`**, and jumps to **`MmioSyscallProgramCounter`**. `SYSRET` reverses that frame. This path is **orthogonal** to `IRET`; mixing frames is a hard trap in `bvm`.

---

## Interrupts & interrupt vector table (IVT)

Honeycomb does **not** hard-code handler addresses in the decoder. Interrupts are dispatched through an **interrupt vector table (IVT)** in **normal memory**: a contiguous array of **64-bit handler entry addresses** (full `IC` values), one per vector index.

**Table layout**

* **`IVB`** holds `IVT_Base` (64-bit byte address; should be 8-byte aligned).
* **`IVL`** holds the number of **entries** in the table. Valid vector indices are **`n` with `0 ≤ n < IVL`**. The table occupies **`IVL × 8`** contiguous bytes starting at **`IVB`** (inferred size; no separate “byte length” register).
* Entry `n` is at **`IVB + 8·n`**: `handler_ic = mem64[IVB + 8·n]`.
* An entry of **0** means **unhandled** (implementation may trap or fault).

**Enable / mask**

* **`EI` / `DI`** manipulate the **interrupt-enable** bit in **`FR`** (reference VM: `FR` bit 7, `Interrupt` in `StatusFlags`). **`EI`/`DI`** are **supervisor-only** in baseline `bvm`.
* **External / asynchronous** interrupts are only **taken** when this bit is **set** (between instructions), after the current instruction completes and the processor is not halted.
* **`INT n`** (software interrupt) is **synchronous**: it uses the same IVT entry `n` and is **not** suppressed when the interrupt-enable bit is clear (same general idea as `INT` on common ISAs).

**Hardware exception stack (reference VM)**

Nested **`INT`**, external IRQ, page fault, and TLB-miss delivery use a **3-deep hardware exception stack** (not the GPR **`SP`** stack). Each frame stores **`{IC, FR}`**. **`IRET`** pops one frame. A fourth nested exception traps with **stack overflow**. This is separate from **`SYSCALL`/`SYSRET`**, which continue to use the software stack.

**Invocation**

On delivery, the processor saves **`IC`** and **`FR`** on the hardware stack, clears **`FR.Interrupt`**, and branches to the handler from the IVT. **`IRET`** restores **`FR`** then **`IC`** from the top frame.

See `examples/ExceptionNestDemo.s` for three nested vectors.

**Registers (BVM / scalar control)**

* **`IVB` / `IVL`** locate the IVT. **`IVB`** must contain **`IVT_Base`**, the physical byte address of the first **`uint64_t`** table slot (typically a label on the `.quad` sequence), **not** the **`mem64[IVT_Base]`** dereference contents.
* In **`bas`**, **`lea Rd, Symbol`** lowers to **`ADD Rd, R0, #VMA(Symbol)`** (**no memory read**). Use **`lea ivb, MyIvt`** where **`MyIvt:`** tags the vector table bytes. **`load ivb, [MyIvt]`** would mistakenly place **slot 0’s handler **`IC`** into **`IVB`**, defeating IVT lookups.
* **`PTB`** remains the page-table base register for MMU implementations; don’t overload it as the IVT pointer.

See `examples/InterruptDemo.s` (`lea ivb`, …) and `examples/MmioBootSequenceDemo.s` for MMIO + interrupt + syscall bring-up.

---


## # Floating point discipline (FSX)

* FSX registers are 64-bit FP64. 32-bit FP32 lanes are exposed via `.H`/`.L`.
* Support IEEE-754 semantics; control registers for rounding mode, flush-to-zero, trap enable and reproducibility mode.
* FMA instructions provide IEEE fused semantics in strict mode.

---

## # Tensor / matrix offload

* `MATMUL`, `GEMM_OFFLOAD`, `CONV2D` accept tile descriptors (imm128 descriptor high-level layout: pointer(s), shapes, strides, datatypes, alpha/beta scalars). Descriptor format is implementation-defined but must be passed in imm128 or memory pointed-to by imm.
* **Baseline `bvm`:** **`matmul`/`gemm`/`qgemm`/`tensor_load`/`tensor_store`/`conv2d`/`qdot`** take **`imm1`** = VMA of a memory descriptor; **`vdotp`/`dotp_acc`** use **`Xd,Xs,Xt`** / **`A#,Xs,Xt`**. **FP32** row-major tiles up to **4×4**; **`qdot`/`qgemm`** support **int8** with **FP32 scale** in descriptor; accumulators **`A0..A3`** hold **128-bit** partials. See **`examples/MlInferenceDemo.s`**.
* Offloaded units should return status via `A0..A3` or via a memory mailbox.

---

## # Assembler conventions (recommended)

* Immediate: `#0x...` = 64-bit immediate; `%0x...` = 128-bit immediate; `f32(3.14)` float immediate.
* Registers (scalar toolchain): `R0..R31`, `SP`, `IC`, `FR`, `PTB`, `IVB`, `IVL`, `WIND`, `I0..I3`, `KMASK`; plus `X*` / `F*` / etc. where implemented.
* **K-Bank (baseline `bas`):** `kwind`, `kload rd, [i0]`, `kstore rd, [i0]`, `kloadi rd, #idx`, `kstorei rd, #idx`. Example: `examples/KBankDemo.s`.
* **ALU:** `add rd, rs, rt` or `add rd, rs, rt<<4` / `#imm>>2` (shift-integrated); `sub`, `and`, `xor`, `cmp`, etc. **`mov`** aliases **`copy`**.
* **Memory:** `load rd, [sp+16]`; `store rd, [base+index+offset]`.
* **`cmp`:** `cmp rs, rt` or `cmp rs, #imm`.
* **Data movement:** `copy rd, rs` or `mov rd, rs` (**`COPY`**); `swap rd, rs` (**`SWAP`**—two GPRs only).
* **Address synthesis:** `lea Rd, Symbol` or `lea Rd, #imm` (**`ADD Rd, R0, #…` pseudo**, no **`LOAD`**).
* **FSX (preliminary `bvm`):** `f0..f7`, `fload`/`fstore`/`fadd`/`fsub`/`fmul`/`fdiv`/`fmod`/`fma`/`fcopy`/`fcmp`/`fsqrt`/`fabs`/`fneg`/`recip`/`rsqrt` (**opmode `001`**).
* **SSX (preliminary `bvm`):** `x0..x7` (128-bit), fixed-pack `xload`/`xstore`/`xcopy`/`xadd`/`xsub`/`xand`/`xxor`/`xnot`/`xswap`; scalable `vadd`/`vsub`/`vmul`/`vdiv`/`vfma`/`vfmsub`/`vfnmadd`/`vfnmsub`/`vmin`/`vmax`/`vabs`/`vneg`/`vsqrt`/`vrcp`/`vrsqrt`/`vbroadcast`; **`vload`/`vstore`** alias **`xload`/`xstore`** (**`0x148`/`0x149`**) with **`SsxVectorLength`** (default **2** FP32 lanes, **`opmode `100`**).
* **Matrix / ML (preliminary `bvm`):** **`matmul`**, **`gemm`**, **`tensor_load`**, **`tensor_store`**, **`conv2d`**, **`vdotp`**, **`dotp_acc`**, **`qdot`**, **`qgemm`** (see § Tensor / matrix offload and **`MlInferenceDemo.s`**).
* **Stack:** `push rd` or `push #imm`; `pop rd` (**64-bit slot**).
* **System:** `ei`, `di`, `int n`, `iret`, `syscall #n`, `sysret`, `eu`, `du`, `ep`, `dp`, `esr`, `dsr`, `tlbinsert`, `wiretlbentry #slot`, `pause`, `perfmon rd, #sel`, `nop`, `hlt`, `ret`.
* **Entry symbols:** Prefer **`BootEntry`** as the program entry marker; **`_start`** remains accepted for compatibility.
* Sections / data: `.text`, `.data`, `.bss`; `.quad expr` (**numeric or symbol**, symbol patched to absolute VMA in flat images).
* Vector ops: predicate may be appended: `VFMA fp32 X0, X1, X2, P0` (**`Pk`** selects mask per § SSX — scalable vectors).
* Example:

  ```
  ADD R1, R2, #0x10
  VFMA.v fp32 X0, X1, X2, P0
  VFMA.mixed X0, X1, X2     ; opmode=101 disp[1:0]=11 (widen-accum)
  ```

  Some assemblers accept **`VFMADD`** as a synonym for **`VFMA`** (**opcode `0x144`**).
---

## # ABI / Calling convention (recommended)

* Integer args: `R0..R7` (first eight); returns in `R0`. Deep calls may use **K-Bank windows** (`KSTORE`/`KLOAD` with `I0`) when windowing is implemented.
* FP scalar args: `F0..F7` ; returns in `F0`.
* Vector args: pass pointer/descriptor in `X0..X3` (or pass small vectors via `X0..X3` if ABI wants).
* Callee-saved: `R8..R15` (extend to `R8..R23` with windowed ABI), `F8..F15`, `X8..X15`, `A0..A3`.
* Caller-saved: `R0..R7`, `F0..F7`, `X0..X7`, `WIND`, `I0..I3`.
* Stack: `SP` grows downward; `PUSH` and `POP` provided for convenience but compiler will usually use `SP` arithmetic and `STORE`/`LOAD` for spills.

---

## # Examples (encoding & semantics)

### Example 1 — scalar add with immediate

Assembly:

```
ADD R1, R2, #0x10
```

Encoding steps:

* opcode = `0x010` (ADD)
* imm1_flag = 1, imm2_flag = 0
* rd = code(R1) = 0x01
* rs = code(R2) = 0x02
* rt = 0
* opmode = `000` (scalar int64)
* disp = 0
  Followed by one 64-bit immediate word: `0x0000_0000_0000_0010` (big-endian storage).

Decoder behavior: fetch base, see imm1_flag, fetch imm1, compute `R1 = R2 + imm1`.

---

### Example 2 — vector **VFMA** (FP32 lanes), masked by **P₀**

Assembly:

```
VFMA.v fp32 X0, X1, X2, P0
```

Encoding:

* opcode = **`0x144`** (**VFMA**)
* imm1_flag = 0, imm2_flag = 0
* rd = code(X0)
* rs = code(X1)
* rt = code(X2)
* opmode = **`100`** (SSX vector FP32 lanes)
* **disp**: encodes predicate register **P₀** (**e.g.** **`disp[8:5]=0`**) plus any **zeroing** / **subop** bits (see § SSX — scalable vectors).  
  **Result (per active lane *i*):** if **P₀[i]=1** then **`X0[i] ← X1[i]×X2[i] + X0[i]`** (**IEEE fused** when implementation is in strict mode); inactive lanes unchanged unless **zeroing** profile is set.
---

### Example 3 — mixed-precision **VFMA**: widen accumulate (**FP16 → FP32**)

Assembly:

```
VFMA.mixed X0, X1, X2   ; opmode 101, disp[1:0]=11 (widen-accum)
```

Encoding:

* opcode = **`0x144`** (**VFMA**)
* opmode = **`101`** (mixed)
* disp[1:0] = **`11`** (widen-accumulate); **predicate** (if any) in remaining **disp** field per implementation.
* rd/rs/rt = **X0, X1, X2**

Execution (informative microcode):

* Read **X1**, **X2** halves/lanes as packed **FP16** where selected by **`disp`**, unpack to **FP32**, perform **fused VFMA** in **FP32**, accumulate into **X0**, then pack/store according to profile.