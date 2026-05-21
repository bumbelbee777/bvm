#pragma once

/**
 * Honeycomb ISA constants aligned with docs/Honeycomb.md (Honeycomb v0.7).
 *
 * 64-bit base word layout ([63:0], big-endian in memory):
 *   [63:53] opcode (11)
 *   [52]    imm1_flag
 *   [51]    imm2_flag
 *   [50:45] rd (6)
 *   [44:39] rs (6)
 *   [38:33] rt (6)
 *   [32:30] opmode (3)
 *   [29:0]  disp (30-bit, sign-extended for PC-relative branches / misc)
 */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Honeycomb {

/// Integer scalar baseline (`000`).
constexpr uint8_t OpmodeInt64 = 0;
/// `001` FP64 scalar (baseline BVM executes few FP opcodes).
constexpr uint8_t OpmodeFp64 = 1;
/// `010` packed FP32 lanes in 64-bit container.
constexpr uint8_t OpmodeFp32Packed = 2;
/// `011` SSX generic.
constexpr uint8_t OpmodeSsxGeneric = 3;
/// `100` SSX FP32 lanes.
constexpr uint8_t OpmodeSsxFp32 = 4;
/// `101` mixed microcode path (`disp` subfields select halves / widen).
constexpr uint8_t OpmodeMixed = 5;
/// `110` atomic / privileged profiling hook.
constexpr uint8_t OpmodeAtomicPriv = 6;
/// `111` reserved.
constexpr uint8_t OpmodeReserved = 7;

/** NSX element type (`opmode` when `IsNsxOpcode`). */
constexpr uint8_t NsxOpmodeInt = 0;
constexpr uint8_t NsxOpmodeFp64 = 1;
constexpr uint8_t NsxOpmodeFp32 = 2;
constexpr uint8_t NsxOpmodeBf16 = 3;
constexpr uint8_t NsxOpmodeInt8 = 4;

enum class Opcode : uint16_t {
    NOP = 0x000,
    HLT = 0x001,
    WAIT = 0x002,
    Pause = 0x022,
    EnableSoftwareTlbRefill = 0x023,
    DisableSoftwareTlbRefill = 0x024,
    TlbInsert = 0x025,
    WireTlbEntry = 0x026,
    EI = 0x003,
    DI = 0x004,
    INT = 0x005,
    IRET = 0x006,
    SYSCALL = 0x007,
    SYSRET = 0x008,
    PERFMON = 0x009,
    EnablePaging = 0x00A,
    DisablePaging = 0x00B,
    EnableUser = 0x020,
    DisableUser = 0x021,
    SAVWIN = 0x00C,
    RESWIN = 0x00D,
    FLUSHWIN = 0x00E,
    RESTWIN = 0x00F,

    ADD = 0x010,
    SUB = 0x011,
    MUL = 0x012,
    DIV = 0x013,
    MOD = 0x014,
    INC = 0x015,
    DEC = 0x016,
    CMP = 0x017,
    AND = 0x018,
    OR = 0x019,
    XOR = 0x01A,
    NOT = 0x01B,
    RSH = 0x01C,
    LSH = 0x01D,
    ROR = 0x01E,
    ROL = 0x01F,

    JMP = 0x030,
    JE = 0x031,
    JNE = 0x032,
    JZ = 0x033,
    JNZ = 0x034,
    JC = 0x035,
    JNC = 0x036,
    JLT = 0x037,
    JGT = 0x038,
    JO = 0x039,
    JNO = 0x03A,
    CALL = 0x03B,
    RET = 0x03C,
    SWAP = 0x03D,
    COPY = 0x03E,
    PUSH = 0x03F,
    POP = 0x040,

    LOAD = 0x050,
    STORE = 0x051,
    XLOAD = 0x052,
    XSTORE = 0x053,
    FLOAD = 0x054,
    FSTORE = 0x055,
    PREFETCH = 0x056,
    IN = 0x057,
    OUT = 0x058,
    /// Reserved SSX/I/O mnemonic `VXSWAP` in prose; opcode `0x059` (`docs/Honeycomb.md`).
    XSWAP = 0x059,

    ATAND = 0x060,
    ATOR = 0x061,
    ATNOT = 0x062,
    ATXOR = 0x063,
    ATADD = 0x064,
    ATSUB = 0x065,
    ATMUL = 0x066,
    ATDIV = 0x067,
    ATFADD = 0x068,
    ATFSUB = 0x069,
    ATCAS = 0x06A,
    ATCMP = 0x06B,
    ATINC = 0x06C,
    ATDEC = 0x06D,
    ATLOAD = 0x06E,
    ATSTORE = 0x06F,
    ATSWAP = 0x070,
    ATREDUCE = 0x071,

    IGLOAD = 0x072,
    IGSTORE = 0x073,
    IWIND = 0x074,
    XSPILL = 0x075,
    XRELOAD = 0x076,
    KLOAD = 0x077,
    KSTORE = 0x078,
    KLOADI = 0x079,
    KSTOREI = 0x07A,
    KWIND = 0x07B,
    KRELOAD = 0x07C,
    KSPILL = 0x07D,
    KROT = 0x07E,

    RDWIN = 0x080,
    WRWIN = 0x081,
    STREAM = 0x082,
    STREAMOUT = 0x083,
    STREAMADV = 0x084,
    STREAMWAIT = 0x085,
    STREAMFENCE = 0x086,
    STREAMBIND = 0x087,
    STREAMUNBIND = 0x088,
    NUMABIND = 0x089,
    NUMASTREAM = 0x08A,
    NUMAFENCE = 0x08B,
    NUMAADV = 0x08C,
    NUMAATOMIC = 0x08D,
    HINT_OP = 0x08E,
    HINT_BARRIER = 0x08F,

    SKB_LOAD = 0x090,
    SKB_STORE = 0x091,
    SKB_BIND = 0x092,
    SKB_UNBIND = 0x093,
    SKB_FLUSH = 0x094,
    SKB_INVALIDATE = 0x095,
    SKB_PREFETCH = 0x096,
    SKB_ZERO = 0x097,
    SKB_AT_LOAD = 0x098,
    SKB_AT_STORE = 0x099,
    SKB_AT_ADD = 0x09A,
    SKB_AT_CAS = 0x09B,
    SKB_AT_SWAP = 0x09C,
    SKB_AT_REDUCE = 0x09D,
    SKB_PIN = 0x09E,
    SKB_UNPIN = 0x09F,
    SKB_MOVE = 0x0A0,
    SKB_BULK_LOAD = 0x0A1,
    SKB_BULK_STORE = 0x0A2,
    SKB_BULK_ZERO = 0x0A3,
    SKB_STATUS = 0x0A4,

    /** Complex scalar (v0.7-CPLX, `opmode=101`, `disp[29:27]=100`). */
    CADD = 0x0B0,
    CSUB = 0x0B1,
    CMUL = 0x0B2,
    CDIV = 0x0B3,
    CMAGMUL = 0x0B4,
    CMAG = 0x0B5,
    CARG = 0x0B6,
    CCONJ = 0x0B7,
    CNEG = 0x0B8,
    CABS = 0x0B9,
    CLOAD = 0x0C8,
    CSTORE = 0x0C9,
    CMOVE = 0x0CC,
    CEXTRACT = 0x0CE,
    CPACK = 0x0D0,
    CMSQ = 0x0C5,
    CFMA = 0x0C0,
    CFMS = 0x0C1,
    CNFMA = 0x0C2,
    CNFMS = 0x0C3,
    CMAC = 0x0C4,
    CDOT = 0x0C6,
    CNORM = 0x0C7,
    CLOAD_SOA = 0x0CA,
    CSTORE_SOA = 0x0CB,
    CSWAP = 0x0CD,
    CINSERT = 0x0CF,
    CUNPACK = 0x0D1,
    CSPLAT = 0x0D3,
    CCEQ = 0x0D4,
    CCNE = 0x0D5,
    CCMPLT = 0x0D6,
    CCMPGT = 0x0D7,
    CSEL = 0x0D8,
    CMIN = 0x0D9,
    CMAX = 0x0DA,
    CSCALE = 0x0BA,
    CROT = 0x0BB,
    CEXP = 0x0BC,
    CLOG = 0x0BD,
    CPOW = 0x0BE,
    CSQRT = 0x0BF,
    CKLOAD = 0x0DB,
    CKSTORE = 0x0DC,

    VCADD = 0x1A0,
    VCSUB = 0x1A1,
    VCMUL = 0x1A2,
    VCDIV = 0x1A3,
    VCFMA = 0x1A4,
    VCFMS = 0x1A5,
    VCMAC = 0x1A6,
    VCMAG = 0x1A7,
    VCARG = 0x1A8,
    VCCONJ = 0x1A9,
    VCFFT2 = 0x1AE,
    VCFFT4 = 0x1AF,
    VCLOAD = 0x1B0,
    VCSTORE = 0x1B1,
    VCBROADCAST = 0x1B4,
    VCSCALE = 0x1AA,
    VCROT = 0x1AB,
    VCDOT = 0x1AC,
    VCNORM = 0x1AD,
    VCGATHER = 0x1B2,
    VCSCATTER = 0x1B3,
    VCSPLAT = 0x1B5,
    VCREDUCE = 0x1B6,
    VCCMP = 0x1B7,
    VCSEL = 0x1B8,
    VCMIN = 0x1B9,
    VCMAX = 0x1BA,
    VCZIP = 0x1BB,
    VCUNZIP = 0x1BC,
    VCSWAP = 0x1BD,
    VCREVERSE = 0x1BE,
    VCTWIDDLE = 0x1BF,

    /** Architected alias per doc: contiguous `VL×ES` SSX load (also `Opcode::XLOAD`). */
    VLOAD = 0x148,
    /** Architected alias: contiguous SSX store (also `Opcode::XSTORE`). */
    VSTORE = 0x149,

    /** SSX fixed 128-bit pack (0x100..0x12F). */
    XAND = 0x100,
    XXOR = 0x101,
    XNOT = 0x102,
    XADD = 0x103,
    XSUB = 0x104,
    XMUL = 0x105,
    XDIV = 0x106,
    XMOD = 0x107,
    XCMP = 0x108,
    XRSH = 0x109,
    XLSH = 0x10A,
    XROR = 0x10B,
    XROL = 0x10C,
    /** Fixed-pack 128-bit load (alias semantics of `Opcode::XLOAD` at `0x052`). */
    XLOAD128 = 0x10D,
    XSTORE128 = 0x10E,
    XCOPY = 0x10F,
    XSWAP128 = 0x110,
    XCRYPT = 0x111,
    XDECRYPT = 0x112,
    XGEN = 0x113,

    /** SSX scalable vectors (0x140..). */
    VADD = 0x140,
    VSUB = 0x141,
    VMUL = 0x142,
    VDIV = 0x143,
    VFMA = 0x144,
    VFMSUB = 0x145,
    VFNMADD = 0x146,
    VFNMSUB = 0x147,
    VBROADCAST = 0x14C,
    VMIN = 0x150,
    VMAX = 0x151,
    VABS = 0x152,
    VNEG = 0x153,
    VSQRT = 0x154,
    VRCP = 0x155,
    VRSQRT = 0x156,
    VDOTP = 0x15B,

    /** Tensor / matrix units (0x180..). */
    MATMUL = 0x180,
    GEMM_OFFLOAD = 0x181,
    TENSOR_LOAD = 0x182,
    TENSOR_STORE = 0x183,
    CONV2D = 0x184,
    DOTP_ACC = 0x185,

    QDOT = 0x300,
    QGEMM = 0x301,

    /** FSX scalar floating (0x200..). */
    FADD = 0x200,
    FSUB = 0x201,
    FMUL = 0x202,
    FDIV = 0x203,
    FMOD = 0x204,
    FMA = 0x205,
    FCMP = 0x206,
    FRSH = 0x207,
    FLSH = 0x208,
    FROL = 0x209,
    FROR = 0x20A,
    FCOPY = 0x20D,
    FSQRT = 0x20E,
    FABS = 0x20F,
    FNEG = 0x210,
    FTOI = 0x211,
    ITOF = 0x212,

    SATADD = 0x302,
    SATMUL = 0x303,
    RECIP = 0x304,
    RSQRT = 0x305,

    /** DAX — DAtaflow eXtensions (0x400..0x46F, K-Bank token slots). */
    DAX_ADD = 0x400,
    DAX_SUB = 0x401,
    DAX_MUL = 0x402,
    DAX_DIV = 0x403,
    DAX_FMA = 0x404,
    DAX_CMP = 0x405,
    DAX_SEL = 0x406,
    DAX_ADDI = 0x407,
    DAX_MULI = 0x408,
    DAX_CMPI = 0x409,

    DAX_VADD = 0x410,
    DAX_VMUL = 0x411,
    DAX_VFMA = 0x412,
    DAX_VLOAD = 0x413,
    DAX_VSTORE = 0x414,

    DAX_FORK = 0x420,
    DAX_JOIN = 0x421,
    DAX_MERGE = 0x422,
    DAX_BRANCH = 0x423,
    DAX_CALL = 0x424,
    DAX_RET = 0x425,
    DAX_LOOP = 0x426,
    DAX_SERIALIZE = 0x427,

    DAX_TOKEN = 0x430,
    DAX_CONSUME = 0x431,
    DAX_PEEK = 0x432,
    DAX_PROMOTE = 0x433,
    DAX_DEMOTE = 0x434,
    DAX_REFILL = 0x435,
    DAX_VALID = 0x436,
    DAX_REFCNT = 0x437,
    DAX_TAG = 0x438,
    DAX_WAIT = 0x439,
    DAX_WAIT_ALL = 0x43A,
    DAX_WAIT_ANY = 0x43B,

    DAX_LOAD = 0x440,
    DAX_STORE = 0x441,
    DAX_ATOMIC = 0x442,
    DAX_STREAM = 0x443,
    DAX_GATHER = 0x444,

    DAX_MATMUL = 0x450,
    DAX_CONV = 0x451,
    DAX_REDUCE = 0x452,
    DAX_SCAN = 0x453,

    DAX_SEND = 0x460,
    DAX_RECV = 0x461,
    DAX_BCAST = 0x462,
    DAX_REDUCE_SCATTER = 0x463,

    /** NSX — Neural Support eXtensions (0x600..0x6FF). */
    SOFTMAX = 0x600,
    LAYERNORM = 0x601,
    BATCHNORM = 0x602,
    DROPOUT = 0x603,
    NSX_RELU = 0x604,
    GELU = 0x605,
    SWISH = 0x606,
    SILU = 0x607,
    NSX_TANH = 0x608,
    SIGMOID = 0x609,

    MSE_LOSS = 0x60A,
    CROSS_ENTROPY = 0x60B,
    L2_LOSS = 0x60C,

    CONV1D = 0x620,
    NSX_CONV2D = 0x621,
    CONV3D = 0x622,
    DWCONV2D = 0x623,
    SEPCONV2D = 0x624,
    POOL_MAX = 0x625,
    POOL_AVG = 0x626,
    POOL_GLOBAL = 0x627,
    UPSAMPLE = 0x628,

    ATTENTION = 0x640,
    CROSS_ATTN = 0x641,
    SELF_ATTN = 0x642,
    FLASH_ATTN = 0x643,
    PAGED_ATTN = 0x644,

    TRANSFORMER = 0x660,

    RNN_CELL = 0x670,
    LSTM_CELL = 0x671,
    GRU_CELL = 0x672,
    RNN_SEQ = 0x673,
    LSTM_SEQ = 0x674,
    GRU_SEQ = 0x675,
    BIDIR_LSTM = 0x676,

    FWD_START = 0x680,
    FWD_END = 0x681,
    FWD_LAYER = 0x682,
    BWD_START = 0x683,
    BWD_ALL = 0x684,
    BWD_GRAD = 0x685,
    CKPT = 0x690,
    CKPT_RESTORE = 0x691,
    CKPT_DISCARD = 0x692,

    SGD = 0x6A0,
    ADAM = 0x6A1,
    LAMB = 0x6A2,

    LAPLACIAN_2D = 0x6C0,
    LAPLACIAN_3D = 0x6C1,
    HESSIAN_2D = 0x6C2,
    HESSIAN_3D = 0x6C3,
    NEWTON_STEP = 0x6C4,

    /** Security architecture (0x500..0x521). */
    PACIA = 0x500,
    PACDA = 0x501,
    PACIB = 0x502,
    AUTIA = 0x503,
    AUTDA = 0x504,
    XPACI = 0x505,
    CFI_LABEL = 0x506,
    CFI_CALL = 0x507,
    CFI_JUMP = 0x508,
    SSP_PUSH = 0x509,
    SSP_POP = 0x50A,
    SSP_CHECK = 0x50B,
    SANDBOX_ALLOW = 0x50C,
    SANDBOX_DENY = 0x50D,
    SANDBOX_RESET = 0x50E,
    CAP_LOAD = 0x50F,
    CAP_STORE = 0x510,
    CAP_CALL = 0x511,
    CAP_SEAL = 0x512,
    CAP_UNSEAL = 0x513,
    ENC_LOAD = 0x514,
    ENC_STORE = 0x515,
    ENC_GENKEY = 0x516,
    ENC_SWITCH = 0x517,
    MEASURE = 0x518,
    QUOTE = 0x519,
    SEAL = 0x51A,
    UNSEAL = 0x51B,
    VMLAUNCH = 0x51C,
    VMRESUME = 0x51D,
    VMEXIT = 0x51E,
    VMREAD = 0x51F,
    VMWRITE = 0x520,
    PAC_KEY_INIT = 0x521,
};

enum class Reg : uint8_t {
    /** GPRs (`0x00..0x1F`): `R0`..`R31`. */
    R0 = 0x00, R1, R2, R3, R4, R5, R6, R7,
    R8, R9, R10, R11, R12, R13, R14, R15,
    R16, R17, R18, R19, R20, R21, R22, R23,
    R24, R25, R26, R27, R28, R29, R30, R31,
    /** Scalar control / special-purpose (`0x20..0x27`). */
    SP = 0x20,
    IC = 0x21,
    FR = 0x22,
    PTB = 0x23,
    IVB = 0x24,
    IVL = 0x25,
    WIND = 0x26,
    WINSZ = 0x27,
    /** Index registers for K-Bank / gather (`0x28..0x2B`). */
    I0 = 0x28,
    I1 = 0x29,
    I2 = 0x2A,
    I3 = 0x2B,
    TlbFaultAddr = 0x2C,
    TlbFaultKind = 0x2D,
    ZERO = 0x5C,
    KMASK = 0x5F,
    CWP = 0x58,
    CANSAVE = 0x59,
    CANRESTORE = 0x5A,
    OTHERWIN = 0x5B,
    ROTBASE = 0x5D,
    ROTSZ = 0x5E
};

namespace StatusFlags {
    constexpr uint64_t Carry = 1ULL << 0;
    constexpr uint64_t Zero = 1ULL << 1;
    constexpr uint64_t Sign = 1ULL << 2;
    constexpr uint64_t Overflow = 1ULL << 3;
    constexpr uint64_t Equal = 1ULL << 4;
    constexpr uint64_t Greater = 1ULL << 5;
    constexpr uint64_t Interrupt = 1ULL << 7;
    /// When set, `LOAD`/`STORE`/fetch use `PTB` + the TLB (see `EP`/`DP`).
    constexpr uint64_t Paging = 1ULL << 8;
    /// When set, the core runs in user mode (see `EU`/`DU`). Cleared on `SYSCALL` entry.
    constexpr uint64_t UserMode = 1ULL << 9;
    /// When set with paging, TLB misses trap to vector **TlbMiss** for software refill.
    constexpr uint64_t SoftwareTlbRefill = 1ULL << 10;
    /// 16-bit instruction fetch/decode when set (entered via CALL/RET mode tag).
    constexpr uint64_t CompactMode = 1ULL << 11;
    constexpr uint64_t Hypervisor = 1ULL << 12;
}

struct DecodedInsn {
    Opcode Op = Opcode::NOP;
    bool CompactForm = false;
    bool Imm1Flag = false;
    bool Imm2Flag = false;
    uint8_t Rd = 0;
    uint8_t Rs = 0;
    uint8_t Rt = 0;
    uint8_t Opmode = 0;
    int32_t Disp = 0;
    uint64_t Imm1 = 0;
    uint64_t Imm2 = 0;
    uint64_t BaseWord = 0;
    uint64_t Address = 0;
};

struct EncodedInsn {
    uint64_t Base = 0;
    uint64_t Imm1 = 0;
    uint64_t Imm2 = 0;
    bool HasImm1 = false;
    bool HasImm2 = false;

    size_t ByteSize() const {
        size_t Size = 8;
        if (HasImm1) Size += 8;
        if (HasImm2) Size += 8;
        return Size;
    }
};

inline std::string ToLower(std::string Value) {
    std::transform(Value.begin(), Value.end(), Value.begin(),
                   [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
    return Value;
}

inline uint64_t SwapBE64(uint64_t Value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((Value & 0x00000000000000FFULL) << 56) |
           ((Value & 0x000000000000FF00ULL) << 40) |
           ((Value & 0x0000000000FF0000ULL) << 24) |
           ((Value & 0x00000000FF000000ULL) << 8) |
           ((Value & 0x000000FF00000000ULL) >> 8) |
           ((Value & 0x0000FF0000000000ULL) >> 24) |
           ((Value & 0x00FF000000000000ULL) >> 40) |
           ((Value & 0xFF00000000000000ULL) >> 56);
#else
    return Value;
#endif
}

inline void WriteBE16(uint8_t* Dest, uint16_t Value) {
    Dest[0] = static_cast<uint8_t>((Value >> 8) & 0xFF);
    Dest[1] = static_cast<uint8_t>(Value & 0xFF);
}

inline uint16_t ReadBE16(const uint8_t* Src) {
    return static_cast<uint16_t>((static_cast<uint16_t>(Src[0]) << 8) | Src[1]);
}

inline void WriteBE64(uint8_t* Dest, uint64_t Value) {
    Value = SwapBE64(Value);
    std::memcpy(Dest, &Value, sizeof(Value));
}

inline uint64_t ReadBE64(const uint8_t* Src) {
    uint64_t Value = 0;
    std::memcpy(&Value, Src, sizeof(Value));
    return SwapBE64(Value);
}

inline uint64_t PackBase(Opcode OpcodeValue, bool Imm1Flag, bool Imm2Flag,
                         uint8_t Rd, uint8_t Rs, uint8_t Rt,
                         uint8_t Opmode, int32_t Disp) {
    uint64_t Word = (static_cast<uint64_t>(OpcodeValue) << 53);
    if (Imm1Flag) Word |= (1ULL << 52);
    if (Imm2Flag) Word |= (1ULL << 51);
    Word |= (static_cast<uint64_t>(Rd & 0x3F) << 45);
    Word |= (static_cast<uint64_t>(Rs & 0x3F) << 39);
    Word |= (static_cast<uint64_t>(Rt & 0x3F) << 33);
    Word |= (static_cast<uint64_t>(Opmode & 0x7) << 30);
    Word |= (static_cast<uint64_t>(Disp) & 0x3FFFFFFF);
    return Word;
}

inline DecodedInsn DecodeBase(uint64_t BaseWord) {
    DecodedInsn Result;
    Result.BaseWord = BaseWord;
    Result.Op = static_cast<Opcode>((BaseWord >> 53) & 0x7FF);
    Result.Imm1Flag = (BaseWord >> 52) & 1;
    Result.Imm2Flag = (BaseWord >> 51) & 1;
    Result.Rd = static_cast<uint8_t>((BaseWord >> 45) & 0x3F);
    Result.Rs = static_cast<uint8_t>((BaseWord >> 39) & 0x3F);
    Result.Rt = static_cast<uint8_t>((BaseWord >> 33) & 0x3F);
    Result.Opmode = static_cast<uint8_t>((BaseWord >> 30) & 0x7);
    Result.Disp = static_cast<int32_t>((BaseWord & 0x3FFFFFFFULL) << 2) >> 2;
    return Result;
}

inline void AppendCompact(std::vector<uint8_t>& Buffer, uint16_t Word) {
    const size_t Offset = Buffer.size();
    Buffer.resize(Offset + 2);
    WriteBE16(Buffer.data() + Offset, Word);
}

inline void AppendEncoded(std::vector<uint8_t>& Buffer, const EncodedInsn& Insn) {
    size_t Offset = Buffer.size();
    Buffer.resize(Offset + Insn.ByteSize());
    WriteBE64(Buffer.data() + Offset, Insn.Base);
    size_t Pos = Offset + 8;
    if (Insn.HasImm1) {
        WriteBE64(Buffer.data() + Pos, Insn.Imm1);
        Pos += 8;
    }
    if (Insn.HasImm2) {
        WriteBE64(Buffer.data() + Pos, Insn.Imm2);
    }
}

inline bool TryParseIndexRegister(const std::string& Token, uint8_t& Code) {
    std::string Lower = ToLower(Token);
    if (Lower.size() == 2 && Lower[0] == 'i' && Lower[1] >= '0' && Lower[1] <= '3') {
        const int Num = Lower[1] - '0';
        Code = static_cast<uint8_t>(static_cast<uint8_t>(Reg::I0) + Num);
        return true;
    }
    return false;
}

inline bool TryParseComplexSsxRegister(const std::string& Token, uint8_t& Index) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 2 && Lower[0] == 'c' && Lower[1] == 'x') {
        int Num = std::stoi(Lower.substr(2));
        if (Num >= 0 && Num <= 15) {
            Index = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

inline bool IsComplexOpcode(Opcode Op) {
    const uint16_t Value = static_cast<uint16_t>(Op);
    return (Value >= 0x0B0 && Value <= 0x0DC) || (Value >= 0x1A0 && Value <= 0x1BF);
}

inline bool IsDaxOpcode(Opcode Op) {
    const uint16_t Value = static_cast<uint16_t>(Op);
    return Value >= 0x400 && Value <= 0x46F;
}

inline bool IsNsxOpcode(Opcode Op) {
    const uint16_t Value = static_cast<uint16_t>(Op);
    return Value >= 0x600 && Value <= 0x6FF;
}

inline bool IsSecurityOpcode(Opcode Op) {
    const uint16_t Value = static_cast<uint16_t>(Op);
    return Value >= 0x500 && Value <= 0x521;
}

/** WIND-relative K-slot operand (`k0`..`k63` in assembly). */
inline bool TryParseKSlot(const std::string& Token, uint8_t& Slot) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 2 && Lower[0] == 'k') {
        const int Num = std::stoi(Lower.substr(1));
        if (Num >= 0 && Num <= 63) {
            Slot = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

inline bool TryParseComplexRegister(const std::string& Token, uint8_t& Index) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 3 && Lower[0] == 'c' && Lower[1] == 'r') {
        int Num = std::stoi(Lower.substr(2));
        if (Num >= 0 && Num <= 31) {
            Index = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

inline bool TryParseRegister(const std::string& Token, uint8_t& Code) {
    std::string Lower = ToLower(Token);
    static const struct { const char* Name; Reg RegCode; } Special[] = {
        {"sp", Reg::SP},     {"ic", Reg::IC},     {"fr", Reg::FR},
        {"ptb", Reg::PTB},   {"ivb", Reg::IVB},   {"ivl", Reg::IVL},
        {"wind", Reg::WIND}, {"winsz", Reg::WINSZ}, {"kmask", Reg::KMASK},
        {"cwp", Reg::CWP}, {"cansave", Reg::CANSAVE},
        {"canrestore", Reg::CANRESTORE}, {"rotbase", Reg::ROTBASE},
        {"rotsz", Reg::ROTSZ}, {"ctype", Reg::WINSZ},
        {"tfa", Reg::TlbFaultAddr}, {"tfk", Reg::TlbFaultKind},
        {"zero", Reg::ZERO},
        {"pac_key_h", static_cast<Reg>(0x60)},
        {"pac_key_l", static_cast<Reg>(0x61)},
        {"stack_guard", static_cast<Reg>(0x64)},
        {"shadow_stack_base", static_cast<Reg>(0x65)},
        {"shadow_stack_top", static_cast<Reg>(0x66)},
        {"rop_trap", static_cast<Reg>(0x67)},
        {"sandbox_mask", static_cast<Reg>(0x68)},
        {"sandbox_base", static_cast<Reg>(0x69)},
        {"sandbox_limit", static_cast<Reg>(0x6A)},
        {"vmcs_base", static_cast<Reg>(0x6E)},
        {"pcr_base", static_cast<Reg>(0x6D)},
    };
    for (const auto& Entry : Special) {
        if (Lower == Entry.Name) {
            Code = static_cast<uint8_t>(Entry.RegCode);
            return true;
        }
    }
    if (TryParseIndexRegister(Lower, Code)) {
        return true;
    }
    if (Lower.size() >= 2 && Lower[0] == 'r') {
        size_t End = 1;
        while (End < Lower.size() &&
               std::isdigit(static_cast<unsigned char>(Lower[End]))) {
            ++End;
        }
        if (End == Lower.size()) {
            const int Num = std::stoi(Lower.substr(1));
            if (Num >= 0 && Num <= 31) {
                Code = static_cast<uint8_t>(Num);
                return true;
            }
        }
    }
    return false;
}

inline uint64_t ParseImmediateToken(const std::string& Token) {
    std::string Value = Token;
    if (!Value.empty() && Value[0] == '#') {
        Value = Value.substr(1);
    }
    int Base = 10;
    if (Value.size() > 2 && Value[0] == '0' &&
        (Value[1] == 'x' || Value[1] == 'X')) {
        Base = 16;
    } else if (Value.size() > 2 && Value[0] == '0' &&
               (Value[1] == 'b' || Value[1] == 'B')) {
        Base = 2;
        Value = Value.substr(2);
    }
    return std::stoull(Value, nullptr, Base);
}

inline bool IsGpReg(uint8_t Code) {
    return Code <= static_cast<uint8_t>(Reg::R31);
}

inline bool IsIndexReg(uint8_t Code) {
    return Code >= static_cast<uint8_t>(Reg::I0) &&
           Code <= static_cast<uint8_t>(Reg::I3);
}

inline bool TryParseSsxRegister(const std::string& Token, uint8_t& Code) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 2 && Lower[0] == 'x') {
        int Num = std::stoi(Lower.substr(1));
        if (Num >= 0 && Num <= 15) {
            Code = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

inline bool TryParseSsxOrComplexSsxRegister(const std::string& Token, uint8_t& Code) {
    return TryParseSsxRegister(Token, Code) || TryParseComplexSsxRegister(Token, Code);
}

inline bool TryParseAccRegister(const std::string& Token, uint8_t& Code) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 2 && Lower[0] == 'a') {
        int Num = std::stoi(Lower.substr(1));
        if (Num >= 0 && Num <= 3) {
            Code = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

inline bool TryParseFsxRegister(const std::string& Token, uint8_t& Code) {
    std::string Lower = ToLower(Token);
    if (Lower.size() >= 2 && Lower[0] == 'f') {
        int Num = std::stoi(Lower.substr(1));
        if (Num >= 0 && Num <= 15) {
            Code = static_cast<uint8_t>(Num);
            return true;
        }
    }
    return false;
}

/** NSX operand: SSX `x*`, WIND-relative `k*`, or GPR `r*`. */
inline bool TryParseNsxRegister(const std::string& Token, uint8_t& Code) {
    return TryParseSsxRegister(Token, Code) || TryParseKSlot(Token, Code) ||
           TryParseRegister(Token, Code);
}

} // namespace Honeycomb
