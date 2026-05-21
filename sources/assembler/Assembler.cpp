#include "Assembler.h"
#include "../Isa.h"
#include "../bvm/AluShiftProfile.h"
#include "../bvm/CompactProfile.h"
#include "../bvm/PhysMap.h"
#include "../bvm/SkbProfile.h"
#include "../bvm/CplxProfile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace Honeycomb;

namespace {

enum class SectionKind { None, Text, Data, Bss };

struct SymbolInfo {
    std::string Name;
    uint64_t Offset = 0;
    SectionKind Section = SectionKind::Text;
    bool TextCompact = true;
};

struct Fixup {
    size_t PatchOffset = 0;
    std::string Symbol;
    /** Text immediates and branch displacements use final merged-image offsets. */
    enum class Kind {
        BranchDisp,
        AbsoluteImm,
        AbsoluteImmData,
        CompactBranch5,
        CompactCall10,
        CallTargetImm,
    } Type = Kind::BranchDisp;
    uint8_t BranchRd = 0;
};

bool AssemblingCompact = false;

SectionKind CurrentSection = SectionKind::Text;
std::vector<uint8_t> TextSection;
std::vector<uint8_t> DataSection;
std::unordered_map<std::string, SymbolInfo> SymbolTable;
std::vector<Fixup> Fixups;

void ResetState() {
    CurrentSection = SectionKind::Text;
    TextSection.clear();
    DataSection.clear();
    SymbolTable.clear();
    Fixups.clear();
    AssemblingCompact = false;
}

std::vector<uint8_t>& ActiveBytes() {
    return CurrentSection == SectionKind::Data ? DataSection : TextSection;
}

void PadTextTo8() {
    while (TextSection.size() % 8 != 0) {
        TextSection.push_back(0);
    }
}

void EmitCompactWord(uint16_t Word) {
    AppendCompact(ActiveBytes(), Word);
}

uint8_t RequireCompactGpr(const std::string& Token) {
    uint8_t Code = 0;
    if (!TryParseRegister(Token, Code) || Code > 31) {
        throw std::runtime_error("Compact encoding requires GPR R0..R31");
    }
    return Code;
}

uint16_t EncodeCompactMnemonic(const std::string& Mnemonic,
                               const std::vector<std::string>& Operands) {
    const std::string Op = ToLower(Mnemonic);
    using CompactOp = CompactProfile::CompactOp;

    if (Op == "nop") {
        return CompactProfile::PackRr(CompactOp::NOP, 0, 0);
    }
    if (Op == "hlt") {
        return CompactProfile::PackRr(CompactOp::HLT, 0, 0);
    }
    if (Op == "ret") {
        return CompactProfile::PackRr(CompactOp::RET, 0, 0);
    }
    if (Op == "mov" || Op == "copy") {
        if (Operands.size() != 2) {
            throw std::runtime_error("Compact MOV requires <dst>, <src>");
        }
        return CompactProfile::PackRr(CompactOp::MOV, RequireCompactGpr(Operands[0]),
                                      RequireCompactGpr(Operands[1]));
    }
    if (Op == "movs") {
        if (Operands.size() != 1) {
            throw std::runtime_error("Compact MOVS requires <dst>");
        }
        return CompactProfile::PackRr(CompactOp::MOVS, RequireCompactGpr(Operands[0]), 0);
    }
    if (Op == "add" || Op == "sub") {
        if (Operands.size() != 2) {
            throw std::runtime_error("Compact ADD/SUB require <dst>, <src>");
        }
        const CompactOp Cop = (Op == "add") ? CompactOp::ADD : CompactOp::SUB;
        return CompactProfile::PackRr(Cop, RequireCompactGpr(Operands[0]),
                                      RequireCompactGpr(Operands[1]));
    }
    if (Op == "ldi") {
        if (Operands.size() != 2) {
            throw std::runtime_error("Compact LDI requires <dst>, #imm");
        }
        const int64_t Imm = static_cast<int64_t>(ParseImmediateToken(Operands[1]));
        if (Imm < -16 || Imm > 15) {
            throw std::runtime_error("Compact LDI immediate out of range (-16..15)");
        }
        const uint8_t Enc = static_cast<uint8_t>(Imm < 0 ? Imm + 32 : Imm);
        return CompactProfile::PackRi(CompactOp::LDI, RequireCompactGpr(Operands[0]), Enc);
    }
    if (Op == "cmpi") {
        if (Operands.size() != 2) {
            throw std::runtime_error("Compact CMPI requires <reg>, #imm");
        }
        const int64_t Imm = static_cast<int64_t>(ParseImmediateToken(Operands[1]));
        if (Imm < -16 || Imm > 15) {
            throw std::runtime_error("Compact CMPI immediate out of range (-16..15)");
        }
        const uint8_t Enc = static_cast<uint8_t>(Imm < 0 ? Imm + 32 : Imm);
        return CompactProfile::PackRi(CompactOp::CMPI, RequireCompactGpr(Operands[0]), Enc);
    }
    if (Op == "lw" || Op == "sw") {
        if (Operands.size() != 2) {
            throw std::runtime_error("Compact LW/SW require <reg>, #slot");
        }
        const int64_t Slot = static_cast<int64_t>(ParseImmediateToken(Operands[1]));
        if (Slot < -16 || Slot > 15) {
            throw std::runtime_error("Compact stack slot out of range (-16..15)");
        }
        const uint8_t Enc = static_cast<uint8_t>(Slot < 0 ? Slot + 32 : Slot);
        const CompactOp Cop = (Op == "lw") ? CompactOp::LW : CompactOp::SW;
        return CompactProfile::PackRi(Cop, RequireCompactGpr(Operands[0]), Enc);
    }
    if (Op == "push" || Op == "pop") {
        if (Operands.size() != 1) {
            throw std::runtime_error("Compact PUSH/POP require one register");
        }
        const CompactOp Cop = (Op == "push") ? CompactOp::PUSH : CompactOp::POP;
        return CompactProfile::PackRr(Cop, RequireCompactGpr(Operands[0]), 0);
    }
    if (Op == "bz" || Op == "bnz" || Op == "jmp") {
        std::string Target;
        uint8_t Rd = 0;
        if (Op == "jmp") {
            if (Operands.size() != 1) {
                throw std::runtime_error("Compact JMP requires <label>");
            }
            Target = Operands[0];
        } else {
            if (Operands.size() == 1) {
                Rd = 0;
                Target = Operands[0];
            } else if (Operands.size() == 2) {
                Rd = RequireCompactGpr(Operands[0]);
                Target = Operands[1];
            } else {
                throw std::runtime_error("Compact BZ/BNZ require [<reg>,] <label>");
            }
        }
        const CompactOp Cop =
            (Op == "bz") ? CompactOp::BZ : (Op == "bnz") ? CompactOp::BNZ : CompactOp::JMP;
        const size_t PatchAt = ActiveBytes().size();
        const uint16_t Placeholder =
            (Op == "jmp") ? CompactProfile::PackRi(CompactOp::JMP, 0, 0)
                          : CompactProfile::PackRi(Cop, Rd, 0);
        EmitCompactWord(Placeholder);
        Fixup Entry;
        Entry.PatchOffset = PatchAt;
        Entry.Symbol = Target;
        Entry.Type = Fixup::Kind::CompactBranch5;
        Entry.BranchRd = Rd;
        Fixups.push_back(Entry);
        return 0;
    }
    if (Op == "call") {
        if (Operands.size() != 1) {
            throw std::runtime_error("Compact CALL requires <label>");
        }
        const size_t PatchAt = ActiveBytes().size();
        EmitCompactWord(CompactProfile::PackCall(0, false));
        Fixup Entry;
        Entry.PatchOffset = PatchAt;
        Entry.Symbol = Operands[0];
        Entry.Type = Fixup::Kind::CompactCall10;
        Fixups.push_back(Entry);
        return 0;
    }
    throw std::runtime_error("Unknown compact instruction: " + Mnemonic);
}

std::string Trim(const std::string& Line) {
    size_t Start = Line.find_first_not_of(" \t\r\n");
    if (Start == std::string::npos) return "";
    size_t End = Line.find_last_not_of(" \t\r\n");
    std::string Result = Line.substr(Start, End - Start + 1);
    while (!Result.empty() && Result.back() == '\r') {
        Result.pop_back();
    }
    return Result;
}

std::string StripComment(const std::string& Line) {
    size_t Pos = Line.find("//");
    return Pos == std::string::npos ? Line : Line.substr(0, Pos);
}

std::vector<std::string> TokenizeLine(const std::string& Line) {
    std::vector<std::string> Tokens;
    std::string Current;
    bool InBrackets = false;
    for (char Ch : Line) {
        if (Ch == '[') {
            if (!Current.empty()) {
                Tokens.push_back(Current);
                Current.clear();
            }
            InBrackets = true;
            Current.push_back(Ch);
        } else if (Ch == ']') {
            Current.push_back(Ch);
            Tokens.push_back(Current);
            Current.clear();
            InBrackets = false;
        } else if ((Ch == ',' || Ch == ' ' || Ch == '\t') && !InBrackets) {
            if (!Current.empty()) {
                Tokens.push_back(Current);
                Current.clear();
            }
        } else {
            Current.push_back(Ch);
        }
    }
    if (!Current.empty()) Tokens.push_back(Current);
    return Tokens;
}

bool IsNumeric(const std::string& Token) {
    if (Token.empty()) return false;
    std::string Value = Token;
    if (Value[0] == '#') Value = Value.substr(1);
    return std::isdigit(static_cast<unsigned char>(Value[0])) ||
           (Value.size() > 2 && Value[0] == '0');
}

void RecordSymbol(const std::string& Name) {
    SymbolInfo Info;
    Info.Name = Name;
    Info.Offset = ActiveBytes().size();
    Info.Section = CurrentSection;
    Info.TextCompact = AssemblingCompact;
    SymbolTable[Name] = Info;
}

uint64_t ResolveAddress(const std::string& Name) {
    auto It = SymbolTable.find(Name);
    if (It == SymbolTable.end()) {
        throw std::runtime_error("Undefined symbol: " + Name);
    }
    if (It->second.Section == SectionKind::Data) {
        return TextSection.size() + It->second.Offset;
    }
    return It->second.Offset;
}

uint64_t ResolveCallTarget(const std::string& Name) {
    const uint64_t Base = ResolveAddress(Name);
    const auto It = SymbolTable.find(Name);
    if (It == SymbolTable.end()) {
        throw std::runtime_error("Undefined symbol: " + Name);
    }
    if (It->second.Section != SectionKind::Text) {
        throw std::runtime_error("CALL target must be a text symbol");
    }
    return Base | (It->second.TextCompact ? 0ULL : 1ULL);
}

void QueueImmFixup(const std::string& Symbol) {
    Fixup Entry;
    Entry.PatchOffset = ActiveBytes().size() + 8;
    Entry.Symbol = Symbol;
    Entry.Type = Fixup::Kind::AbsoluteImm;
    Fixups.push_back(Entry);
}

void QueueBranchFixup(const std::string& Symbol) {
    Fixup Entry;
    Entry.PatchOffset = ActiveBytes().size();
    Entry.Symbol = Symbol;
    Entry.Type = Fixup::Kind::BranchDisp;
    Fixups.push_back(Entry);
}

void QueueCallTargetFixup(const std::string& Symbol) {
    Fixup Entry;
    Entry.PatchOffset = ActiveBytes().size() + 8;
    Entry.Symbol = Symbol;
    Entry.Type = Fixup::Kind::CallTargetImm;
    Fixups.push_back(Entry);
}

EncodedInsn EncodeAluRRR(Opcode Op, uint8_t Rd, uint8_t Rs, uint8_t Rt,
                         int32_t ShiftDisp = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, Rd, Rs, Rt, OpmodeInt64, ShiftDisp);
    return Insn;
}

EncodedInsn EncodeAluImm(Opcode Op, uint8_t Rd, uint8_t Rs, uint64_t Imm,
                         int32_t ShiftDisp = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, Rd, Rs, 0, OpmodeInt64, ShiftDisp);
    Insn.Imm1 = Imm;
    Insn.HasImm1 = true;
    return Insn;
}

struct AluThirdOperand {
    bool IsImm = false;
    uint64_t Imm = 0;
    uint8_t Rt = 0;
    AluShiftProfile::Kind Shift = AluShiftProfile::Kind::None;
    uint32_t ShiftAmount = 0;
};

struct MemAddressOperand {
    bool AbsoluteImm = false;
    uint64_t Imm = 0;
    std::string Symbol;
    uint8_t Base = 0;
    uint8_t Index = 0;
    int64_t Offset = 0;
};

std::vector<std::string> TokenizeMemExpr(const std::string& Expr) {
    std::vector<std::string> Parts;
    std::string Cur;
    for (char Ch : Expr) {
        if (Ch == '+' || Ch == '-') {
            if (!Cur.empty()) {
                Parts.push_back(Cur);
                Cur.clear();
            }
            Parts.push_back(std::string(1, Ch));
        } else {
            Cur.push_back(Ch);
        }
    }
    if (!Cur.empty()) {
        Parts.push_back(Cur);
    }
    return Parts;
}

AluThirdOperand ParseAluThirdOperand(const std::string& Token) {
    AluThirdOperand Out;
    std::string Expr = Trim(Token);
    struct ShiftSuffix {
        const char* Text;
        AluShiftProfile::Kind Kind;
    };
    const ShiftSuffix Suffixes[] = {
        {"<<<", AluShiftProfile::Kind::Rol},
        {">>>", AluShiftProfile::Kind::Ror},
        {"<<", AluShiftProfile::Kind::Lsh},
        {">>", AluShiftProfile::Kind::Rsh},
    };
    for (const ShiftSuffix& Entry : Suffixes) {
        const size_t Pos = Expr.rfind(Entry.Text);
        if (Pos == std::string::npos) {
            continue;
        }
        const std::string Core = Trim(Expr.substr(0, Pos));
        const std::string AmtTok =
            Trim(Expr.substr(Pos + std::strlen(Entry.Text)));
        if (!IsNumeric(AmtTok)) {
            throw std::runtime_error("ALU shift amount must be an immediate");
        }
        Out.ShiftAmount = static_cast<uint32_t>(ParseImmediateToken(AmtTok));
        Out.Shift = Entry.Kind;
        Expr = Core;
        break;
    }

    if (IsNumeric(Expr)) {
        Out.IsImm = true;
        Out.Imm = ParseImmediateToken(Expr);
        return Out;
    }
    if (!TryParseRegister(Expr, Out.Rt)) {
        throw std::runtime_error("ALU third operand must be register or immediate");
    }
    return Out;
}

MemAddressOperand ParseMemAddressOperand(const std::string& BracketToken) {
    if (BracketToken.size() < 3 || BracketToken.front() != '[' ||
        BracketToken.back() != ']') {
        throw std::runtime_error("Expected bracketed memory address operand");
    }
    const std::string Inner = Trim(BracketToken.substr(1, BracketToken.size() - 2));
    MemAddressOperand Out;
    if (Inner.empty()) {
        throw std::runtime_error("Empty memory address []");
    }
    if (IsNumeric(Inner)) {
        Out.AbsoluteImm = true;
        Out.Imm = ParseImmediateToken(Inner);
        return Out;
    }

    const std::vector<std::string> Parts = TokenizeMemExpr(Inner);
    if (Parts.empty()) {
        throw std::runtime_error("Invalid memory address expression");
    }

    if (Parts.size() == 1) {
        uint8_t RegCode = 0;
        if (TryParseRegister(Parts[0], RegCode)) {
            Out.Base = RegCode;
            return Out;
        }
        Out.Symbol = Parts[0];
        return Out;
    }

    if (!TryParseRegister(Parts[0], Out.Base)) {
        throw std::runtime_error("Indexed address must start with a base register");
    }

    int64_t Offset = 0;
    bool IndexSet = false;
    for (size_t I = 1; I < Parts.size();) {
        if (Parts[I] != "+" && Parts[I] != "-") {
            throw std::runtime_error("Malformed memory address (expected + or -)");
        }
        const bool Neg = Parts[I] == "-";
        if (I + 1 >= Parts.size()) {
            throw std::runtime_error("Trailing operator in memory address");
        }
        const std::string& Term = Parts[I + 1];
        if (IsNumeric(Term)) {
            const int64_t Delta = static_cast<int64_t>(ParseImmediateToken(Term));
            Offset += Neg ? -Delta : Delta;
        } else {
            uint8_t RegCode = 0;
            if (!TryParseRegister(Term, RegCode)) {
                throw std::runtime_error("Memory index must be a register or offset");
            }
            if (IndexSet) {
                throw std::runtime_error(
                    "Only one index register supported in memory address");
            }
            Out.Index = RegCode;
            IndexSet = true;
            if (Neg) {
                throw std::runtime_error("Negative index register not supported");
            }
        }
        I += 2;
    }
    Out.Offset = Offset;
    return Out;
}

EncodedInsn EncodeOpmodeRRR(Opcode Op, uint8_t Rd, uint8_t Rs, uint8_t Rt,
                            uint8_t Opmode) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, Rd, Rs, Rt, Opmode, 0);
    return Insn;
}

EncodedInsn EncodeOpmodeImm(Opcode Op, uint8_t Rd, uint8_t Rs, uint64_t Imm,
                            uint8_t Opmode) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, Rd, Rs, 0, Opmode, 0);
    Insn.Imm1 = Imm;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeLoad(uint8_t DestReg, uint64_t Address, bool UseImm) {
    EncodedInsn Insn;
    if (UseImm) {
        Insn.Base = PackBase(Opcode::LOAD, true, false, DestReg, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = Address;
        Insn.HasImm1 = true;
    } else {
        Insn.Base = PackBase(Opcode::LOAD, false, false, DestReg, 0, 0, OpmodeInt64,
                            static_cast<int32_t>(Address));
    }
    return Insn;
}

EncodedInsn EncodeStore(uint8_t SrcReg, uint64_t Address, bool UseImm) {
    EncodedInsn Insn;
    if (UseImm) {
        Insn.Base = PackBase(Opcode::STORE, true, false, SrcReg, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = Address;
        Insn.HasImm1 = true;
    } else {
        Insn.Base = PackBase(Opcode::STORE, false, false, SrcReg, 0, 0, OpmodeInt64,
                            static_cast<int32_t>(Address));
    }
    return Insn;
}

EncodedInsn EncodeLoadAddress(uint8_t DestReg, const MemAddressOperand& Addr) {
    if (!Addr.Symbol.empty()) {
        QueueImmFixup(Addr.Symbol);
        return EncodeLoad(DestReg, 0, true);
    }
    if (Addr.AbsoluteImm) {
        return EncodeLoad(DestReg, Addr.Imm, true);
    }
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::LOAD, false, false, DestReg, Addr.Base, Addr.Index,
                         OpmodeInt64, static_cast<int32_t>(Addr.Offset));
    return Insn;
}

EncodedInsn EncodeStoreAddress(uint8_t SrcReg, const MemAddressOperand& Addr) {
    if (!Addr.Symbol.empty()) {
        QueueImmFixup(Addr.Symbol);
        return EncodeStore(SrcReg, 0, true);
    }
    if (Addr.AbsoluteImm) {
        return EncodeStore(SrcReg, Addr.Imm, true);
    }
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::STORE, false, false, SrcReg, Addr.Base, Addr.Index,
                         OpmodeInt64, static_cast<int32_t>(Addr.Offset));
    return Insn;
}

EncodedInsn EncodeBranch(Opcode Op, int32_t Disp) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64, Disp);
    return Insn;
}

EncodedInsn EncodeSystem(Opcode Op) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EncodeSystemImm(Opcode Op, uint64_t Imm) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, 0, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = Imm;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeRegOp(Opcode Op, uint8_t RegCode) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, RegCode, 0, 0, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EncodeCmpRegs(uint8_t Rs, uint8_t Rt, int32_t ShiftDisp = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::CMP, false, false, 0, Rs, Rt, OpmodeInt64, ShiftDisp);
    return Insn;
}

EncodedInsn EncodeCmpRegImm(uint8_t Rs, uint64_t Imm, int32_t ShiftDisp = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::CMP, true, false, 0, Rs, 0, OpmodeInt64, ShiftDisp);
    Insn.Imm1 = Imm;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeTwoReg(Opcode Op, uint8_t Rd, uint8_t Rs) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, Rd, Rs, 0, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EncodePushImmWord(uint64_t Value) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::PUSH, true, false, 0, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = Value;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeInterruptImm(uint8_t Vector) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::INT, true, false, 0, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = Vector;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodePerfmonReg(uint8_t Rd, uint64_t CounterSelector) {
    EncodedInsn Insn;
    Insn.Base =
        PackBase(Opcode::PERFMON, true, false, Rd, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = CounterSelector;
    Insn.HasImm1 = true;
    return Insn;
}

void Emit(const EncodedInsn& Insn) {
    AppendEncoded(ActiveBytes(), Insn);
}

EncodedInsn EncodeInsnMemImm(Opcode Op, uint8_t Rd, uint8_t Rs, uint8_t Rt,
                             int32_t Disp, const std::string& BracketToken,
                             uint8_t Opmode = OpmodeInt64) {
    if (BracketToken.size() < 3 || BracketToken.front() != '[' ||
        BracketToken.back() != ']') {
        throw std::runtime_error("Expected bracketed memory address operand");
    }
    const std::string Inner =
        BracketToken.substr(1, BracketToken.size() - 2);
    EncodedInsn Insn;
    if (IsNumeric(Inner)) {
        Insn.Base = PackBase(Op, true, false, Rd, Rs, Rt, Opmode, Disp);
        Insn.Imm1 = ParseImmediateToken(Inner);
        Insn.HasImm1 = true;
    } else {
        QueueImmFixup(Inner);
        Insn.Base = PackBase(Op, true, false, Rd, Rs, Rt, Opmode, Disp);
        Insn.Imm1 = 0;
        Insn.HasImm1 = true;
    }
    return Insn;
}

EncodedInsn EncodePrefetch(const std::string& BracketToken, uint64_t Hint) {
    EncodedInsn Insn = EncodeInsnMemImm(Opcode::PREFETCH, 0, 0, 0,
                                        static_cast<int32_t>(Hint & 0x3FFFFFFFULL),
                                        BracketToken);
    return Insn;
}

EncodedInsn EncodeIoIn(uint8_t Rd, uint64_t Port) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::IN, true, false, Rd, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = Port;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeIoOut(uint8_t Rs, uint64_t Port) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::OUT, true, false, 0, Rs, 0, OpmodeInt64, 0);
    Insn.Imm1 = Port;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EncodeBranchFar(Opcode Op, uint64_t AbsoluteTarget) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, 0, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = AbsoluteTarget;
    Insn.HasImm1 = true;
    return Insn;
}

int32_t PackDaxDisp(uint32_t Subop, uint32_t Extra24) {
    return static_cast<int32_t>(((Subop & 0x3F) << 24) | (Extra24 & 0xFFFFFF));
}

EncodedInsn EncodeDaxInsn(Opcode Op, uint8_t Kd, uint8_t Ks, uint8_t Kt, uint8_t Opmode,
                          int32_t Disp, bool Imm1Flag = false, uint64_t Imm1 = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, Imm1Flag, false, Kd, Ks, Kt, Opmode, Disp);
    if (Imm1Flag) {
        Insn.Imm1 = Imm1;
        Insn.HasImm1 = true;
    }
    return Insn;
}

bool TryParseKSlotOperand(const std::string& Token, uint8_t& Slot) {
    return TryParseKSlot(Token, Slot);
}

EncodedInsn EncodeDaxMnemonic(const std::string& Op,
                            const std::vector<std::string>& Operands) {
    if (Op.size() < 5 || Op.compare(0, 4, "dax_") != 0) {
        throw std::runtime_error("Not a DAX mnemonic: " + Op);
    }
    const std::string Stem = Op.substr(4);

    auto RequireK = [&](size_t Idx, uint8_t& Slot) {
        if (Idx >= Operands.size() || !TryParseKSlotOperand(Operands[Idx], Slot)) {
            throw std::runtime_error("DAX operand " + std::to_string(Idx) + " must be k0..k63");
        }
    };

    if (Stem == "token") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_TOKEN requires kD, <reg>|#imm");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (IsNumeric(Operands[1])) {
            return EncodeDaxInsn(Opcode::DAX_TOKEN, Kd, 0, 0, OpmodeInt64, 0, true,
                                 ParseImmediateToken(Operands[1]));
        }
        uint8_t Rs = 0;
        if (!TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("DAX_TOKEN source must be GPR or immediate");
        }
        return EncodeDaxInsn(Opcode::DAX_TOKEN, Kd, Rs, 0, OpmodeInt64, 0);
    }

    if (Stem == "consume" || Stem == "wait") {
        if (Operands.size() != 1) {
            throw std::runtime_error("DAX_" + Stem + " requires one K-slot");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        const Opcode Opc = (Stem == "consume") ? Opcode::DAX_CONSUME : Opcode::DAX_WAIT;
        return EncodeDaxInsn(Opc, Ks, 0, 0, OpmodeInt64, 0);
    }

    if (Stem == "peek" || Stem == "valid" || Stem == "refcnt" || Stem == "tag") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_" + Stem + " requires <reg>, kS");
        }
        uint8_t Rd = 0;
        uint8_t Ks = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("DAX_" + Stem + " destination must be a GPR");
        }
        RequireK(1, Ks);
        Opcode Opc = Opcode::DAX_PEEK;
        if (Stem == "valid") {
            Opc = Opcode::DAX_VALID;
        } else if (Stem == "refcnt") {
            Opc = Opcode::DAX_REFCNT;
        } else if (Stem == "tag") {
            Opc = Opcode::DAX_TAG;
        }
        return EncodeDaxInsn(Opc, Rd, Ks, 0, OpmodeInt64, 0);
    }

    if (Stem == "refill") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_REFILL requires kD, <reg>|#count");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (IsNumeric(Operands[1])) {
            return EncodeDaxInsn(Opcode::DAX_REFILL, Kd, 0, 0, OpmodeInt64, 0, true,
                                 ParseImmediateToken(Operands[1]));
        }
        uint8_t Rs = 0;
        if (!TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("DAX_REFILL count must be GPR or immediate");
        }
        return EncodeDaxInsn(Opcode::DAX_REFILL, Kd, Rs, 0, OpmodeInt64, 0);
    }

    if (Stem == "wait_all") {
        if (Operands.size() != 1 || !IsNumeric(Operands[0])) {
            throw std::runtime_error("DAX_WAIT_ALL requires #mask");
        }
        const uint32_t Mask = static_cast<uint32_t>(ParseImmediateToken(Operands[0]) & 0xFFFFFF);
        return EncodeDaxInsn(Opcode::DAX_WAIT_ALL, 0, 0, 0, OpmodeInt64,
                             PackDaxDisp(0, Mask));
    }

    if (Stem == "wait_any") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_WAIT_ANY requires <reg>, #mask");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_WAIT_ANY requires GPR and #mask");
        }
        const uint32_t Mask = static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0xFFFFFF);
        return EncodeDaxInsn(Opcode::DAX_WAIT_ANY, Rd, 0, 0, OpmodeInt64, PackDaxDisp(0, Mask));
    }

    auto EncodeTernary = [&](Opcode Opc) -> EncodedInsn {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX ternary op requires kD, kS, kT");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        uint8_t Kt = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        RequireK(2, Kt);
        return EncodeDaxInsn(Opc, Kd, Ks, Kt, OpmodeInt64, 0);
    };

    if (Stem == "add" || Stem == "sub" || Stem == "mul" || Stem == "div") {
        Opcode Opc = Opcode::DAX_ADD;
        if (Stem == "sub") {
            Opc = Opcode::DAX_SUB;
        } else if (Stem == "mul") {
            Opc = Opcode::DAX_MUL;
        } else if (Stem == "div") {
            Opc = Opcode::DAX_DIV;
        }
        return EncodeTernary(Opc);
    }

    if (Stem == "fma") {
        if (Operands.size() != 4) {
            throw std::runtime_error("DAX_FMA requires kD, kS, kT, kU");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        uint8_t Kt = 0;
        uint8_t Ku = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        RequireK(2, Kt);
        RequireK(3, Ku);
        return EncodeDaxInsn(Opcode::DAX_FMA, Kd, Ks, Kt, OpmodeInt64, PackDaxDisp(0, Ku));
    }

    if (Stem == "cmp" || Stem == "cmpi") {
        if (Stem == "cmpi") {
            if (Operands.size() != 4) {
                throw std::runtime_error("DAX_CMPI requires kD, kS, #imm, #rel");
            }
            uint8_t Kd = 0;
            uint8_t Ks = 0;
            RequireK(0, Kd);
            RequireK(1, Ks);
            const uint64_t Imm = ParseImmediateToken(Operands[2]);
            const uint32_t Rel = static_cast<uint32_t>(ParseImmediateToken(Operands[3]) & 7);
            return EncodeDaxInsn(Opcode::DAX_CMPI, Kd, Ks, 0, OpmodeInt64,
                                 PackDaxDisp(0, Rel), true, Imm);
        }
        if (Operands.size() != 4) {
            throw std::runtime_error("DAX_CMP requires kD, kS, kT, #rel");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        uint8_t Kt = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        RequireK(2, Kt);
        const uint32_t Rel = static_cast<uint32_t>(ParseImmediateToken(Operands[3]) & 7);
        return EncodeDaxInsn(Opcode::DAX_CMP, Kd, Ks, Kt, OpmodeInt64, PackDaxDisp(0, Rel));
    }

    if (Stem == "sel") {
        if (Operands.size() != 4) {
            throw std::runtime_error("DAX_SEL requires kD, kC, kS, kT");
        }
        uint8_t Kd = 0;
        uint8_t Kc = 0;
        uint8_t Ks = 0;
        uint8_t Kt = 0;
        RequireK(0, Kd);
        RequireK(1, Kc);
        RequireK(2, Ks);
        RequireK(3, Kt);
        return EncodeDaxInsn(Opcode::DAX_SEL, Kd, Kc, Ks, OpmodeInt64, PackDaxDisp(0, Kt));
    }

    if (Stem == "addi" || Stem == "muli") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX immediate op requires kD, kS, #imm");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        const Opcode Opc = (Stem == "addi") ? Opcode::DAX_ADDI : Opcode::DAX_MULI;
        return EncodeDaxInsn(Opc, Kd, Ks, 0, OpmodeInt64, 0, true,
                             ParseImmediateToken(Operands[2]));
    }

    if (Stem == "fork") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_FORK requires kD1, kD2, kS");
        }
        uint8_t Kd1 = 0;
        uint8_t Kd2 = 0;
        uint8_t Ks = 0;
        RequireK(0, Kd1);
        RequireK(1, Kd2);
        RequireK(2, Ks);
        return EncodeDaxInsn(Opcode::DAX_FORK, Kd1, Ks, Kd2, OpmodeInt64, 0);
    }

    if (Stem == "join" || Stem == "merge" || Stem == "serialize") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_" + Stem + " requires kD, kS, kT");
        }
        Opcode Opc = Opcode::DAX_JOIN;
        if (Stem == "merge") {
            Opc = Opcode::DAX_MERGE;
        } else if (Stem == "serialize") {
            Opc = Opcode::DAX_SERIALIZE;
        }
        return EncodeTernary(Opc);
    }

    if (Stem == "branch") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_BRANCH requires kC, #true_pc, #false_pc");
        }
        uint8_t Kc = 0;
        RequireK(0, Kc);
        if (!IsNumeric(Operands[1]) || !IsNumeric(Operands[2])) {
            throw std::runtime_error("DAX_BRANCH requires numeric #true_pc and #false_pc in baseline bas");
        }
        const uint32_t TruePc =
            static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0xFFFFFF);
        const uint64_t FalsePc = ParseImmediateToken(Operands[2]);
        return EncodeDaxInsn(Opcode::DAX_BRANCH, 0, Kc, 0, OpmodeInt64, PackDaxDisp(0, TruePc),
                             true, FalsePc);
    }

    if (Stem == "call") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_CALL requires kC, #target");
        }
        uint8_t Kc = 0;
        RequireK(0, Kc);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_CALL requires numeric #target in baseline bas");
        }
        const uint32_t Target =
            static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0xFFFFFF);
        return EncodeDaxInsn(Opcode::DAX_CALL, 0, Kc, 0, OpmodeInt64, PackDaxDisp(0, Target));
    }

    if (Stem == "ret") {
        if (Operands.size() != 1) {
            throw std::runtime_error("DAX_RET requires kC");
        }
        uint8_t Kc = 0;
        RequireK(0, Kc);
        return EncodeDaxInsn(Opcode::DAX_RET, 0, Kc, 0, OpmodeInt64, 0);
    }

    if (Stem == "load" || Stem == "store") {
        const Opcode Opc = (Stem == "load") ? Opcode::DAX_LOAD : Opcode::DAX_STORE;
        if (Stem == "load") {
            if (Operands.size() != 2) {
                throw std::runtime_error("DAX_LOAD requires kD, #addr|<reg>");
            }
            uint8_t Kd = 0;
            RequireK(0, Kd);
            EncodedInsn Insn = EncodeDaxInsn(Opc, Kd, 0, 0, OpmodeInt64, 0);
            if (IsNumeric(Operands[1])) {
                Insn.Imm1 = ParseImmediateToken(Operands[1]);
            } else {
                uint8_t Rs = 0;
                if (!TryParseRegister(Operands[1], Rs)) {
                    throw std::runtime_error("DAX_LOAD address must be #imm or GPR");
                }
                Insn.Base = PackBase(Opc, false, false, Kd, Rs, 0, OpmodeInt64, 0);
                return Insn;
            }
            Insn.HasImm1 = true;
            return Insn;
        }
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_STORE requires kS, #addr|<reg>");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        EncodedInsn Insn = EncodeDaxInsn(Opc, 0, Ks, 0, OpmodeInt64, 0);
        if (IsNumeric(Operands[1])) {
            Insn.Imm1 = ParseImmediateToken(Operands[1]);
            Insn.HasImm1 = true;
            return Insn;
        }
        uint8_t Rt = 0;
        if (!TryParseRegister(Operands[1], Rt)) {
            throw std::runtime_error("DAX_STORE address must be #imm or GPR");
        }
        return EncodeDaxInsn(Opc, 0, Ks, Rt, OpmodeInt64, 0);
    }

    if (Stem == "reduce") {
        if (Operands.size() != 4) {
            throw std::runtime_error("DAX_REDUCE requires kD, kS, kP, #op");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        uint8_t Kp = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        RequireK(2, Kp);
        const uint32_t Sub = static_cast<uint32_t>(ParseImmediateToken(Operands[3]) & 7);
        const uint32_t Extra = (static_cast<uint32_t>(Kp) << 6) | Sub;
        return EncodeDaxInsn(Opcode::DAX_REDUCE, Kd, Ks, 0, OpmodeInt64, PackDaxDisp(0, Extra));
    }

    auto EncodeSkbLineImm = [](uint64_t Line) -> uint64_t {
        return (static_cast<uint64_t>(PhysMap::Domain::LocalSkb) << 60) |
               ((Line & 0x7FFFFULL) << 6);
    };

    if (Stem == "promote") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_PROMOTE requires kS, #line");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_PROMOTE requires #skb_line");
        }
        return EncodeDaxInsn(Opcode::DAX_PROMOTE, 0, Ks, 0, OpmodeInt64, 0, true,
                             EncodeSkbLineImm(ParseImmediateToken(Operands[1])));
    }

    if (Stem == "demote") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_DEMOTE requires kD, #line");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_DEMOTE requires #skb_line");
        }
        return EncodeDaxInsn(Opcode::DAX_DEMOTE, Kd, 0, 0, OpmodeInt64, 0, true,
                             EncodeSkbLineImm(ParseImmediateToken(Operands[1])));
    }

    if (Stem == "atomic") {
        if (Operands.size() < 3) {
            throw std::runtime_error("DAX_ATOMIC requires kD, #addr, #op [, <reg>|#imm]");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1]) || !IsNumeric(Operands[2])) {
            throw std::runtime_error("DAX_ATOMIC requires numeric #addr and #op");
        }
        const uint32_t Sub = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 7);
        EncodedInsn Insn =
            EncodeDaxInsn(Opcode::DAX_ATOMIC, Kd, 0, 0, OpmodeInt64, PackDaxDisp(0, Sub), true,
                          ParseImmediateToken(Operands[1]));
        if (Operands.size() >= 4) {
            if (IsNumeric(Operands[3])) {
                Insn.Imm2 = ParseImmediateToken(Operands[3]);
                Insn.HasImm2 = true;
                Insn.Base = PackBase(Opcode::DAX_ATOMIC, true, true, Kd, 0, 0, OpmodeInt64,
                                     PackDaxDisp(0, Sub));
            } else {
                uint8_t Rs = 0;
                if (!TryParseRegister(Operands[3], Rs)) {
                    throw std::runtime_error("DAX_ATOMIC operand must be GPR or immediate");
                }
                Insn.Base = PackBase(Opcode::DAX_ATOMIC, true, false, Kd, Rs, 0, OpmodeInt64,
                                     PackDaxDisp(0, Sub));
            }
        }
        return Insn;
    }

    if (Stem == "stream") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_STREAM requires kD, #line, #len");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1]) || !IsNumeric(Operands[2])) {
            throw std::runtime_error("DAX_STREAM requires #line and #len");
        }
        const uint32_t Len = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 0xFFFFFF);
        const uint64_t LineImm =
            EncodeSkbLineImm(static_cast<uint64_t>(ParseImmediateToken(Operands[1])));
        return EncodeDaxInsn(Opcode::DAX_STREAM, Kd, 0, 0, OpmodeInt64, PackDaxDisp(0, Len),
                             true, LineImm);
    }

    if (Stem == "gather") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_GATHER requires kD, #line, kI");
        }
        uint8_t Kd = 0;
        uint8_t Ki = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_GATHER requires #skb_line");
        }
        RequireK(2, Ki);
        return EncodeDaxInsn(Opcode::DAX_GATHER, Kd, 0, Ki, OpmodeInt64, 0, true,
                             EncodeSkbLineImm(ParseImmediateToken(Operands[1])));
    }

    if (Stem == "vadd" || Stem == "vmul" || Stem == "vfma") {
        Opcode Opc = Opcode::DAX_VADD;
        if (Stem == "vmul") {
            Opc = Opcode::DAX_VMUL;
        } else if (Stem == "vfma") {
            Opc = Opcode::DAX_VFMA;
        }
        if (Stem == "vfma") {
            if (Operands.size() != 4) {
                throw std::runtime_error("DAX_VFMA requires kD, kS, kT, kU");
            }
            uint8_t Kd = 0;
            uint8_t Ks = 0;
            uint8_t Kt = 0;
            uint8_t Ku = 0;
            RequireK(0, Kd);
            RequireK(1, Ks);
            RequireK(2, Kt);
            RequireK(3, Ku);
            return EncodeDaxInsn(Opc, Kd, Ks, Kt, OpmodeSsxFp32, PackDaxDisp(0, Ku));
        }
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX vector op requires kD, kS, kT");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        uint8_t Kt = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        RequireK(2, Kt);
        return EncodeDaxInsn(Opc, Kd, Ks, Kt, OpmodeSsxFp32, 0);
    }

    if (Stem == "vload") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_VLOAD requires kD, #addr|<reg>");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        EncodedInsn Insn = EncodeDaxInsn(Opcode::DAX_VLOAD, Kd, 0, 0, OpmodeSsxFp32, 0);
        if (IsNumeric(Operands[1])) {
            Insn.Imm1 = ParseImmediateToken(Operands[1]);
            Insn.HasImm1 = true;
            return Insn;
        }
        uint8_t Rs = 0;
        if (!TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("DAX_VLOAD address must be #imm or GPR");
        }
        Insn.Base = PackBase(Opcode::DAX_VLOAD, false, false, Kd, Rs, 0, OpmodeSsxFp32, 0);
        return Insn;
    }

    if (Stem == "vstore") {
        if (Operands.size() != 2) {
            throw std::runtime_error("DAX_VSTORE requires kS, #addr|<reg>");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        EncodedInsn Insn = EncodeDaxInsn(Opcode::DAX_VSTORE, 0, Ks, 0, OpmodeSsxFp32, 0);
        if (IsNumeric(Operands[1])) {
            Insn.Imm1 = ParseImmediateToken(Operands[1]);
            Insn.HasImm1 = true;
            return Insn;
        }
        uint8_t Rt = 0;
        if (!TryParseRegister(Operands[1], Rt)) {
            throw std::runtime_error("DAX_VSTORE address must be #imm or GPR");
        }
        return EncodeDaxInsn(Opcode::DAX_VSTORE, 0, Ks, Rt, OpmodeSsxFp32, 0);
    }

    if (Stem == "loop") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_LOOP requires kCount, kInit, kBody");
        }
        uint8_t Kcount = 0;
        uint8_t Kinit = 0;
        uint8_t Kbody = 0;
        RequireK(0, Kcount);
        RequireK(1, Kinit);
        RequireK(2, Kbody);
        return EncodeDaxInsn(Opcode::DAX_LOOP, 0, Kcount, Kinit, OpmodeInt64, PackDaxDisp(0, Kbody));
    }

    if (Stem == "matmul") {
        if (Operands.size() < 2) {
            throw std::runtime_error("DAX_MATMUL requires kD, #desc_addr");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_MATMUL requires numeric descriptor address");
        }
        return EncodeDaxInsn(Opcode::DAX_MATMUL, Kd, 0, 0, OpmodeInt64, 0, true,
                             ParseImmediateToken(Operands[1]));
    }

    if (Stem == "conv") {
        if (Operands.size() < 2) {
            throw std::runtime_error("DAX_CONV requires kD, #desc_addr");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("DAX_CONV requires numeric descriptor address");
        }
        return EncodeDaxInsn(Opcode::DAX_CONV, Kd, 0, 0, OpmodeInt64, 0, true,
                             ParseImmediateToken(Operands[1]));
    }

    if (Stem == "scan") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_SCAN requires kD, kS, #op");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        const uint32_t Sub = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 7);
        return EncodeDaxInsn(Opcode::DAX_SCAN, Kd, Ks, 0, OpmodeSsxFp32, PackDaxDisp(0, Sub));
    }

    if (Stem == "send") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_SEND requires kS, #core, #slot");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        const uint32_t Core = static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0x3F);
        const uint32_t Slot = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 0x3F);
        const uint32_t Extra = (Core << 6) | Slot;
        return EncodeDaxInsn(Opcode::DAX_SEND, 0, Ks, 0, OpmodeInt64, PackDaxDisp(0, Extra));
    }

    if (Stem == "recv") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_RECV requires kD, #core, #slot");
        }
        uint8_t Kd = 0;
        RequireK(0, Kd);
        const uint32_t Core = static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0x3F);
        const uint32_t Slot = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 0x3F);
        const uint32_t Extra = (Core << 6) | Slot;
        return EncodeDaxInsn(Opcode::DAX_RECV, Kd, 0, 0, OpmodeInt64, PackDaxDisp(0, Extra));
    }

    if (Stem == "bcast") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DAX_BCAST requires kS, #slot, #mask");
        }
        uint8_t Ks = 0;
        RequireK(0, Ks);
        const uint32_t Slot = static_cast<uint32_t>(ParseImmediateToken(Operands[1]) & 0x3F);
        const uint32_t Mask = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 0xFFFFFF);
        return EncodeDaxInsn(Opcode::DAX_BCAST, 0, Ks, static_cast<uint8_t>(Slot), OpmodeInt64,
                             PackDaxDisp(0, Mask));
    }

    if (Stem == "reduce_scatter") {
        if (Operands.size() != 5) {
            throw std::runtime_error("DAX_REDUCE_SCATTER requires kD, kS, #slot, #op, #mask");
        }
        uint8_t Kd = 0;
        uint8_t Ks = 0;
        RequireK(0, Kd);
        RequireK(1, Ks);
        const uint32_t Slot = static_cast<uint32_t>(ParseImmediateToken(Operands[2]) & 0x3F);
        const uint32_t Sub = static_cast<uint32_t>(ParseImmediateToken(Operands[3]) & 7);
        const uint32_t Mask = static_cast<uint32_t>(ParseImmediateToken(Operands[4]) & 0x1FFFFFU);
        const uint32_t Extra = (Mask << 3) | Sub;
        return EncodeDaxInsn(Opcode::DAX_REDUCE_SCATTER, Kd, Ks, static_cast<uint8_t>(Slot),
                             OpmodeInt64, PackDaxDisp(0, Extra));
    }

    throw std::runtime_error("Unknown or unsupported DAX mnemonic: " + Op);
}

static bool IsNsxMnemonic(const std::string& Op) {
    static const char* const Names[] = {
        "softmax", "layernorm", "batchnorm", "dropout", "relu", "gelu", "swish", "silu",
        "mse_loss", "cross_entropy", "l2_loss",
        "tanh", "sigmoid", "conv1d", "nconv2d", "xconv2d", "conv3d", "dwconv2d",
        "sepconv2d", "pool_max", "pool_avg", "pool_global", "upsample", "attention",
        "cross_attn", "self_attn", "flash_attn", "paged_attn", "transformer", "rnn_cell",
        "lstm_cell", "gru_cell", "rnn_seq", "lstm_seq", "gru_seq", "bidir_lstm",
        "fwd_start", "fwd_end", "fwd_layer", "bwd_start", "bwd_all", "bwd_grad", "ckpt",
        "ckpt_restore", "ckpt_discard", "sgd", "adam", "lamb", "laplacian_2d",
        "laplacian_3d", "hessian_2d", "hessian_3d", "newton_step",
    };
    for (const char* Name : Names) {
        if (Op == Name) {
            return true;
        }
    }
    return false;
}

EncodedInsn EncodeNsxMnemonic(const std::string& Op,
                              const std::vector<std::string>& Operands) {
    auto EncodeNsxRR = [&](Opcode NsxOp) -> EncodedInsn {
        if (Operands.size() != 2) {
            throw std::runtime_error("NSX op requires <dest>, <src>");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseNsxRegister(Operands[0], Rd) ||
            !TryParseNsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("NSX activation requires SSX/K/GPR operands");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(NsxOp, false, false, Rd, Rs, 0, NsxOpmodeFp32, 0);
        return Insn;
    };
    auto EncodeNsxDesc = [&](Opcode NsxOp) -> EncodedInsn {
        if (Operands.size() != 2) {
            throw std::runtime_error("NSX descriptor op requires <dest>, <descriptor>");
        }
        uint8_t Rd = 0;
        if (!TryParseNsxRegister(Operands[0], Rd)) {
            throw std::runtime_error("NSX descriptor op requires destination register");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(NsxOp, true, false, Rd, 0, 0, NsxOpmodeFp32, 0);
        if (IsNumeric(Operands[1])) {
            Insn.Imm1 = ParseImmediateToken(Operands[1]);
        } else {
            QueueImmFixup(Operands[1]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    };

    if (Op == "mse_loss") return EncodeNsxDesc(Opcode::MSE_LOSS);
    if (Op == "cross_entropy") return EncodeNsxDesc(Opcode::CROSS_ENTROPY);
    if (Op == "l2_loss") return EncodeNsxDesc(Opcode::L2_LOSS);
    if (Op == "softmax") return EncodeNsxRR(Opcode::SOFTMAX);
    if (Op == "relu") return EncodeNsxRR(Opcode::NSX_RELU);
    if (Op == "gelu") return EncodeNsxRR(Opcode::GELU);
    if (Op == "silu") return EncodeNsxRR(Opcode::SILU);
    if (Op == "tanh") return EncodeNsxRR(Opcode::NSX_TANH);
    if (Op == "sigmoid") return EncodeNsxRR(Opcode::SIGMOID);
    if (Op == "swish") {
        if (Operands.size() == 2) {
            return EncodeNsxRR(Opcode::SWISH);
        }
        if (Operands.size() == 3) {
            uint8_t Rd = 0, Rs = 0;
            if (!TryParseNsxRegister(Operands[0], Rd) ||
                !TryParseNsxRegister(Operands[1], Rs)) {
                throw std::runtime_error("SWISH requires dest and src registers");
            }
            const uint32_t BetaBits = static_cast<uint32_t>(ParseImmediateToken(Operands[2]));
            EncodedInsn Insn;
            Insn.Base = PackBase(Opcode::SWISH, false, false, Rd, Rs, 0, NsxOpmodeFp32,
                                 static_cast<int32_t>(BetaBits));
            return Insn;
        }
        throw std::runtime_error("SWISH requires <dest>, <src> or <dest>, <src>, #beta");
    }
    if (Op == "dropout") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DROPOUT requires <dest>, <src>, #rate");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseNsxRegister(Operands[0], Rd) ||
            !TryParseNsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("DROPOUT requires dest and src registers");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::DROPOUT, true, false, Rd, Rs, 0, NsxOpmodeFp32, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[2]);
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "layernorm" || Op == "batchnorm") {
        if (Operands.size() != 4) {
            throw std::runtime_error("LAYERNORM/BATCHNORM require <dest>, <src>, gamma, beta");
        }
        uint8_t Rd = 0, Rs = 0, Rt = 0;
        if (!TryParseNsxRegister(Operands[0], Rd) ||
            !TryParseNsxRegister(Operands[1], Rs) ||
            !TryParseNsxRegister(Operands[2], Rt)) {
            throw std::runtime_error("LAYERNORM/BATCHNORM register parse failed");
        }
        const Opcode Kop = (Op == "layernorm") ? Opcode::LAYERNORM : Opcode::BATCHNORM;
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, Rd, Rs, Rt, NsxOpmodeFp32, 0);
        if (IsNumeric(Operands[3])) {
            Insn.Imm1 = ParseImmediateToken(Operands[3]);
        } else {
            QueueImmFixup(Operands[3]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "nconv2d" || Op == "xconv2d") {
        return EncodeNsxDesc(Opcode::NSX_CONV2D);
    }
    if (Op == "conv1d") return EncodeNsxDesc(Opcode::CONV1D);
    if (Op == "conv3d") return EncodeNsxDesc(Opcode::CONV3D);
    if (Op == "dwconv2d") return EncodeNsxDesc(Opcode::DWCONV2D);
    if (Op == "sepconv2d") return EncodeNsxDesc(Opcode::SEPCONV2D);
    if (Op == "pool_max") return EncodeNsxDesc(Opcode::POOL_MAX);
    if (Op == "pool_avg") return EncodeNsxDesc(Opcode::POOL_AVG);
    if (Op == "pool_global") return EncodeNsxRR(Opcode::POOL_GLOBAL);
    if (Op == "upsample") return EncodeNsxDesc(Opcode::UPSAMPLE);
    if (Op == "attention") return EncodeNsxDesc(Opcode::ATTENTION);
    if (Op == "cross_attn") return EncodeNsxDesc(Opcode::CROSS_ATTN);
    if (Op == "self_attn") return EncodeNsxDesc(Opcode::SELF_ATTN);
    if (Op == "flash_attn") return EncodeNsxDesc(Opcode::FLASH_ATTN);
    if (Op == "paged_attn") return EncodeNsxDesc(Opcode::PAGED_ATTN);
    if (Op == "transformer") return EncodeNsxDesc(Opcode::TRANSFORMER);
    if (Op == "lstm_cell") return EncodeNsxDesc(Opcode::LSTM_CELL);
    if (Op == "gru_cell") return EncodeNsxDesc(Opcode::GRU_CELL);
    if (Op == "rnn_cell") return EncodeNsxDesc(Opcode::RNN_CELL);
    if (Op == "rnn_seq") return EncodeNsxDesc(Opcode::RNN_SEQ);
    if (Op == "lstm_seq") return EncodeNsxDesc(Opcode::LSTM_SEQ);
    if (Op == "gru_seq") return EncodeNsxDesc(Opcode::GRU_SEQ);
    if (Op == "bidir_lstm") return EncodeNsxDesc(Opcode::BIDIR_LSTM);
    if (Op == "fwd_layer") return EncodeNsxDesc(Opcode::FWD_LAYER);
    if (Op == "bwd_grad") return EncodeNsxDesc(Opcode::BWD_GRAD);
    if (Op == "sgd" || Op == "adam" || Op == "lamb") {
        return EncodeNsxDesc(Op == "sgd"   ? Opcode::SGD
                             : Op == "adam" ? Opcode::ADAM
                                            : Opcode::LAMB);
    }
    if (Op == "laplacian_2d" || Op == "laplacian_3d" || Op == "hessian_2d" ||
        Op == "hessian_3d" || Op == "newton_step") {
        if (Operands.size() < 2) {
            throw std::runtime_error("NSX scientific op requires at least <dest>, <src>");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseNsxRegister(Operands[0], Rd) ||
            !TryParseNsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("NSX scientific op register parse failed");
        }
        int32_t Disp = 0;
        if (Operands.size() >= 3) {
            Disp = static_cast<int32_t>(ParseImmediateToken(Operands[2]));
        }
        Opcode Kop = Opcode::LAPLACIAN_2D;
        if (Op == "laplacian_3d") Kop = Opcode::LAPLACIAN_3D;
        if (Op == "hessian_2d") Kop = Opcode::HESSIAN_2D;
        if (Op == "hessian_3d") Kop = Opcode::HESSIAN_3D;
        if (Op == "newton_step") Kop = Opcode::NEWTON_STEP;
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, Rd, Rs, 0, NsxOpmodeFp32, Disp);
        return Insn;
    }
    auto EncodeNsxImmOnly = [&](Opcode NsxOp) -> EncodedInsn {
        if (Operands.size() != 1) {
            throw std::runtime_error("NSX op requires one immediate operand");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(NsxOp, true, false, 0, 0, 0, NsxOpmodeFp32, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[0]);
        Insn.HasImm1 = true;
        return Insn;
    };
    auto EncodeNsxNullary = [&](Opcode NsxOp) -> EncodedInsn {
        EncodedInsn Insn;
        Insn.Base = PackBase(NsxOp, false, false, 0, 0, 0, NsxOpmodeFp32, 0);
        return Insn;
    };
    if (Op == "fwd_start" || Op == "bwd_start" || Op == "ckpt" || Op == "ckpt_restore") {
        if (Operands.size() != 1) {
            throw std::runtime_error("NSX tape op requires one descriptor operand");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Op == "fwd_start"   ? Opcode::FWD_START
                             : Op == "bwd_start"   ? Opcode::BWD_START
                             : Op == "ckpt_restore" ? Opcode::CKPT_RESTORE
                                                    : Opcode::CKPT,
                             true, false, 0, 0, 0, NsxOpmodeFp32, 0);
        if (IsNumeric(Operands[0])) {
            Insn.Imm1 = ParseImmediateToken(Operands[0]);
        } else {
            QueueImmFixup(Operands[0]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "fwd_end") return EncodeNsxNullary(Opcode::FWD_END);
    if (Op == "bwd_all") return EncodeNsxNullary(Opcode::BWD_ALL);
    if (Op == "ckpt_discard") return EncodeNsxImmOnly(Opcode::CKPT_DISCARD);

    throw std::runtime_error("Unknown NSX mnemonic: " + Op);
}

static bool IsSecurityMnemonic(const std::string& Op) {
    static const char* const Names[] = {
        "pacia", "pacda", "pacib", "autia", "autda", "xpaci", "pac_key_init",
        "cfi_label", "cfi_call", "cfi_jump", "ssp_push", "ssp_pop", "ssp_check",
        "sandbox_allow", "sandbox_deny", "sandbox_reset",
        "enc_load", "enc_store", "enc_genkey", "enc_switch", "measure",
        "vmlaunch", "vmresume", "vmexit", "vmread", "vmwrite",
    };
    for (const char* Name : Names) {
        if (Op == Name) {
            return true;
        }
    }
    return false;
}

static EncodedInsn EncodeSecurityMnemonic(const std::string& Op,
                                          const std::vector<std::string>& Operands) {
    auto EncodeSecRR = [&](Opcode Kop) -> EncodedInsn {
        if (Operands.size() != 2) {
            throw std::runtime_error("Security op requires <dest>, <src>");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("Security RR operands must be registers");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, Rd, Rs, 0, OpmodeInt64, 0);
        return Insn;
    };
    auto EncodeSecR = [&](Opcode Kop) -> EncodedInsn {
        if (Operands.size() != 1) {
            throw std::runtime_error("Security op requires one register operand");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("Security R operand must be a register");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, Rd, 0, 0, OpmodeInt64, 0);
        return Insn;
    };
    auto EncodeSecImm = [&](Opcode Kop) -> EncodedInsn {
        if (Operands.size() != 1) {
            throw std::runtime_error("Security op requires one immediate operand");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[0]);
        Insn.HasImm1 = true;
        return Insn;
    };
    auto EncodeSecNull = [&](Opcode Kop) -> EncodedInsn {
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, 0, 0, 0, OpmodeInt64, 0);
        return Insn;
    };

    if (Op == "pacia" || Op == "pacda" || Op == "pacib") {
        Opcode Kop = Opcode::PACIA;
        if (Op == "pacda") Kop = Opcode::PACDA;
        if (Op == "pacib") Kop = Opcode::PACIB;
        return EncodeSecRR(Kop);
    }
    if (Op == "autia" || Op == "autda") {
        return EncodeSecRR(Op == "autia" ? Opcode::AUTIA : Opcode::AUTDA);
    }
    if (Op == "xpaci") return EncodeSecR(Opcode::XPACI);
    if (Op == "pac_key_init") return EncodeSecNull(Opcode::PAC_KEY_INIT);
    if (Op == "cfi_label") return EncodeSecImm(Opcode::CFI_LABEL);
    if (Op == "cfi_call" || Op == "cfi_jump") {
        return EncodeSecR(Op == "cfi_call" ? Opcode::CFI_CALL : Opcode::CFI_JUMP);
    }
    if (Op == "ssp_push") return EncodeSecRR(Opcode::SSP_PUSH);
    if (Op == "ssp_pop") return EncodeSecR(Opcode::SSP_POP);
    if (Op == "ssp_check") return EncodeSecNull(Opcode::SSP_CHECK);
    if (Op == "sandbox_allow") return EncodeSecImm(Opcode::SANDBOX_ALLOW);
    if (Op == "sandbox_deny") return EncodeSecImm(Opcode::SANDBOX_DENY);
    if (Op == "sandbox_reset") return EncodeSecNull(Opcode::SANDBOX_RESET);
    if (Op == "enc_genkey") return EncodeSecNull(Opcode::ENC_GENKEY);
    if (Op == "enc_switch") return EncodeSecImm(Opcode::ENC_SWITCH);
    if (Op == "vmlaunch") return EncodeSecNull(Opcode::VMLAUNCH);
    if (Op == "vmresume") return EncodeSecNull(Opcode::VMRESUME);
    if (Op == "vmexit") return EncodeSecNull(Opcode::VMEXIT);
    if (Op == "vmread" || Op == "vmwrite") {
        if (Operands.size() != 2) {
            throw std::runtime_error("VMREAD/VMWRITE require <reg>, #field");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("VMREAD/VMWRITE require register operand");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Op == "vmread" ? Opcode::VMREAD : Opcode::VMWRITE, true, false,
                             Rd, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[1]);
        Insn.HasImm1 = true;
        return Insn;
    }
    throw std::runtime_error("Unknown security mnemonic: " + Op);
}

EncodedInsn EncodeMnemonic(const std::string& Mnemonic,
                           const std::vector<std::string>& Operands) {
    std::string Op = ToLower(Mnemonic);

    if (Op.rfind("dax_", 0) == 0) {
        return EncodeDaxMnemonic(Op, Operands);
    }
    if (IsSecurityMnemonic(Op)) {
        return EncodeSecurityMnemonic(Op, Operands);
    }
    if (IsNsxMnemonic(Op)) {
        return EncodeNsxMnemonic(Op, Operands);
    }

    if (Op == "nop") return EncodeSystem(Opcode::NOP);
    if (Op == "wait") return EncodeSystem(Opcode::WAIT);
    if (Op == "hlt") return EncodeSystem(Opcode::HLT);
    if (Op == "ret") return EncodeSystem(Opcode::RET);
    if (Op == "ei") return EncodeSystem(Opcode::EI);
    if (Op == "di") return EncodeSystem(Opcode::DI);
    if (Op == "iret") return EncodeSystem(Opcode::IRET);

    if (Op == "lea") {
        if (Operands.size() != 2) {
            throw std::runtime_error("LEA requires <reg>, <symbol|immediate>");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("LEA destination must be a register");
        }
        const std::string& Src = Operands[1];
        if (Src.size() >= 3 && Src.front() == '[' && Src.back() == ']') {
            throw std::runtime_error(
                "LEA does not accept bracketed operands; use a bare symbol or immediate");
        }
        if (IsNumeric(Src)) {
            return EncodeAluImm(Opcode::ADD, Rd, 0, ParseImmediateToken(Src));
        }
        QueueImmFixup(Src);
        return EncodeAluImm(Opcode::ADD, Rd, 0, 0);
    }

    if (Op == "syscall") {
        if (Operands.size() != 1) {
            throw std::runtime_error("SYSCALL requires one immediate syscall number");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::SYSCALL, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[0]);
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "sysret") {
        if (!Operands.empty()) {
            throw std::runtime_error("SYSRET takes no operands");
        }
        return EncodeSystem(Opcode::SYSRET);
    }

    if (Op == "int") {
        if (Operands.size() != 1) {
            throw std::runtime_error("INT requires one immediate operand");
        }
        const uint64_t Imm = ParseImmediateToken(Operands[0]);
        if (Imm > 255) {
            throw std::runtime_error("INT vector must fit in 8 bits");
        }
        return EncodeInterruptImm(static_cast<uint8_t>(Imm));
    }

    if (Op == "ep") {
        if (!Operands.empty()) {
            throw std::runtime_error("EP takes no operands");
        }
        return EncodeSystem(Opcode::EnablePaging);
    }
    if (Op == "dp") {
        if (!Operands.empty()) {
            throw std::runtime_error("DP takes no operands");
        }
        return EncodeSystem(Opcode::DisablePaging);
    }
    if (Op == "eu") {
        if (!Operands.empty()) {
            throw std::runtime_error("EU takes no operands");
        }
        return EncodeSystem(Opcode::EnableUser);
    }
    if (Op == "du") {
        if (!Operands.empty()) {
            throw std::runtime_error("DU takes no operands");
        }
        return EncodeSystem(Opcode::DisableUser);
    }
    if (Op == "pause") {
        if (!Operands.empty()) {
            throw std::runtime_error("PAUSE takes no operands");
        }
        return EncodeSystem(Opcode::Pause);
    }
    if (Op == "esr") {
        if (!Operands.empty()) {
            throw std::runtime_error("ESR takes no operands");
        }
        return EncodeSystem(Opcode::EnableSoftwareTlbRefill);
    }
    if (Op == "dsr") {
        if (!Operands.empty()) {
            throw std::runtime_error("DSR takes no operands");
        }
        return EncodeSystem(Opcode::DisableSoftwareTlbRefill);
    }
    if (Op == "tlbinsert") {
        if (!Operands.empty()) {
            throw std::runtime_error("TlbInsert takes no operands");
        }
        return EncodeSystem(Opcode::TlbInsert);
    }
    if (Op == "wiretlbentry") {
        if (Operands.size() != 1) {
            throw std::runtime_error("WireTlbEntry requires #slot immediate");
        }
        const uint64_t Slot = ParseImmediateToken(Operands[0]);
        if (Slot >= 64) {
            throw std::runtime_error("WireTlbEntry slot must be 0..63");
        }
        return EncodeSystemImm(Opcode::WireTlbEntry, Slot);
    }

    if (Op == "perfmon") {
        uint8_t Rd = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("PERFMON requires <reg>, <counter_id immediate>");
        }
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("PERFMON counter selector must be an immediate");
        }
        return EncodePerfmonReg(Rd, ParseImmediateToken(Operands[1]));
    }

    Opcode AluOp = Opcode::NOP;
    if (Op == "add") AluOp = Opcode::ADD;
    else if (Op == "sub") AluOp = Opcode::SUB;
    else if (Op == "mul") AluOp = Opcode::MUL;
    else if (Op == "div") AluOp = Opcode::DIV;
    else if (Op == "mod") AluOp = Opcode::MOD;
    else if (Op == "and") AluOp = Opcode::AND;
    else if (Op == "or") AluOp = Opcode::OR;
    else if (Op == "xor") AluOp = Opcode::XOR;
    else if (Op == "not") AluOp = Opcode::NOT;
    else if (Op == "rsh") AluOp = Opcode::RSH;
    else if (Op == "lsh") AluOp = Opcode::LSH;
    else if (Op == "ror") AluOp = Opcode::ROR;
    else if (Op == "rol") AluOp = Opcode::ROL;

    if (AluOp != Opcode::NOP) {
        if (Operands.size() != 3) {
            throw std::runtime_error("ALU instruction requires three operands");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("Invalid register in ALU instruction");
        }
        const AluThirdOperand Third = ParseAluThirdOperand(Operands[2]);
        const int32_t ShiftDisp =
            AluShiftProfile::PackDisp(Third.Shift, Third.ShiftAmount);
        if (Third.IsImm) {
            return EncodeAluImm(AluOp, Rd, Rs, Third.Imm, ShiftDisp);
        }
        return EncodeAluRRR(AluOp, Rd, Rs, Third.Rt, ShiftDisp);
    }

    Opcode SatOp = Opcode::NOP;
    if (Op == "satadd") SatOp = Opcode::SATADD;
    else if (Op == "satmul") SatOp = Opcode::SATMUL;

    if (SatOp != Opcode::NOP) {
        if (Operands.size() != 3) {
            throw std::runtime_error("SATADD/SATMUL requires three operands");
        }
        uint8_t Rd = 0, Rs = 0, Rt = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("Invalid register in saturated ALU instruction");
        }
        if (TryParseRegister(Operands[2], Rt)) {
            return EncodeAluRRR(SatOp, Rd, Rs, Rt);
        }
        return EncodeAluImm(SatOp, Rd, Rs, ParseImmediateToken(Operands[2]));
    }

    if (Op == "cmp") {
        uint8_t Rs = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rs)) {
            throw std::runtime_error("CMP requires a register as the first operand");
        }
        const AluThirdOperand Second = ParseAluThirdOperand(Operands[1]);
        const int32_t ShiftDisp =
            AluShiftProfile::PackDisp(Second.Shift, Second.ShiftAmount);
        if (Second.IsImm) {
            return EncodeCmpRegImm(Rs, Second.Imm, ShiftDisp);
        }
        return EncodeCmpRegs(Rs, Second.Rt, ShiftDisp);
    }

    if (Op == "load") {
        uint8_t Dest = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Dest)) {
            throw std::runtime_error("LOAD requires register destination");
        }
        const std::string& Src = Operands[1];
        if (Src.size() >= 3 && Src.front() == '[' && Src.back() == ']') {
            return EncodeLoadAddress(Dest, ParseMemAddressOperand(Src));
        }
        if (IsNumeric(Src)) {
            return EncodeAluImm(Opcode::ADD, Dest, static_cast<uint8_t>(Reg::ZERO),
                                ParseImmediateToken(Src));
        }
        MemAddressOperand Sym;
        Sym.Symbol = Src;
        return EncodeLoadAddress(Dest, Sym);
    }

    if (Op == "store") {
        uint8_t Src = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Src)) {
            throw std::runtime_error("STORE requires register source");
        }
        const std::string& Dst = Operands[1];
        if (Dst.size() >= 3 && Dst.front() == '[' && Dst.back() == ']') {
            return EncodeStoreAddress(Src, ParseMemAddressOperand(Dst));
        }
        if (IsNumeric(Dst)) {
            return EncodeStore(Src, ParseImmediateToken(Dst), true);
        }
        MemAddressOperand Sym;
        Sym.Symbol = Dst;
        return EncodeStoreAddress(Src, Sym);
    }

    if (Op == "prefetch") {
        if (Operands.size() != 2 || !IsNumeric(Operands[1])) {
            throw std::runtime_error(
                "PREFETCH requires [<addr>], <hint immediate>");
        }
        const uint64_t Hint = ParseImmediateToken(Operands[1]);
        return EncodePrefetch(Operands[0], Hint);
    }

    if (Op == "in") {
        uint8_t Rd = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("IN requires <dest reg>, <port immediate>");
        }
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("IN port must be immediate");
        }
        return EncodeIoIn(Rd, ParseImmediateToken(Operands[1]));
    }

    if (Op == "out") {
        uint8_t Rs = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rs)) {
            throw std::runtime_error("OUT requires <src reg>, <port immediate>");
        }
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error("OUT port must be immediate");
        }
        return EncodeIoOut(Rs, ParseImmediateToken(Operands[1]));
    }

    if (Op == "inc" || Op == "dec") {
        uint8_t RegCode = 0;
        if (Operands.size() != 1 || !TryParseRegister(Operands[0], RegCode)) {
            throw std::runtime_error("INC/DEC requires one register");
        }
        return EncodeRegOp(Op == "inc" ? Opcode::INC : Opcode::DEC, RegCode);
    }

    if (Op == "push" || Op == "pop") {
        if (Op == "pop") {
            uint8_t RegCode = 0;
            if (Operands.size() != 1 || !TryParseRegister(Operands[0], RegCode)) {
                throw std::runtime_error("POP requires one register");
            }
            return EncodeRegOp(Opcode::POP, RegCode);
        }
        uint8_t RegCode = 0;
        if (Operands.size() != 1) {
            throw std::runtime_error("PUSH requires one operand (register or immediate)");
        }
        if (TryParseRegister(Operands[0], RegCode)) {
            return EncodeRegOp(Opcode::PUSH, RegCode);
        }
        if (IsNumeric(Operands[0])) {
            return EncodePushImmWord(ParseImmediateToken(Operands[0]));
        }
        throw std::runtime_error("PUSH operand must be a register or immediate");
    }

    if (Op == "swap" || Op == "copy" || Op == "mov") {
        uint8_t Rd = 0, Rs = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rd) ||
            !TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("SWAP/COPY/MOV require two registers");
        }
        const Opcode PairOpc = (Op == "swap") ? Opcode::SWAP : Opcode::COPY;
        return EncodeTwoReg(PairOpc, Rd, Rs);
    }

    if (Op == "atand" || Op == "ator" || Op == "atxor" || Op == "atadd" ||
        Op == "atsub" || Op == "atmul" || Op == "atdiv" || Op == "atfadd" ||
        Op == "atfsub" || Op == "atswap" || Op == "atload" || Op == "atstore") {
        Opcode At = Opcode::NOP;
        if (Op == "atand") At = Opcode::ATAND;
        else if (Op == "ator") At = Opcode::ATOR;
        else if (Op == "atxor") At = Opcode::ATXOR;
        else if (Op == "atadd") At = Opcode::ATADD;
        else if (Op == "atsub") At = Opcode::ATSUB;
        else if (Op == "atmul") At = Opcode::ATMUL;
        else if (Op == "atdiv") At = Opcode::ATDIV;
        else if (Op == "atfadd") At = Opcode::ATFADD;
        else if (Op == "atfsub") At = Opcode::ATFSUB;
        else if (Op == "atswap") At = Opcode::ATSWAP;
        else if (Op == "atload") At = Opcode::ATLOAD;
        else if (Op == "atstore") At = Opcode::ATSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error(
                "Atomic RMW requires <reg>, [<addr>] except unary forms");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("Atomic memory op needs GPR in first operand");
        }
        return EncodeInsnMemImm(At, Rd, 0, 0, 0, Operands[1]);
    }

    if (Op == "atnot" || Op == "atinc" || Op == "atdec") {
        Opcode At = Op == "atnot" ? Opcode::ATNOT : (Op == "atinc" ? Opcode::ATINC : Opcode::ATDEC);
        if (Operands.size() != 1) {
            throw std::runtime_error(
                "Unary atomic memory op requires one [<addr>] operand");
        }
        return EncodeInsnMemImm(At, 0, 0, 0, 0, Operands[0]);
    }

    if (Op == "atcmp") {
        uint8_t Rd = 0;
        if (Operands.size() != 2 || !TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("ATCMP requires <reg>, [<addr>]");
        }
        return EncodeInsnMemImm(Opcode::ATCMP, Rd, 0, 0, 0, Operands[1]);
    }

    if (Op == "atreduce") {
        if (Operands.size() != 3 || !IsNumeric(Operands[2])) {
            throw std::runtime_error(
                "ATREDUCE requires <reg>, [<addr>], <op immediate>");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("ATREDUCE needs GPR as first operand");
        }
        const int32_t Selector =
            static_cast<int32_t>(ParseImmediateToken(Operands[2]) & 0xFFULL);
        return EncodeInsnMemImm(Opcode::ATREDUCE, Rd, 0, 0, Selector,
                                Operands[1]);
    }

    if (Op == "atcas") {
        if (Operands.size() != 3) {
            throw std::runtime_error(
                "ATCAS requires <new reg>, <expected reg|#imm>, [<addr>]");
        }
        uint8_t RdNew = 0;
        if (!TryParseRegister(Operands[0], RdNew)) {
            throw std::runtime_error("ATCAS first operand must be a register");
        }
        const std::string& AddrTok = Operands[2];
        if (AddrTok.size() < 3 || AddrTok.front() != '[' || AddrTok.back() != ']') {
            throw std::runtime_error("ATCAS last operand must be [<addr>]");
        }
        std::string AddrInner = AddrTok.substr(1, AddrTok.size() - 2);
        EncodedInsn Insn;

        uint8_t RsExp = 0;
        if (TryParseRegister(Operands[1], RsExp)) {
            if (IsNumeric(AddrInner)) {
                Insn.Base =
                    PackBase(Opcode::ATCAS, true, false, RdNew, RsExp, 0,
                             OpmodeInt64, 0);
                Insn.Imm1 = ParseImmediateToken(AddrInner);
                Insn.HasImm1 = true;
                return Insn;
            }
            Insn.Base =
                PackBase(Opcode::ATCAS, true, false, RdNew, RsExp, 0,
                         OpmodeInt64, 0);
            QueueImmFixup(AddrInner);
            Insn.Imm1 = 0;
            Insn.HasImm1 = true;
            return Insn;
        }
        if (!IsNumeric(Operands[1])) {
            throw std::runtime_error(
                "ATCAS expected value must be register or immediate");
        }
        const uint64_t Expected = ParseImmediateToken(Operands[1]);
        if (IsNumeric(AddrInner)) {
            Insn.Base =
                PackBase(Opcode::ATCAS, true, true, RdNew, 0, 0, OpmodeInt64,
                         0);
            Insn.Imm1 = ParseImmediateToken(AddrInner);
            Insn.Imm2 = Expected;
            Insn.HasImm1 = true;
            Insn.HasImm2 = true;
            return Insn;
        }
        Insn.Base =
            PackBase(Opcode::ATCAS, true, true, RdNew, 0, 0, OpmodeInt64, 0);
        QueueImmFixup(AddrInner);
        Insn.Imm1 = 0;
        Insn.Imm2 = Expected;
        Insn.HasImm1 = true;
        Insn.HasImm2 = true;
        return Insn;
    }

    auto EncodeFsxAlu = [&](Opcode AluOpcode) -> EncodedInsn {
        if (Operands.size() != 3) {
            throw std::runtime_error("FSX ALU requires three operands");
        }
        uint8_t Rd = 0, Rs = 0, Rt = 0;
        if (!TryParseFsxRegister(Operands[0], Rd) ||
            !TryParseFsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("FSX ALU requires F registers");
        }
        if (TryParseFsxRegister(Operands[2], Rt)) {
            return EncodeOpmodeRRR(AluOpcode, Rd, Rs, Rt, OpmodeFp64);
        }
        return EncodeOpmodeImm(AluOpcode, Rd, Rs, ParseImmediateToken(Operands[2]),
                               OpmodeFp64);
    };

    if (Op == "fload") {
        uint8_t Dest = 0;
        if (Operands.size() != 2 || !TryParseFsxRegister(Operands[0], Dest)) {
            throw std::runtime_error("FLOAD requires F register destination");
        }
        const std::string& Src = Operands[1];
        if (Src.size() < 3 || Src.front() != '[' || Src.back() != ']') {
            throw std::runtime_error("FLOAD requires [<addr>] operand");
        }
        return EncodeInsnMemImm(Opcode::FLOAD, Dest, 0, 0, 0, Src, OpmodeFp64);
    }
    if (Op == "fstore") {
        uint8_t Src = 0;
        if (Operands.size() != 2 || !TryParseFsxRegister(Operands[0], Src)) {
            throw std::runtime_error("FSTORE requires F register source");
        }
        const std::string& Dst = Operands[1];
        if (Dst.size() < 3 || Dst.front() != '[' || Dst.back() != ']') {
            throw std::runtime_error("FSTORE requires [<addr>] operand");
        }
        return EncodeInsnMemImm(Opcode::FSTORE, Src, 0, 0, 0, Dst, OpmodeFp64);
    }
    if (Op == "fadd") return EncodeFsxAlu(Opcode::FADD);
    if (Op == "fsub") return EncodeFsxAlu(Opcode::FSUB);
    if (Op == "fmul") return EncodeFsxAlu(Opcode::FMUL);
    if (Op == "fdiv") return EncodeFsxAlu(Opcode::FDIV);
    if (Op == "fmod") return EncodeFsxAlu(Opcode::FMOD);
    if (Op == "fma" || Op == "fmadd") return EncodeFsxAlu(Opcode::FMA);
    auto EncodeFsxUnary = [&](Opcode UnaryOpcode) -> EncodedInsn {
        uint8_t Rd = 0, Rs = 0;
        if (Operands.size() != 2 || !TryParseFsxRegister(Operands[0], Rd) ||
            !TryParseFsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("FSX unary op requires <fd>, <fs>");
        }
        return EncodeOpmodeRRR(UnaryOpcode, Rd, Rs, 0, OpmodeFp64);
    };
    if (Op == "fsqrt") {
        return EncodeFsxUnary(Opcode::FSQRT);
    }
    if (Op == "fabs") {
        return EncodeFsxUnary(Opcode::FABS);
    }
    if (Op == "fneg") {
        return EncodeFsxUnary(Opcode::FNEG);
    }
    if (Op == "frecip" || Op == "recip") {
        return EncodeFsxUnary(Opcode::RECIP);
    }
    if (Op == "frsqrt" || Op == "rsqrt") {
        return EncodeFsxUnary(Opcode::RSQRT);
    }
    if (Op == "fcopy" || Op == "fmov") {
        if (Operands.size() != 2) {
            throw std::runtime_error("FCOPY requires two F registers");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseFsxRegister(Operands[0], Rd) ||
            !TryParseFsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("FCOPY requires F registers");
        }
        return EncodeOpmodeRRR(Opcode::FCOPY, Rd, Rs, 0, OpmodeFp64);
    }
    if (Op == "frsh" || Op == "flsh" || Op == "fror" || Op == "frol") {
        Opcode Sh = Opcode::NOP;
        if (Op == "frsh") Sh = Opcode::FRSH;
        else if (Op == "flsh") Sh = Opcode::FLSH;
        else if (Op == "fror") Sh = Opcode::FROR;
        else Sh = Opcode::FROL;
        if (Operands.size() != 3) {
            throw std::runtime_error("FSX shift/rotate requires <fd>, <fs>, <amt>");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseFsxRegister(Operands[0], Rd) ||
            !TryParseFsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("FSX shift requires F registers");
        }
        if (IsNumeric(Operands[2])) {
            return EncodeOpmodeImm(Sh, Rd, Rs, ParseImmediateToken(Operands[2]), OpmodeFp64);
        }
        uint8_t Rt = 0;
        if (!TryParseFsxRegister(Operands[2], Rt)) {
            throw std::runtime_error("FSX shift amount must be immediate or F register");
        }
        return EncodeOpmodeRRR(Sh, Rd, Rs, Rt, OpmodeFp64);
    }
    if (Op == "fcmp") {
        uint8_t Rs = 0, Rt = 0;
        if (Operands.size() != 2 || !TryParseFsxRegister(Operands[0], Rs)) {
            throw std::runtime_error("FCMP requires F register first operand");
        }
        if (TryParseFsxRegister(Operands[1], Rt)) {
            return EncodeOpmodeRRR(Opcode::FCMP, 0, Rs, Rt, OpmodeFp64);
        }
        return EncodeOpmodeImm(Opcode::FCMP, 0, Rs, ParseImmediateToken(Operands[1]),
                               OpmodeFp64);
    }

    auto EncodeSsxAlu = [&](Opcode AluOpcode, uint8_t Opmode) -> EncodedInsn {
        if (Operands.size() != 3) {
            throw std::runtime_error("SSX ALU requires three X register operands");
        }
        uint8_t Rd = 0, Rs = 0, Rt = 0;
        if (!TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs) ||
            !TryParseSsxRegister(Operands[2], Rt)) {
            throw std::runtime_error("SSX ALU requires X registers");
        }
        return EncodeOpmodeRRR(AluOpcode, Rd, Rs, Rt, Opmode);
    };

    auto EncodeSsxMem = [&](Opcode MemOpcode) -> EncodedInsn {
        if (MemOpcode == Opcode::XLOAD || MemOpcode == Opcode::VLOAD) {
            uint8_t Dest = 0;
            if (Operands.size() != 2 || !TryParseSsxOrComplexSsxRegister(Operands[0], Dest)) {
                throw std::runtime_error("SSX load requires X/CX register destination");
            }
            const std::string& Src = Operands[1];
            if (Src.size() < 3 || Src.front() != '[' || Src.back() != ']') {
                throw std::runtime_error("SSX load requires [<addr>] operand");
            }
            return EncodeInsnMemImm(MemOpcode, Dest, 0, 0, 0, Src, OpmodeSsxFp32);
        }
        uint8_t Src = 0;
        if (Operands.size() != 2 || !TryParseSsxOrComplexSsxRegister(Operands[0], Src)) {
            throw std::runtime_error("SSX store requires X/CX register source");
        }
        const std::string& Dst = Operands[1];
        if (Dst.size() < 3 || Dst.front() != '[' || Dst.back() != ']') {
            throw std::runtime_error("SSX store requires [<addr>] operand");
        }
        return EncodeInsnMemImm(MemOpcode, Src, 0, 0, 0, Dst, OpmodeSsxFp32);
    };
    if (Op == "xload" || Op == "vload") {
        return EncodeSsxMem(Op == "vload" ? Opcode::VLOAD : Opcode::XLOAD);
    }
    if (Op == "xstore" || Op == "vstore") {
        return EncodeSsxMem(Op == "vstore" ? Opcode::VSTORE : Opcode::XSTORE);
    }
    if (Op == "xcopy") {
        if (Operands.size() != 2) {
            throw std::runtime_error("XCOPY requires two X registers");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("XCOPY requires X registers");
        }
        return EncodeOpmodeRRR(Opcode::XCOPY, Rd, Rs, 0, OpmodeSsxGeneric);
    }
    auto EncodeSsxShift = [&](Opcode ShOp) -> EncodedInsn {
        if (Operands.size() != 3) {
            throw std::runtime_error("SSX shift requires <xd>, <xs>, #u6");
        }
        uint8_t Rd = 0, Rs = 0;
        if (!TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("SSX shift requires X registers");
        }
        if (!IsNumeric(Operands[2])) {
            throw std::runtime_error("SSX shift amount must be immediate");
        }
        return EncodeOpmodeImm(ShOp, Rd, Rs, ParseImmediateToken(Operands[2]),
                               OpmodeSsxGeneric);
    };
    if (Op == "xrsh") {
        return EncodeSsxShift(Opcode::XRSH);
    }
    if (Op == "xlsh") {
        return EncodeSsxShift(Opcode::XLSH);
    }
    if (Op == "xror") {
        return EncodeSsxShift(Opcode::XROR);
    }
    if (Op == "xrol") {
        return EncodeSsxShift(Opcode::XROL);
    }
    if (Op == "xmul") {
        return EncodeSsxAlu(Opcode::XMUL, OpmodeSsxGeneric);
    }
    if (Op == "xdiv") {
        return EncodeSsxAlu(Opcode::XDIV, OpmodeSsxGeneric);
    }
    if (Op == "xadd") {
        return EncodeSsxAlu(Opcode::XADD, OpmodeSsxGeneric);
    }
    if (Op == "xsub") {
        return EncodeSsxAlu(Opcode::XSUB, OpmodeSsxGeneric);
    }
    if (Op == "xcmp") {
        if (Operands.size() != 2) {
            throw std::runtime_error("XCMP requires two X registers");
        }
        uint8_t Rs = 0, Rt = 0;
        if (!TryParseSsxRegister(Operands[0], Rs) ||
            !TryParseSsxRegister(Operands[1], Rt)) {
            throw std::runtime_error("XCMP requires X registers");
        }
        return EncodeOpmodeRRR(Opcode::XCMP, 0, Rs, Rt, OpmodeSsxGeneric);
    }
    if (Op == "xand") {
        return EncodeSsxAlu(Opcode::XAND, OpmodeSsxGeneric);
    }
    if (Op == "xxor") {
        return EncodeSsxAlu(Opcode::XXOR, OpmodeSsxGeneric);
    }
    if (Op == "xnot") {
        uint8_t Rd = 0, Rs = 0;
        if (Operands.size() != 2 || !TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("XNOT requires two X registers");
        }
        return EncodeOpmodeRRR(Opcode::XNOT, Rd, Rs, 0, OpmodeSsxGeneric);
    }
    if (Op == "xswap") {
        uint8_t Rd = 0, Rs = 0;
        if (Operands.size() != 2 || !TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("XSWAP requires two X registers");
        }
        return EncodeOpmodeRRR(Opcode::XSWAP128, Rd, Rs, 0, OpmodeSsxGeneric);
    }
    auto EncodeSsxCrypto = [&](Opcode CryptoOp) -> EncodedInsn {
        if (CryptoOp == Opcode::XGEN && Operands.size() == 2) {
            uint8_t Rd = 0;
            if (!TryParseSsxRegister(Operands[0], Rd)) {
                throw std::runtime_error("XGEN requires X destination register");
            }
            if (IsNumeric(Operands[1])) {
                return EncodeOpmodeImm(CryptoOp, Rd, 0, ParseImmediateToken(Operands[1]),
                                       OpmodeSsxGeneric);
            }
            uint8_t Rs = 0;
            if (!TryParseSsxRegister(Operands[1], Rs)) {
                throw std::runtime_error("XGEN KDF form requires <xd>, <xs>");
            }
            return EncodeOpmodeRRR(CryptoOp, Rd, Rs, 0, OpmodeSsxGeneric);
        }
        if (Operands.size() == 3) {
            uint8_t Rd = 0, Rs = 0, Rt = 0;
            if (!TryParseSsxRegister(Operands[0], Rd) ||
                !TryParseSsxRegister(Operands[1], Rs) ||
                !TryParseSsxRegister(Operands[2], Rt)) {
                throw std::runtime_error("SSX crypto requires X registers");
            }
            return EncodeOpmodeRRR(CryptoOp, Rd, Rs, Rt, OpmodeSsxGeneric);
        }
        throw std::runtime_error("SSX crypto operand mismatch");
    };
    if (Op == "xcrypt") {
        return EncodeSsxCrypto(Opcode::XCRYPT);
    }
    if (Op == "xdecrypt") {
        return EncodeSsxCrypto(Opcode::XDECRYPT);
    }
    if (Op == "xgen") {
        return EncodeSsxCrypto(Opcode::XGEN);
    }
    if (Op == "vadd") {
        return EncodeSsxAlu(Opcode::VADD, OpmodeSsxFp32);
    }
    if (Op == "vsub") {
        return EncodeSsxAlu(Opcode::VSUB, OpmodeSsxFp32);
    }
    if (Op == "vmul") {
        return EncodeSsxAlu(Opcode::VMUL, OpmodeSsxFp32);
    }
    if (Op == "vdiv") {
        return EncodeSsxAlu(Opcode::VDIV, OpmodeSsxFp32);
    }
    if (Op == "vmin") {
        return EncodeSsxAlu(Opcode::VMIN, OpmodeSsxFp32);
    }
    if (Op == "vmax") {
        return EncodeSsxAlu(Opcode::VMAX, OpmodeSsxFp32);
    }
    if (Op == "vfma" || Op == "vfmadd") {
        return EncodeSsxAlu(Opcode::VFMA, OpmodeSsxFp32);
    }
    if (Op == "vfmsub") {
        return EncodeSsxAlu(Opcode::VFMSUB, OpmodeSsxFp32);
    }
    if (Op == "vfnmadd") {
        return EncodeSsxAlu(Opcode::VFNMADD, OpmodeSsxFp32);
    }
    if (Op == "vfnmsub") {
        return EncodeSsxAlu(Opcode::VFNMSUB, OpmodeSsxFp32);
    }
    auto EncodeSsxUnaryFp32 = [&](Opcode UnaryOpcode) -> EncodedInsn {
        uint8_t Rd = 0, Rs = 0;
        if (Operands.size() != 2 || !TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs)) {
            throw std::runtime_error("SSX unary lane op requires <xd>, <xs>");
        }
        return EncodeOpmodeRRR(UnaryOpcode, Rd, Rs, 0, OpmodeSsxFp32);
    };
    if (Op == "vsqrt") {
        return EncodeSsxUnaryFp32(Opcode::VSQRT);
    }
    if (Op == "vabs") {
        return EncodeSsxUnaryFp32(Opcode::VABS);
    }
    if (Op == "vneg") {
        return EncodeSsxUnaryFp32(Opcode::VNEG);
    }
    if (Op == "vrcp") {
        return EncodeSsxUnaryFp32(Opcode::VRCP);
    }
    if (Op == "vrsqrt") {
        return EncodeSsxUnaryFp32(Opcode::VRSQRT);
    }
    auto EncodeMatrixDesc = [&](Opcode MatrixOp) -> EncodedInsn {
        if (Operands.size() != 2) {
            throw std::runtime_error("Matrix op requires <xd>, <descriptor>");
        }
        uint8_t Rd = 0;
        if (!TryParseSsxRegister(Operands[0], Rd)) {
            throw std::runtime_error("Matrix op requires X destination register");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(MatrixOp, true, false, Rd, 0, 0, OpmodeSsxFp32, 0);
        if (IsNumeric(Operands[1])) {
            Insn.Imm1 = ParseImmediateToken(Operands[1]);
        } else {
            QueueImmFixup(Operands[1]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    };
    if (Op == "matmul") {
        return EncodeMatrixDesc(Opcode::MATMUL);
    }
    if (Op == "gemm") {
        return EncodeMatrixDesc(Opcode::GEMM_OFFLOAD);
    }
    if (Op == "qgemm") {
        return EncodeMatrixDesc(Opcode::QGEMM);
    }
    if (Op == "tensor_load") {
        return EncodeMatrixDesc(Opcode::TENSOR_LOAD);
    }
    if (Op == "tensor_store") {
        return EncodeMatrixDesc(Opcode::TENSOR_STORE);
    }
    if (Op == "conv2d") {
        return EncodeMatrixDesc(Opcode::CONV2D);
    }
    if (Op == "qdot") {
        return EncodeMatrixDesc(Opcode::QDOT);
    }
    if (Op == "vdotp") {
        if (Operands.size() != 3) {
            throw std::runtime_error("VDOTP requires three X registers");
        }
        uint8_t Rd = 0, Rs = 0, Rt = 0;
        if (!TryParseSsxRegister(Operands[0], Rd) ||
            !TryParseSsxRegister(Operands[1], Rs) ||
            !TryParseSsxRegister(Operands[2], Rt)) {
            throw std::runtime_error("VDOTP requires X registers");
        }
        return EncodeOpmodeRRR(Opcode::VDOTP, Rd, Rs, Rt, OpmodeSsxFp32);
    }
    if (Op == "dotp_acc") {
        if (Operands.size() != 3) {
            throw std::runtime_error("DOTP_ACC requires <a#>, <xs>, <xt>");
        }
        uint8_t Acc = 0, Rs = 0, Rt = 0;
        if (!TryParseAccRegister(Operands[0], Acc) ||
            !TryParseSsxRegister(Operands[1], Rs) ||
            !TryParseSsxRegister(Operands[2], Rt)) {
            throw std::runtime_error("DOTP_ACC requires A and X registers");
        }
        return EncodeOpmodeRRR(Opcode::DOTP_ACC, Acc, Rs, Rt, OpmodeSsxFp32);
    }

    if (Op == "vbroadcast") {
        uint8_t Rd = 0;
        if (Operands.size() != 2 || !TryParseSsxRegister(Operands[0], Rd)) {
            throw std::runtime_error("VBROADCAST requires X destination register");
        }
        uint8_t Rs = 0;
        if (TryParseRegister(Operands[1], Rs)) {
            return EncodeOpmodeRRR(Opcode::VBROADCAST, Rd, Rs, 0, OpmodeSsxFp32);
        }
        return EncodeOpmodeImm(Opcode::VBROADCAST, Rd, 0, ParseImmediateToken(Operands[1]),
                               OpmodeSsxFp32);
    }

    Opcode BranchOp = Opcode::NOP;
    if (Op == "jmp") BranchOp = Opcode::JMP;
    else if (Op == "je") BranchOp = Opcode::JE;
    else if (Op == "jne") BranchOp = Opcode::JNE;
    else if (Op == "jz") BranchOp = Opcode::JZ;
    else if (Op == "jnz") BranchOp = Opcode::JNZ;
    else if (Op == "jc") BranchOp = Opcode::JC;
    else if (Op == "jnc") BranchOp = Opcode::JNC;
    else if (Op == "jlt") BranchOp = Opcode::JLT;
    else if (Op == "jgt") BranchOp = Opcode::JGT;
    else if (Op == "jo") BranchOp = Opcode::JO;
    else if (Op == "jno") BranchOp = Opcode::JNO;
    else if (Op == "call") BranchOp = Opcode::CALL;

    if (BranchOp != Opcode::NOP) {
        if (Operands.size() != 1) {
            throw std::runtime_error("Branch requires one operand");
        }
        if (BranchOp == Opcode::CALL) {
            if (IsNumeric(Operands[0])) {
                return EncodeBranchFar(BranchOp, ParseImmediateToken(Operands[0]));
            }
            QueueCallTargetFixup(Operands[0]);
            return EncodeBranchFar(BranchOp, 0);
        }
        if (IsNumeric(Operands[0])) {
            return EncodeBranchFar(BranchOp, ParseImmediateToken(Operands[0]));
        }
        QueueBranchFixup(Operands[0]);
        return EncodeBranch(BranchOp, 0);
    }

    if (Op == "savwin") {
        if (!Operands.empty()) {
            throw std::runtime_error("SAVWIN takes no operands");
        }
        return EncodeSystem(Opcode::SAVWIN);
    }
    if (Op == "reswin") {
        if (!Operands.empty()) {
            throw std::runtime_error("RESWIN takes no operands");
        }
        return EncodeSystem(Opcode::RESWIN);
    }
    if (Op == "streamfence") {
        if (!Operands.empty()) {
            throw std::runtime_error("STREAMFENCE takes no operands");
        }
        return EncodeSystem(Opcode::STREAMFENCE);
    }
    if (Op == "hintbarrier") {
        if (!Operands.empty()) {
            throw std::runtime_error("HINTBARRIER takes no operands");
        }
        return EncodeSystem(Opcode::HINT_BARRIER);
    }
    if (Op == "hint") {
        if (Operands.size() != 2 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("HINT requires #type, #line");
        }
        const uint64_t Type = ParseImmediateToken(Operands[0]);
        const uint64_t Line = ParseImmediateToken(Operands[1]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::HINT_OP, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = (Type << 24) | ((Line & 0xFF) << 16) | ((Line & 0x7FFFFULL) << 6);
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "streambind") {
        if (Operands.size() != 2 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("STREAMBIND requires #tag, #slot");
        }
        const uint64_t Tag = ParseImmediateToken(Operands[0]);
        const uint64_t Slot = ParseImmediateToken(Operands[1]);
        if (Slot > 7) {
            throw std::runtime_error("STREAMBIND slot must be 0..7");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::STREAMBIND, false, false, 0, static_cast<uint8_t>(Slot),
                             static_cast<uint8_t>(Tag & 0x3F), OpmodeInt64, 0);
        return Insn;
    }
    if (Op == "streamunbind") {
        if (Operands.size() != 1 || !IsNumeric(Operands[0])) {
            throw std::runtime_error("STREAMUNBIND requires #tag");
        }
        const uint64_t Tag = ParseImmediateToken(Operands[0]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::STREAMUNBIND, false, false, 0, 0,
                             static_cast<uint8_t>(Tag & 0x3F), OpmodeInt64, 0);
        return Insn;
    }

    if (Op == "numafence") {
        if (!Operands.empty()) {
            throw std::runtime_error("NUMAFENCE takes no operands");
        }
        return EncodeSystem(Opcode::NUMAFENCE);
    }
    if (Op == "numaadv") {
        EncodedInsn Insn;
        if (Operands.empty()) {
            return EncodeSystem(Opcode::NUMAADV);
        }
        if (Operands.size() != 1 || !IsNumeric(Operands[0])) {
            throw std::runtime_error("NUMAADV requires optional #node");
        }
        Insn.Base = PackBase(Opcode::NUMAADV, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[0]);
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "numabind") {
        if (Operands.size() != 2 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("NUMABIND requires #remote_line, #slot");
        }
        const uint64_t Line = ParseImmediateToken(Operands[0]);
        const uint64_t Slot = ParseImmediateToken(Operands[1]);
        const uint64_t Node = (Operands.size() > 2 && IsNumeric(Operands[2]))
                                  ? ParseImmediateToken(Operands[2])
                                  : 0;
        if (Slot > 7) {
            throw std::runtime_error("NUMABIND slot must be 0..7");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::NUMABIND, true, false, 0, static_cast<uint8_t>(Slot), 0,
                             OpmodeInt64, 0);
        Insn.Imm1 = (static_cast<uint64_t>(PhysMap::Domain::RemoteSkb) << 60) |
                    ((Node & 0xF) << 56) | ((Line & 0x7FFFFULL) << 6);
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "numastream" || Op == "numastreamout") {
        const Opcode Kop = (Op == "numastream") ? Opcode::NUMASTREAM : Opcode::NUMASTREAM;
        const uint8_t Mode = (Op == "numastreamout") ? 1 : 0;
        if (Operands.size() != 3 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("NUMASTREAM requires #tag, #len, <remote_addr>");
        }
        const uint64_t Tag = ParseImmediateToken(Operands[0]);
        const uint64_t Len = ParseImmediateToken(Operands[1]);
        const uint64_t Node = 0;
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, 0, static_cast<uint8_t>(Tag & 0x3F), Mode,
                             static_cast<int32_t>(Len & 0x0FFFFFFF));
        if (IsNumeric(Operands[2])) {
            const uint64_t Addr = ParseImmediateToken(Operands[2]);
            Insn.Imm1 = (static_cast<uint64_t>(PhysMap::Domain::RemoteDram) << 60) |
                        ((Node & 0xF) << 56) | (Addr & ((1ULL << 56) - 1));
        } else {
            QueueImmFixup(Operands[2]);
            Insn.Imm1 = static_cast<uint64_t>(PhysMap::Domain::RemoteDram) << 60;
        }
        Insn.HasImm1 = true;
        return Insn;
    }
    if (Op == "numaatomic") {
        if (Operands.size() < 2 || !IsNumeric(Operands[1])) {
            throw std::runtime_error("NUMAATOMIC requires <reg>, #remote_line");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("NUMAATOMIC requires GPR destination");
        }
        const uint64_t Line = ParseImmediateToken(Operands[1]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::NUMAATOMIC, true, false, Rd, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = (static_cast<uint64_t>(PhysMap::Domain::RemoteSkb) << 60) |
                    ((Line & 0x7FFFFULL) << 6);
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "igload" || Op == "igstore") {
        const Opcode Kop = (Op == "igload") ? Opcode::IGLOAD : Opcode::IGSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error("IGLOAD/IGSTORE require <reg>, [i#]");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("IGLOAD/IGSTORE need GPR operand");
        }
        const std::string& Bracket = Operands[1];
        if (Bracket.size() < 4 || Bracket.front() != '[' || Bracket.back() != ']') {
            throw std::runtime_error("IGLOAD/IGSTORE second operand must be [i0]..[i3]");
        }
        uint8_t IndexReg = 0;
        if (!TryParseIndexRegister(Bracket.substr(1, Bracket.size() - 2), IndexReg)) {
            throw std::runtime_error("IGLOAD/IGSTORE index must be I0..I3");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, Rd, IndexReg, 0, OpmodeInt64, 0);
        return Insn;
    }

    if (Op == "krot") {
        if (Operands.size() != 2) {
            throw std::runtime_error("KROT requires <reg>, [i#]");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("KROT destination must be a GPR");
        }
        uint8_t IndexReg = 0;
        const std::string& Bracket = Operands[1];
        if (!TryParseIndexRegister(Bracket.substr(1, Bracket.size() - 2), IndexReg)) {
            throw std::runtime_error("KROT index must be [i0]..[i3]");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::KROT, false, false, Rd, IndexReg, 0, OpmodeInt64,
                            static_cast<int32_t>(2u << 28));
        return Insn;
    }

    if (Op == "stream" || Op == "streamout") {
        const Opcode Kop = (Op == "stream") ? Opcode::STREAM : Opcode::STREAMOUT;
        if (Operands.size() != 3 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("STREAM requires #tag, #len, <addr>");
        }
        const uint64_t Tag = ParseImmediateToken(Operands[0]);
        const uint64_t Len = ParseImmediateToken(Operands[1]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, 0, static_cast<uint8_t>(Tag & 0x3F),
                             OpmodeInt64, static_cast<int32_t>(Len & 0x0FFFFFFF));
        if (IsNumeric(Operands[2])) {
            Insn.Imm1 = ParseImmediateToken(Operands[2]);
        } else {
            QueueImmFixup(Operands[2]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "kwind" || Op == "iwind") {
        if (Operands.size() != 1) {
            throw std::runtime_error("KWIND requires <reg> or #immediate");
        }
        const Opcode Kop = (Op == "iwind") ? Opcode::IWIND : Opcode::KWIND;
        uint8_t Rs = 0;
        if (TryParseRegister(Operands[0], Rs)) {
            EncodedInsn Insn;
            Insn.Base = PackBase(Kop, false, false, 0, Rs, 0, OpmodeInt64, 0);
            return Insn;
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = ParseImmediateToken(Operands[0]);
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "kload" || Op == "kstore") {
        const Opcode Kop = (Op == "kload") ? Opcode::KLOAD : Opcode::KSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error("KLOAD/KSTORE require <reg>, [i#]");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("KLOAD/KSTORE destination must be a GPR");
        }
        const std::string& Bracket = Operands[1];
        if (Bracket.size() < 4 || Bracket.front() != '[' || Bracket.back() != ']') {
            throw std::runtime_error("KLOAD/KSTORE second operand must be [i0]..[i3]");
        }
        uint8_t IndexReg = 0;
        if (!TryParseIndexRegister(Bracket.substr(1, Bracket.size() - 2), IndexReg)) {
            throw std::runtime_error("KLOAD/KSTORE index must be I0..I3");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, false, false, Rd, IndexReg, 0, OpmodeInt64, 0);
        return Insn;
    }

    if (Op == "kloadi" || Op == "kstorei") {
        const Opcode Kop = (Op == "kloadi") ? Opcode::KLOADI : Opcode::KSTOREI;
        if (Operands.size() != 2) {
            throw std::runtime_error("KLOADI/KSTOREI require <reg>, #index");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("KLOADI/KSTOREI require GPR and immediate index");
        }
        const uint64_t Index = ParseImmediateToken(Operands[1]);
        if (Index > 0x1F) {
            throw std::runtime_error("KLOADI/KSTOREI index must fit in 5 bits");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, Rd, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = Index;
        Insn.HasImm1 = true;
        return Insn;
    }

    auto EncodeSkbLineImm = [](uint64_t Line) -> uint64_t {
        return (static_cast<uint64_t>(PhysMap::Domain::LocalSkb) << 60) |
               ((Line & 0x7FFFFULL) << 6);
    };

    if (Op == "skbload" || Op == "skbstore" || Op == "skbstatus" ||
        Op == "skbatload" || Op == "skbatstore" || Op == "skbatadd" ||
        Op == "skbatswap") {
        Opcode Kop = Opcode::SKB_LOAD;
        if (Op == "skbstore") {
            Kop = Opcode::SKB_STORE;
        } else if (Op == "skbstatus") {
            Kop = Opcode::SKB_STATUS;
        } else if (Op == "skbatload") {
            Kop = Opcode::SKB_AT_LOAD;
        } else if (Op == "skbatstore") {
            Kop = Opcode::SKB_AT_STORE;
        } else if (Op == "skbatadd") {
            Kop = Opcode::SKB_AT_ADD;
        } else if (Op == "skbatswap") {
            Kop = Opcode::SKB_AT_SWAP;
        }
        if (Operands.size() != 2 || !IsNumeric(Operands[1])) {
            throw std::runtime_error("SKB ops require <reg>, #line");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("SKB operand must be a GPR");
        }
        const uint64_t Line = ParseImmediateToken(Operands[1]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, Rd, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = EncodeSkbLineImm(Line);
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "skbzero" || Op == "skbpin" || Op == "skbunpin" || Op == "skbflush" ||
        Op == "skbinvalidate" || Op == "skbprefetch") {
        Opcode Kop = Opcode::SKB_ZERO;
        if (Op == "skbpin") {
            Kop = Opcode::SKB_PIN;
        } else if (Op == "skbunpin") {
            Kop = Opcode::SKB_UNPIN;
        } else if (Op == "skbflush") {
            Kop = Opcode::SKB_FLUSH;
        } else if (Op == "skbinvalidate") {
            Kop = Opcode::SKB_INVALIDATE;
        } else if (Op == "skbprefetch") {
            Kop = Opcode::SKB_PREFETCH;
        }
        if (Operands.size() != 1 || !IsNumeric(Operands[0])) {
            throw std::runtime_error("SKB line ops require #line");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = EncodeSkbLineImm(ParseImmediateToken(Operands[0]));
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "skbbind" || Op == "skbunbind") {
        const Opcode Kop = (Op == "skbbind") ? Opcode::SKB_BIND : Opcode::SKB_UNBIND;
        if (Operands.size() < 2 || Operands.size() > 3 || !IsNumeric(Operands[0]) ||
            !IsNumeric(Operands[1])) {
            throw std::runtime_error("SKB_BIND/SKB_UNBIND require #line, #slot [, #mode]");
        }
        const uint64_t Line = ParseImmediateToken(Operands[0]);
        const uint64_t Slot = ParseImmediateToken(Operands[1]);
        if (Slot > 7) {
            throw std::runtime_error("SKB slot must be 0..7");
        }
        uint8_t BindMode = SkbProfile::BindModeShared;
        if (Op == "skbbind" && Operands.size() == 3) {
            const uint64_t Mode = ParseImmediateToken(Operands[2]);
            if (Mode > 2) {
                throw std::runtime_error("SKB_BIND mode must be 0=SHARED, 1=EXCLUSIVE, 2=PINNED");
            }
            BindMode = static_cast<uint8_t>(Mode);
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, 0, static_cast<uint8_t>(Slot), 0, BindMode, 0);
        Insn.Imm1 = EncodeSkbLineImm(Line);
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "skb_move") {
        if (Operands.size() != 2 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("SKB_MOVE requires #src_line, #dst_line");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::SKB_MOVE, true, true, 0, 0, 0, OpmodeInt64, 0);
        Insn.Imm1 = EncodeSkbLineImm(ParseImmediateToken(Operands[0]));
        Insn.Imm2 = EncodeSkbLineImm(ParseImmediateToken(Operands[1]));
        Insn.HasImm1 = true;
        Insn.HasImm2 = true;
        return Insn;
    }

    if (Op == "skbat_reduce") {
        if (Operands.size() != 3 || !IsNumeric(Operands[2])) {
            throw std::runtime_error("SKBAT_REDUCE requires <rd>, #line, #op");
        }
        uint8_t Rd = 0;
        if (!TryParseRegister(Operands[0], Rd)) {
            throw std::runtime_error("SKBAT_REDUCE requires GPR operand");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::SKB_AT_REDUCE, true, false, Rd, 0,
                             static_cast<uint8_t>(ParseImmediateToken(Operands[2]) & 0x3F),
                             OpmodeInt64, 0);
        Insn.Imm1 = EncodeSkbLineImm(ParseImmediateToken(Operands[1]));
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "skbatcas") {
        if (Operands.size() != 3 || !IsNumeric(Operands[2])) {
            throw std::runtime_error("SKBAT_CAS requires <rd>, <rs>, #line");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseRegister(Operands[1], Rs)) {
            throw std::runtime_error("SKBAT_CAS requires GPR operands");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Opcode::SKB_AT_CAS, true, false, Rd, Rs, 0, OpmodeInt64, 0);
        Insn.Imm1 = EncodeSkbLineImm(ParseImmediateToken(Operands[2]));
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "skb_bulk_load" || Op == "skb_bulk_store" || Op == "skb_bulk_zero") {
        Opcode Kop = Opcode::SKB_BULK_LOAD;
        if (Op == "skb_bulk_store") {
            Kop = Opcode::SKB_BULK_STORE;
        } else if (Op == "skb_bulk_zero") {
            Kop = Opcode::SKB_BULK_ZERO;
        }
        if (Operands.size() != 3 || !IsNumeric(Operands[0]) || !IsNumeric(Operands[1])) {
            throw std::runtime_error("SKB_BULK_* require #tag, #count, <addr>");
        }
        const uint64_t Tag = ParseImmediateToken(Operands[0]);
        const uint64_t Count = ParseImmediateToken(Operands[1]);
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, static_cast<uint8_t>(Tag & 0x3F), 0, 0,
                             OpmodeInt64, static_cast<int32_t>(Count & 0x0FFFFFFF));
        if (IsNumeric(Operands[2])) {
            Insn.Imm1 = ParseImmediateToken(Operands[2]);
        } else {
            QueueImmFixup(Operands[2]);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    }

    auto PackCplxInsn = [](Opcode Kop, uint8_t Rd, uint8_t Rs, uint8_t Rt,
                           CplxProfile::Precision Prec = CplxProfile::Precision::Fp64,
                           bool UseImm1 = false, uint64_t Imm1Val = 0,
                           bool UseImm2 = false, uint64_t Imm2Val = 0) {
        EncodedInsn Out;
        Out.Base = PackBase(Kop, UseImm1, UseImm2, Rd, Rs, Rt, OpmodeMixed,
                            CplxProfile::PackCplxDisp(Prec));
        if (UseImm1) {
            Out.Imm1 = Imm1Val;
            Out.HasImm1 = true;
        }
        if (UseImm2) {
            Out.Imm2 = Imm2Val;
            Out.HasImm2 = true;
        }
        return Out;
    };

    auto EncodeVcMem = [&](Opcode Kop, uint8_t Xd, const std::string& Bracket) {
        return EncodeInsnMemImm(Kop, Xd, 0, 0,
                                CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp32),
                                Bracket, OpmodeMixed);
    };

    auto PackVcInsn = [&](Opcode Kop, uint8_t Xd, uint8_t Xs, uint8_t Xt = 0) {
        return PackCplxInsn(Kop, Xd, Xs, Xt, CplxProfile::Precision::Fp32);
    };

    if (Op == "cfma" || Op == "cfms" || Op == "cnfma" || Op == "cnfms" || Op == "cmac") {
        Opcode Kop = Opcode::CFMA;
        if (Op == "cfms") {
            Kop = Opcode::CFMS;
        } else if (Op == "cnfma") {
            Kop = Opcode::CNFMA;
        } else if (Op == "cnfms") {
            Kop = Opcode::CNFMS;
        } else if (Op == "cmac") {
            Kop = Opcode::CMAC;
        }
        if (Operands.size() != 3) {
            throw std::runtime_error("CFMA family requires CRd, CRs, CRt");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs) ||
            !TryParseComplexRegister(Operands[2], Rt)) {
            throw std::runtime_error("CFMA family requires complex register operands");
        }
        return PackCplxInsn(Kop, Rd, Rs, Rt);
    }

    if (Op == "vcadd" || Op == "vcsub" || Op == "vcmul" || Op == "vcfma" || Op == "vcfms" ||
        Op == "vcmac") {
        Opcode Kop = Opcode::VCADD;
        if (Op == "vcsub") {
            Kop = Opcode::VCSUB;
        } else if (Op == "vcmul") {
            Kop = Opcode::VCMUL;
        } else if (Op == "vcfma" || Op == "vcmac") {
            Kop = Opcode::VCFMA;
        } else if (Op == "vcfms") {
            Kop = Opcode::VCFMS;
        }
        if (Operands.size() != 3) {
            throw std::runtime_error("Vector complex ternary ops require CXd, CXs, CXt");
        }
        uint8_t Xd = 0;
        uint8_t Xs = 0;
        uint8_t Xt = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseComplexSsxRegister(Operands[1], Xs) ||
            !TryParseComplexSsxRegister(Operands[2], Xt)) {
            throw std::runtime_error("Vector complex ops require cx0..cx15 operands");
        }
        return PackVcInsn(Kop, Xd, Xs, Xt);
    }

    if (Op == "vcconj") {
        if (Operands.size() != 2) {
            throw std::runtime_error("VCCONJ requires CXd, CXs");
        }
        uint8_t Xd = 0;
        uint8_t Xs = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseComplexSsxRegister(Operands[1], Xs)) {
            throw std::runtime_error("VCCONJ requires cx registers");
        }
        return PackVcInsn(Opcode::VCCONJ, Xd, Xs, 0);
    }

    if (Op == "vcfft2") {
        if (Operands.size() != 2) {
            throw std::runtime_error("VCFFT2 requires CXd, CXs");
        }
        uint8_t Xd = 0;
        uint8_t Xs = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseComplexSsxRegister(Operands[1], Xs)) {
            throw std::runtime_error("VCFFT2 requires cx registers");
        }
        return PackVcInsn(Opcode::VCFFT2, Xd, Xs, 0);
    }

    if (Op == "vcfft4") {
        if (Operands.size() != 3) {
            throw std::runtime_error(
                "VCFFT4 requires CXd, CXs_lo, CXs_hi (baseline BVM: two cx regs, four complexes)");
        }
        uint8_t Xd = 0;
        uint8_t Xs = 0;
        uint8_t Xt = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseComplexSsxRegister(Operands[1], Xs) ||
            !TryParseComplexSsxRegister(Operands[2], Xt)) {
            throw std::runtime_error("VCFFT4 requires cx registers");
        }
        return PackVcInsn(Opcode::VCFFT4, Xd, Xs, Xt);
    }

    if (Op == "vcbroadcast") {
        if (Operands.size() != 2) {
            throw std::runtime_error("VCBROADCAST requires CXd, CRs");
        }
        uint8_t Xd = 0;
        uint8_t Cr = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseComplexRegister(Operands[1], Cr)) {
            throw std::runtime_error("VCBROADCAST requires CXd and CRs");
        }
        return PackVcInsn(Opcode::VCBROADCAST, Xd, Cr, 0);
    }

    if (Op == "cdiv" || Op == "carg" || Op == "cscale" || Op == "crot" || Op == "cexp" ||
        Op == "clog" || Op == "cpow" || Op == "csqrt" || Op == "cmagmul") {
        Opcode Kop = Opcode::CDIV;
        if (Op == "carg") {
            Kop = Opcode::CARG;
        } else if (Op == "cscale") {
            Kop = Opcode::CSCALE;
        } else if (Op == "crot") {
            Kop = Opcode::CROT;
        } else if (Op == "cexp") {
            Kop = Opcode::CEXP;
        } else if (Op == "clog") {
            Kop = Opcode::CLOG;
        } else if (Op == "cpow") {
            Kop = Opcode::CPOW;
        } else if (Op == "csqrt") {
            Kop = Opcode::CSQRT;
        } else if (Op == "cmagmul") {
            Kop = Opcode::CMAGMUL;
        }
        if (Op == "carg" || Op == "cmagmul") {
            if (Operands.size() != 2) {
                throw std::runtime_error("CARG/CMAGMUL require Rd, CRs");
            }
            uint8_t Rd = 0;
            uint8_t Cr = 0;
            if (!TryParseRegister(Operands[0], Rd) ||
                !TryParseComplexRegister(Operands[1], Cr)) {
                throw std::runtime_error("CARG/CMAGMUL require GPR Rd and CRs");
            }
            return PackCplxInsn(Kop, Rd, Cr, 0);
        }
        if (Op == "cexp" || Op == "clog" || Op == "csqrt") {
            if (Operands.size() != 2) {
                throw std::runtime_error("Unary complex ops require CRd, CRs");
            }
            uint8_t Rd = 0;
            uint8_t Rs = 0;
            if (!TryParseComplexRegister(Operands[0], Rd) ||
                !TryParseComplexRegister(Operands[1], Rs)) {
                throw std::runtime_error("Unary complex ops require CR operands");
            }
            return PackCplxInsn(Kop, Rd, Rs, 0);
        }
        if (Operands.size() != 3) {
            throw std::runtime_error("Ternary complex ops require CRd, CRs, CRt/GPR");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs)) {
            throw std::runtime_error("Complex ternary ops require CRd and CRs");
        }
        if (Op == "cscale" || Op == "crot") {
            if (!TryParseRegister(Operands[2], Rt)) {
                throw std::runtime_error("CSCALE/CROT require scalar GPR Rt");
            }
        } else if (!TryParseComplexRegister(Operands[2], Rt)) {
            throw std::runtime_error("CDIV/CPOW require CRt");
        }
        return PackCplxInsn(Kop, Rd, Rs, Rt);
    }

    if (Op == "ckload" || Op == "ckstore") {
        const Opcode Kop = (Op == "ckload") ? Opcode::CKLOAD : Opcode::CKSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error("CKLOAD/CKSTORE require CRd, <index-reg>");
        }
        uint8_t Cr = 0;
        uint8_t Idx = 0;
        if (!TryParseComplexRegister(Operands[0], Cr) ||
            !TryParseIndexRegister(Operands[1], Idx)) {
            throw std::runtime_error("CKLOAD/CKSTORE require CRd and I0..I3");
        }
        return PackCplxInsn(Kop, Cr, Idx, 0);
    }

    if (Op == "cload_soa" || Op == "cstore_soa") {
        const Opcode Kop =
            (Op == "cload_soa") ? Opcode::CLOAD_SOA : Opcode::CSTORE_SOA;
        if (Operands.size() != 3) {
            throw std::runtime_error("CLOAD_SOA/CSTORE_SOA require CR, [real], [imag]");
        }
        uint8_t Cr = 0;
        if (!TryParseComplexRegister(Operands[0], Cr)) {
            throw std::runtime_error("CLOAD_SOA/CSTORE_SOA require CR operand");
        }
        const std::string& RealTok = Operands[1];
        const std::string& ImagTok = Operands[2];
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, true, Cr, 0, 0, OpmodeMixed,
                             CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp64));
        const std::string RealInner =
            RealTok.substr(1, RealTok.size() - 2);
        const std::string ImagInner =
            ImagTok.substr(1, ImagTok.size() - 2);
        if (IsNumeric(RealInner)) {
            Insn.Imm1 = ParseImmediateToken(RealInner);
        } else {
            QueueImmFixup(RealInner);
            Insn.Imm1 = 0;
        }
        if (IsNumeric(ImagInner)) {
            Insn.Imm2 = ParseImmediateToken(ImagInner);
        } else {
            QueueImmFixup(ImagInner);
            Insn.Imm2 = 0;
        }
        Insn.HasImm1 = true;
        Insn.HasImm2 = true;
        return Insn;
    }

    if (Op == "cswap" || Op == "cinsert" || Op == "cunpack" || Op == "csplat") {
        Opcode Kop = Opcode::CSWAP;
        if (Op == "cinsert") {
            Kop = Opcode::CINSERT;
        } else if (Op == "cunpack") {
            Kop = Opcode::CUNPACK;
        } else if (Op == "csplat") {
            Kop = Opcode::CSPLAT;
        }
        if (Op == "cswap" && Operands.size() == 2) {
            uint8_t Rd = 0;
            uint8_t Rs = 0;
            if (!TryParseComplexRegister(Operands[0], Rd) ||
                !TryParseComplexRegister(Operands[1], Rs)) {
                throw std::runtime_error("CSWAP requires two CR operands");
            }
            return PackCplxInsn(Kop, Rd, Rs, 0);
        }
        if (Op == "cinsert" && Operands.size() == 3) {
            uint8_t Rd = 0;
            uint8_t Rs = 0;
            if (!TryParseComplexRegister(Operands[0], Rd) ||
                !TryParseRegister(Operands[1], Rs) || !IsNumeric(Operands[2])) {
                throw std::runtime_error("CINSERT requires CRd, Rs, #which");
            }
            return PackCplxInsn(Kop, Rd, Rs, 0, CplxProfile::Precision::Fp64, true,
                                ParseImmediateToken(Operands[2]));
        }
        if (Op == "cunpack" && Operands.size() == 3) {
            uint8_t Rd = 0;
            uint8_t Rt = 0;
            uint8_t Cr = 0;
            if (!TryParseRegister(Operands[0], Rd) || !TryParseRegister(Operands[1], Rt) ||
                !TryParseComplexRegister(Operands[2], Cr)) {
                throw std::runtime_error("CUNPACK requires Rd, Rt, CRs");
            }
            return PackCplxInsn(Kop, Rd, Rt, Cr);
        }
        throw std::runtime_error("Invalid operands for complex data-movement opcode");
    }

    if (Op == "cceq" || Op == "ccne" || Op == "ccmplt" || Op == "ccmpgt") {
        Opcode Kop = Opcode::CCEQ;
        if (Op == "ccne") {
            Kop = Opcode::CCNE;
        } else if (Op == "ccmplt") {
            Kop = Opcode::CCMPLT;
        } else if (Op == "ccmpgt") {
            Kop = Opcode::CCMPGT;
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (Operands.size() != 3 || !TryParseRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs) ||
            !TryParseComplexRegister(Operands[2], Rt)) {
            throw std::runtime_error("Complex compare requires Rd, CRs, CRt");
        }
        return PackCplxInsn(Kop, Rd, Rs, Rt);
    }

    if (Op == "csel" || Op == "cmin" || Op == "cmax") {
        Opcode Kop = Opcode::CSEL;
        if (Op == "cmin") {
            Kop = Opcode::CMIN;
        } else if (Op == "cmax") {
            Kop = Opcode::CMAX;
        }
        if (Operands.size() != 3 && Operands.size() != 4) {
            throw std::runtime_error("CSEL/CMIN/CMAX require CRd, CRs, CRt [, pred]");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs) ||
            !TryParseComplexRegister(Operands[2], Rt)) {
            throw std::runtime_error("CSEL/CMIN/CMAX require complex operands");
        }
        if (Op == "csel" && Operands.size() == 4) {
            uint8_t Pred = 0;
            if (!TryParseRegister(Operands[3], Pred)) {
                throw std::runtime_error("CSEL predicate must be a GPR");
            }
            return PackCplxInsn(Kop, Rd, Rs, Pred);
        }
        return PackCplxInsn(Kop, Rd, Rs, Rt);
    }

    if (Op == "vcdiv" || Op == "vcmag" || Op == "vcarg" || Op == "vcscale" || Op == "vcrot" ||
        Op == "vcdot" || Op == "vcnorm" || Op == "vczip" || Op == "vcunzip" || Op == "vcswap" ||
        Op == "vcreverse" || Op == "vctwiddle" || Op == "vcsel" || Op == "vcmin" || Op == "vcmax") {
        if (Op == "vcmag" || Op == "vcarg" || Op == "vcnorm") {
            Opcode Kop = Opcode::VCMAG;
            if (Op == "vcarg") {
                Kop = Opcode::VCARG;
            } else if (Op == "vcnorm") {
                Kop = Opcode::VCNORM;
            }
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            if (Op == "vcnorm") {
                if (Operands.size() != 2 || !TryParseRegister(Operands[0], Xd) ||
                    !TryParseComplexSsxRegister(Operands[1], Xs)) {
                    throw std::runtime_error("VCNORM requires Rd, CXs");
                }
                return PackCplxInsn(Kop, Xd, Xs, 0, CplxProfile::Precision::Fp32);
            }
            if (Operands.size() != 2 || !TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs)) {
                throw std::runtime_error("VCMAG/VCARG require CXd, CXs");
            }
            return PackVcInsn(Kop, Xd, Xs, 0);
        }
        if (Op == "vcdot") {
            uint8_t Cr = 0;
            uint8_t Xs = 0;
            uint8_t Xt = 0;
            if (Operands.size() != 3 || !TryParseComplexRegister(Operands[0], Cr) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) ||
                !TryParseComplexSsxRegister(Operands[2], Xt)) {
                throw std::runtime_error("VCDOT requires CRd, CXs, CXt");
            }
            return PackVcInsn(Opcode::VCDOT, Cr, Xs, Xt);
        }
        if (Op == "vcreverse" || Op == "vcswap") {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            Opcode Kop = Opcode::VCREVERSE;
            if (Op == "vcswap") {
                Kop = Opcode::VCSWAP;
            }
            if (Operands.size() != 2 || !TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs)) {
                throw std::runtime_error("Unary vector complex ops require CXd, CXs");
            }
            return PackVcInsn(Kop, Xd, Xs, 0);
        }
        if (Op == "vctwiddle" && Operands.size() == 3) {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) || !IsNumeric(Operands[2])) {
                throw std::runtime_error("VCTWIDDLE requires CXd, CXs, #k");
            }
            return PackCplxInsn(Opcode::VCTWIDDLE, Xd, Xs, 0,
                                CplxProfile::Precision::Fp32, true,
                                ParseImmediateToken(Operands[2]));
        }
        if (Op == "vczip" || Op == "vcunzip") {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            uint8_t Xt = 0;
            Opcode Kop = Opcode::VCZIP;
            if (Op == "vcunzip") {
                Kop = Opcode::VCUNZIP;
            }
            if (Operands.size() != 3 || !TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) ||
                !TryParseComplexSsxRegister(Operands[2], Xt)) {
                throw std::runtime_error("VCZIP/VCUNZIP require three CX operands");
            }
            return PackVcInsn(Kop, Xd, Xs, Xt);
        }
        if (Op == "vcsel" && Operands.size() == 3) {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            uint8_t Xt = 0;
            if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) ||
                !TryParseComplexSsxRegister(Operands[2], Xt)) {
                throw std::runtime_error("VCSEL requires CXd, CXs, CXt");
            }
            return PackVcInsn(Opcode::VCSEL, Xd, Xs, Xt);
        }
        if (Op == "vcmin" || Op == "vcmax") {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            uint8_t Xt = 0;
            if (Operands.size() != 3 || !TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) ||
                !TryParseComplexSsxRegister(Operands[2], Xt)) {
                throw std::runtime_error("VCMIN/VCMAX require CXd, CXs, CXt");
            }
            return PackVcInsn(Op == "vcmax" ? Opcode::VCMAX : Opcode::VCMIN, Xd, Xs, Xt);
        }
        if (Op == "vcscale" || Op == "vcrot" || Op == "vcdiv") {
            uint8_t Xd = 0;
            uint8_t Xs = 0;
            uint8_t Xt = 0;
            Opcode Kop = Opcode::VCDIV;
            if (Op == "vcscale") {
                Kop = Opcode::VCSCALE;
            } else if (Op == "vcrot") {
                Kop = Opcode::VCROT;
            }
            if (Operands.size() != 3 || !TryParseComplexSsxRegister(Operands[0], Xd) ||
                !TryParseComplexSsxRegister(Operands[1], Xs) ||
                !TryParseSsxOrComplexSsxRegister(Operands[2], Xt)) {
                throw std::runtime_error("VCSCALE/VCROT/VCDIV require CXd, CXs, CXt/Xt");
            }
            return PackVcInsn(Kop, Xd, Xs, Xt);
        }
    }

    if (Op == "vcload" || Op == "vcstore") {
        const Opcode Kop = (Op == "vcload") ? Opcode::VCLOAD : Opcode::VCSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error("VCLOAD/VCSTORE require CXd, [<addr>]");
        }
        uint8_t Xd = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd)) {
            throw std::runtime_error("VCLOAD/VCSTORE require cx register");
        }
        return EncodeVcMem(Kop, Xd, Operands[1]);
    }

    if (Op == "vcgather" || Op == "vcscatter") {
        const Opcode Kop = (Op == "vcgather") ? Opcode::VCGATHER : Opcode::VCSCATTER;
        if (Operands.size() != 3) {
            throw std::runtime_error("VCGATHER/VCSCATTER require CXd, [<base>], <Ii>");
        }
        uint8_t Xd = 0;
        uint8_t Idx = 0;
        if (!TryParseComplexSsxRegister(Operands[0], Xd) ||
            !TryParseIndexRegister(Operands[2], Idx)) {
            throw std::runtime_error("VCGATHER/VCSCATTER require CXd and index register");
        }
        const std::string& Bracket = Operands[1];
        if (Bracket.size() < 3 || Bracket.front() != '[' || Bracket.back() != ']') {
            throw std::runtime_error("VCGATHER/VCSCATTER require [<base>]");
        }
        EncodedInsn Insn;
        Insn.Base = PackBase(Kop, true, false, Xd, Idx, 0, OpmodeMixed,
                             CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp32));
        const std::string Inner = Bracket.substr(1, Bracket.size() - 2);
        if (IsNumeric(Inner)) {
            Insn.Imm1 = ParseImmediateToken(Inner);
        } else {
            QueueImmFixup(Inner);
            Insn.Imm1 = 0;
        }
        Insn.HasImm1 = true;
        return Insn;
    }

    if (Op == "cadd" || Op == "csub" || Op == "cmul") {
        Opcode Kop = Opcode::CADD;
        if (Op == "csub") {
            Kop = Opcode::CSUB;
        } else if (Op == "cmul") {
            Kop = Opcode::CMUL;
        }
        if (Operands.size() != 3) {
            throw std::runtime_error("CADD/CSUB/CMUL require CRd, CRs, CRt");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs) ||
            !TryParseComplexRegister(Operands[2], Rt)) {
            throw std::runtime_error("CADD/CSUB/CMUL require complex register operands (cr0..cr31)");
        }
        return PackCplxInsn(Kop, Rd, Rs, Rt);
    }

    if (Op == "cconj" || Op == "cneg" || Op == "cmove") {
        Opcode Kop = Opcode::CCONJ;
        if (Op == "cneg") {
            Kop = Opcode::CNEG;
        } else if (Op == "cmove") {
            Kop = Opcode::CMOVE;
        }
        if (Operands.size() != 2) {
            throw std::runtime_error("CCONJ/CNEG/CMOVE require CRd, CRs");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseComplexRegister(Operands[1], Rs)) {
            throw std::runtime_error("CCONJ/CNEG/CMOVE require complex register operands");
        }
        return PackCplxInsn(Kop, Rd, Rs, 0);
    }

    if (Op == "cmag" || Op == "cabs" || Op == "cmsq") {
        Opcode Kop = Opcode::CMAG;
        if (Op == "cabs") {
            Kop = Opcode::CABS;
        } else if (Op == "cmsq") {
            Kop = Opcode::CMSQ;
        }
        if (Operands.size() != 2) {
            throw std::runtime_error("CMAG/CABS/CMSQ require Rd, CRs");
        }
        uint8_t Rd = 0;
        uint8_t Cr = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseComplexRegister(Operands[1], Cr)) {
            throw std::runtime_error("CMAG/CABS/CMSQ require GPR Rd and complex CRs");
        }
        return PackCplxInsn(Kop, Rd, Cr, 0);
    }

    if (Op == "cpack") {
        if (Operands.size() != 3) {
            throw std::runtime_error("CPACK requires CRd, Rs, Rt (real/imag GPRs)");
        }
        uint8_t Rd = 0;
        uint8_t Rs = 0;
        uint8_t Rt = 0;
        if (!TryParseComplexRegister(Operands[0], Rd) ||
            !TryParseRegister(Operands[1], Rs) ||
            !TryParseRegister(Operands[2], Rt)) {
            throw std::runtime_error("CPACK requires CRd and two GPR operands");
        }
        return PackCplxInsn(Opcode::CPACK, Rd, Rs, Rt);
    }

    if (Op == "cextract") {
        if (Operands.size() != 3) {
            throw std::runtime_error("CEXTRACT requires Rd, CRs, #which");
        }
        uint8_t Rd = 0;
        uint8_t Cr = 0;
        if (!TryParseRegister(Operands[0], Rd) || !TryParseComplexRegister(Operands[1], Cr) ||
            !IsNumeric(Operands[2])) {
            throw std::runtime_error("CEXTRACT requires Rd, CRs, #0|#1");
        }
        return PackCplxInsn(Opcode::CEXTRACT, Rd, Cr, 0, CplxProfile::Precision::Fp64, true,
                            ParseImmediateToken(Operands[2]));
    }

    if (Op == "cload" || Op == "cstore") {
        const Opcode Kop = (Op == "cload") ? Opcode::CLOAD : Opcode::CSTORE;
        if (Operands.size() != 2) {
            throw std::runtime_error("CLOAD/CSTORE require CRd, [<addr>]");
        }
        uint8_t Cr = 0;
        if (!TryParseComplexRegister(Operands[0], Cr)) {
            throw std::runtime_error("CLOAD/CSTORE require complex register operand");
        }
        const std::string& Addr = Operands[1];
        if (Addr.size() < 3 || Addr.front() != '[' || Addr.back() != ']') {
            throw std::runtime_error("CLOAD/CSTORE require [<addr>] operand");
        }
        return EncodeInsnMemImm(Kop, Cr, 0, 0,
                                CplxProfile::PackCplxDisp(CplxProfile::Precision::Fp64),
                                Addr, OpmodeMixed);
    }

    throw std::runtime_error("Unknown instruction: " + Mnemonic);
}

void ProcessLine(const std::string& RawLine) {
    std::string Line = Trim(StripComment(RawLine));
    Line.erase(std::remove(Line.begin(), Line.end(), '\r'), Line.end());
    if (Line.empty()) return;

    if (Line == ".text") {
        CurrentSection = SectionKind::Text;
        return;
    }
    if (Line == ".data") {
        CurrentSection = SectionKind::Data;
        return;
    }
    if (Line == ".bss") {
        CurrentSection = SectionKind::Bss;
        return;
    }
    if (Line.rfind(".section", 0) == 0) {
        std::vector<std::string> SecTokens = TokenizeLine(Line);
        if (SecTokens.size() < 3) {
            throw std::runtime_error(".section requires name, flags, and encoding (compact|full)");
        }
        const std::string Encoding = ToLower(SecTokens.back());
        if (Encoding == "compact") {
            AssemblingCompact = true;
        } else if (Encoding == "full") {
            AssemblingCompact = false;
            PadTextTo8();
        } else {
            throw std::runtime_error(".section encoding must be compact or full");
        }
        CurrentSection = SectionKind::Text;
        return;
    }
    if (Line == ".mode compact") {
        AssemblingCompact = true;
        return;
    }
    if (Line == ".mode full") {
        AssemblingCompact = false;
        PadTextTo8();
        return;
    }
    if (Line.rfind(".align", 0) == 0) {
        std::vector<std::string> AlignTokens = TokenizeLine(Line);
        uint64_t Align = 8;
        if (AlignTokens.size() >= 2) {
            Align = ParseImmediateToken(AlignTokens[1]);
        }
        if (Align == 0) {
            throw std::runtime_error(".align requires positive alignment");
        }
        while (ActiveBytes().size() % Align != 0) {
            ActiveBytes().push_back(0);
        }
        return;
    }

    std::string Label;
    size_t Colon = Line.find(':');
    if (Colon != std::string::npos) {
        Label = Trim(Line.substr(0, Colon));
        Line = Trim(Line.substr(Colon + 1));
        if (!Label.empty() && Label[0] != '.') {
            RecordSymbol(Label);
        }
    }
    if (Line.empty()) return;

    std::vector<std::string> Tokens = TokenizeLine(Line);
    if (Tokens.empty()) return;

    if (Tokens[0] == ".quad") {
        if (Tokens.size() < 2) {
            throw std::runtime_error(".quad requires a value");
        }
        const std::string& ValueToken = Tokens[1];
        if (IsNumeric(ValueToken)) {
            const uint64_t Value = ParseImmediateToken(ValueToken);
            const size_t Offset = DataSection.size();
            DataSection.resize(Offset + 8);
            WriteBE64(DataSection.data() + Offset, Value);
            return;
        }
        const size_t Offset = DataSection.size();
        DataSection.resize(Offset + 8);
        WriteBE64(DataSection.data() + Offset, 0);
        Fixup Entry;
        Entry.PatchOffset = Offset;
        Entry.Symbol = ValueToken;
        Entry.Type = Fixup::Kind::AbsoluteImmData;
        Fixups.push_back(Entry);
        return;
    }

    std::string Mnemonic = Tokens[0];
    std::vector<std::string> Operands(Tokens.begin() + 1, Tokens.end());
    if (AssemblingCompact) {
        const uint16_t Word = EncodeCompactMnemonic(Mnemonic, Operands);
        if (Word != 0) {
            EmitCompactWord(Word);
        }
        return;
    }
    EncodedInsn Insn = EncodeMnemonic(Mnemonic, Operands);
    Emit(Insn);
}

void ApplyFixups(std::vector<uint8_t>& Image, size_t FinalTextSectionSize) {
    for (const Fixup& Entry : Fixups) {
        uint64_t Target = ResolveAddress(Entry.Symbol);
        if (Entry.Type == Fixup::Kind::AbsoluteImm) {
            WriteBE64(Image.data() + Entry.PatchOffset, Target);
        } else if (Entry.Type == Fixup::Kind::AbsoluteImmData) {
            WriteBE64(Image.data() + FinalTextSectionSize + Entry.PatchOffset, Target);
        } else if (Entry.Type == Fixup::Kind::CompactBranch5) {
            const int64_t ByteDisp = static_cast<int64_t>(Target) -
                                     static_cast<int64_t>(Entry.PatchOffset);
            const int64_t Slot = ByteDisp / 2;
            if (Slot < -16 || Slot > 15) {
                throw std::runtime_error("Compact branch displacement out of range");
            }
            const uint8_t Imm5 =
                static_cast<uint8_t>(Slot < 0 ? Slot + 32 : static_cast<uint8_t>(Slot));
            const uint16_t Old = ReadBE16(Image.data() + Entry.PatchOffset);
            const uint8_t Opc = static_cast<uint8_t>((Old >> 10) & 0x3F);
            const uint16_t New = static_cast<uint16_t>(
                (static_cast<uint16_t>(Opc) << 10) |
                (static_cast<uint16_t>(Entry.BranchRd & 0x1F) << 5) | Imm5);
            WriteBE16(Image.data() + Entry.PatchOffset, New);
        } else if (Entry.Type == Fixup::Kind::CompactCall10) {
            const auto It = SymbolTable.find(Entry.Symbol);
            const bool ToFull = It != SymbolTable.end() && !It->second.TextCompact;
            const int64_t ByteDisp = static_cast<int64_t>(Target) -
                                     static_cast<int64_t>(Entry.PatchOffset);
            const int64_t Slot = ByteDisp / 2;
            if (Slot < -256 || Slot > 255) {
                throw std::runtime_error("Compact CALL displacement out of range");
            }
            const uint16_t Enc = static_cast<uint16_t>(Slot & 0x1FF);
            WriteBE16(Image.data() + Entry.PatchOffset, CompactProfile::PackCall(Enc, ToFull));
        } else if (Entry.Type == Fixup::Kind::CallTargetImm) {
            WriteBE64(Image.data() + Entry.PatchOffset, ResolveCallTarget(Entry.Symbol));
        } else {
            int64_t Disp = static_cast<int64_t>(Target) -
                           static_cast<int64_t>(Entry.PatchOffset);
            uint64_t Base = ReadBE64(Image.data() + Entry.PatchOffset);
            Opcode Op = static_cast<Opcode>((Base >> 53) & 0x7FF);
            Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64,
                            static_cast<int32_t>(Disp));
            WriteBE64(Image.data() + Entry.PatchOffset, Base);
        }
    }
}

AssemblyImage BuildImage(const std::vector<std::string>& Lines) {
    ResetState();
    for (const auto& Line : Lines) {
        ProcessLine(Line);
    }

    AssemblyImage Image;
    Image.TextSize = TextSection.size();
    Image.Bytes = TextSection;
    Image.Bytes.insert(Image.Bytes.end(), DataSection.begin(), DataSection.end());
    ApplyFixups(Image.Bytes, Image.TextSize);

    auto Start = SymbolTable.find("BootEntry");
    if (Start == SymbolTable.end()) {
        Start = SymbolTable.find("_start");
    }
    if (Start != SymbolTable.end()) {
        Image.EntryOffset =
            Start->second.Offset | (Start->second.TextCompact ? 0ULL : 1ULL);
    }
    return Image;
}

} // namespace

std::vector<uint64_t> AssembleInstructions(const std::vector<std::string>& Lines) {
    AssemblyImage Image = BuildImage(Lines);
    std::vector<uint64_t> Words;
    for (size_t I = 0; I + 8 <= Image.Bytes.size(); I += 8) {
        Words.push_back(ReadBE64(Image.Bytes.data() + I));
    }
    return Words;
}

AssemblyImage AssembleSourceFile(const std::string& InputFile) {
    std::ifstream InFile(InputFile);
    if (!InFile) {
        throw std::runtime_error("Could not open input file: " + InputFile);
    }
    std::vector<std::string> Lines;
    std::string Line;
    while (std::getline(InFile, Line)) {
        Lines.push_back(Line);
    }
    return BuildImage(Lines);
}

void AssembleFile(const std::string& InputFile, const std::string& OutputFileName) {
    AssemblyImage Image = AssembleSourceFile(InputFile);
    std::ofstream OutFile(OutputFileName, std::ios::binary);
    if (!OutFile) {
        throw std::runtime_error("Could not open output file: " + OutputFileName);
    }
    OutFile.write(reinterpret_cast<const char*>(Image.Bytes.data()),
                  static_cast<std::streamsize>(Image.Bytes.size()));
}
