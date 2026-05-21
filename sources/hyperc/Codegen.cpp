#include "Codegen.h"
#include "HyperC.h"
#include "TypeUtils.h"

#include "../Isa.h"
#include "../assembler/Assembler.h"

#include <cmath>
#include <stdexcept>
#include <unordered_map>

using namespace Honeycomb;

namespace HyperC {

namespace {

constexpr uint8_t RetReg = 15;
constexpr uint8_t ArgRegBase = 1;
constexpr uint8_t ScratchReg = 10;
constexpr uint8_t BoolReg = 11;
constexpr uint8_t TempReg = 12;
constexpr uint8_t FsScratch0 = 0;
constexpr uint8_t FsScratch1 = 1;
constexpr uint8_t SpReg = static_cast<uint8_t>(Reg::SP);
constexpr uint8_t ZeroReg = static_cast<uint8_t>(Reg::ZERO);
constexpr size_t WordSize = 8;
// Runtime guards for null deref, fixed-size array indices, and div-by-zero.
constexpr bool EnableSafetyChecks = false;
constexpr uint64_t SafetyHaltCode = 0xBADDEADULL;

enum class SymbolKind { Function, Global, Local, Param };

struct SymbolEntry {
    SymbolKind Kind = SymbolKind::Local;
    size_t Offset = 0;
    uint8_t ParamReg = 0;
    TypeSpec Type;
    size_t ArrayLength = 1;
};

size_t AlignUp(size_t Value, size_t Align) {
    return (Value + Align - 1) / Align * Align;
}

size_t BindingElementCount(const VarBinding& Binding) {
    if (Binding.ArrayLength && *Binding.ArrayLength > 0) {
        return static_cast<size_t>(*Binding.ArrayLength);
    }
    if (Binding.Init) {
        if (const auto* InitList = std::get_if<ArrayInitExpr>(&(**Binding.Init).Data)) {
            return InitList->Elements.size();
        }
    }
    return 1;
}

size_t BindingByteSize(const TypeSpec& Type, const VarBinding& Binding) {
    const size_t Count = BindingElementCount(Binding);
    if (Count == 1 && Type.PointerDepth == 0 && Type.Kind == TypeKind::Float) {
        return 8;
    }
    return Count * TypeSpecSize(Type);
}

struct Fixup {
    size_t PatchOffset = 0;
    std::string Label;
    enum class Kind { Branch, CallTarget, AbsoluteImm, DataPointer } Type = Kind::Branch;
};

EncodedInsn EmitAluRr(Opcode Op, uint8_t Rd, uint8_t Rs, uint8_t Rt) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, Rd, Rs, Rt, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EmitAluImm(Opcode Op, uint8_t Rd, uint8_t Rs, uint64_t Imm) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, Rd, Rs, 0, OpmodeInt64, 0);
    Insn.Imm1 = Imm;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EmitLoadImm(uint8_t Rd, uint64_t Imm) {
    return EmitAluImm(Opcode::LOAD, Rd, 0, Imm);
}

EncodedInsn EmitBranch(Opcode Op, int32_t Disp) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64, Disp);
    return Insn;
}

EncodedInsn EmitBranchFar(Opcode Op, uint64_t Target) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, true, false, 0, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = Target;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EmitCmpRegs(uint8_t Rs, uint8_t Rt) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::CMP, false, false, 0, Rs, Rt, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EmitLoadMem(uint8_t Rd, uint8_t BaseReg, int32_t Offset = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::LOAD, false, false, Rd, BaseReg, 0, OpmodeInt64, Offset);
    return Insn;
}

EncodedInsn EmitFsLoadMem(uint8_t Rd, uint8_t BaseReg, int32_t Offset = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FLOAD, false, false, Rd, BaseReg, 0, OpmodeFp64, Offset);
    return Insn;
}

EncodedInsn EmitStoreMem(uint8_t Rs, uint8_t BaseReg, int32_t Offset = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::STORE, false, false, Rs, BaseReg, 0, OpmodeInt64, Offset);
    return Insn;
}

EncodedInsn EmitFsStoreMem(uint8_t Rs, uint8_t BaseReg, int32_t Offset = 0) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FSTORE, false, false, Rs, BaseReg, 0, OpmodeFp64, Offset);
    return Insn;
}

EncodedInsn EmitLoadSpOffset(uint8_t Rd, int64_t Offset) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::LOAD, false, false, Rd, SpReg, 0, OpmodeInt64,
                        static_cast<int32_t>(Offset));
    return Insn;
}

EncodedInsn EmitStoreSpOffset(uint8_t Rs, int64_t Offset) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::STORE, false, false, Rs, SpReg, 0, OpmodeInt64,
                         static_cast<int32_t>(Offset));
    return Insn;
}

EncodedInsn EmitLoadSymbol(uint8_t Rd) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::LOAD, true, false, Rd, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = 0;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EmitStoreSymbol(uint8_t Rs) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::STORE, true, false, Rs, 0, 0, OpmodeInt64, 0);
    Insn.Imm1 = 0;
    Insn.HasImm1 = true;
    return Insn;
}

EncodedInsn EmitSystem(Opcode Op) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EmitRet() { return EmitSystem(Opcode::RET); }

EncodedInsn EmitFsAluRr(Opcode Op, uint8_t Rd, uint8_t Rs, uint8_t Rt) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Op, false, false, Rd, Rs, Rt, OpmodeFp64, 0);
    return Insn;
}

EncodedInsn EmitFtoI(uint8_t Rd, uint8_t FsRs) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FTOI, false, false, Rd, FsRs, 0, OpmodeInt64, 0);
    return Insn;
}

EncodedInsn EmitItoF(uint8_t FsRd, uint8_t GprRs) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::ITOF, false, false, FsRd, GprRs, 0, OpmodeFp64, 0);
    return Insn;
}

EncodedInsn EmitFsLoadSpOffset(uint8_t Rd, int64_t Offset) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FLOAD, false, false, Rd, SpReg, 0, OpmodeFp64,
                         static_cast<int32_t>(Offset));
    return Insn;
}

EncodedInsn EmitFsStoreSpOffset(uint8_t Rs, int64_t Offset) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FSTORE, false, false, Rs, SpReg, 0, OpmodeFp64,
                         static_cast<int32_t>(Offset));
    return Insn;
}

EncodedInsn EmitFsLoadPool(uint8_t Rd) {
    EncodedInsn Insn;
    Insn.Base = PackBase(Opcode::FLOAD, true, false, Rd, 0, 0, OpmodeFp64, 0);
    Insn.Imm1 = 0;
    Insn.HasImm1 = true;
    return Insn;
}

class Codegen {
public:
    explicit Codegen(FutureContext* Future) : Future_(Future) {}

    HyperCImage Build(const Program& Prog) {
        RegisterEnums(Prog);
        ReserveFunctions(Prog);
        EmitData(Prog);
        EmitStart();
        for (const FuncDecl& Fn : Prog.Functions) {
            EmitFunction(Fn);
        }
        return Finalize();
    }

private:
    FutureContext* Future_;
    std::vector<uint8_t> Text_;
    std::vector<uint8_t> Data_;
    std::unordered_map<std::string, SymbolEntry> Globals_;
    std::unordered_map<std::string, SymbolEntry> Locals_;
    std::unordered_map<std::string, int64_t> EnumConstants_;
    std::unordered_map<std::string, size_t> Labels_;
    std::vector<Fixup> Fixups_;
    std::vector<std::string> BreakStack_;
    size_t NextLabel_ = 0;
    size_t NextFrameOffset_ = 0;
    size_t FrameBytes_ = 0;

    void ReserveFunctions(const Program& Prog) {
        for (const FuncDecl& Fn : Prog.Functions) {
            Labels_[Fn.Name] = 0;
        }
    }

    void RegisterEnums(const Program& Prog) {
        for (const EnumDecl& Enum : Prog.Enums) {
            int64_t NextValue = 0;
            for (const EnumField& Field : Enum.Fields) {
                const int64_t Value = Field.Value.value_or(NextValue);
                EnumConstants_[Enum.Name + "::" + Field.Name] = Value;
                NextValue = Value + 1;
            }
        }
    }

    int64_t ResolveEnumField(const EnumFieldExpr& Field) const {
        const std::string Key = Field.EnumName + "::" + Field.FieldName;
        const auto It = EnumConstants_.find(Key);
        if (It == EnumConstants_.end()) {
            throw std::runtime_error("unknown enum constant: " + Key);
        }
        return It->second;
    }

    std::string NewLabel(const char* Prefix) {
        return std::string(Prefix) + std::to_string(NextLabel_++);
    }

    void EmitInsn(const EncodedInsn& Insn) { AppendEncoded(Text_, Insn); }

    void DefineLabel(const std::string& Name) { Labels_[Name] = Text_.size(); }

    void EmitSafetyHalt() {
        EmitInsn(EmitLoadImm(RetReg, SafetyHaltCode));
        EmitInsn(EmitSystem(Opcode::HLT));
    }

    void EmitNullCheck() {
        if (!EnableSafetyChecks) {
            return;
        }
        const std::string OkLabel = NewLabel("nnull");
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        QueueBranchFixup(Opcode::JNE, OkLabel);
        EmitSafetyHalt();
        DefineLabel(OkLabel);
    }

    void EmitBoundsCheck(int64_t Length) {
        if (!EnableSafetyChecks || Length <= 0) {
            return;
        }
        const std::string OkLabel = NewLabel("nbnd");
        const std::string HaltLabel = NewLabel("nbndh");
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        QueueBranchFixup(Opcode::JLT, HaltLabel);
        EmitInsn(EmitLoadImm(TempReg, static_cast<uint64_t>(Length)));
        EmitInsn(EmitCmpRegs(ScratchReg, TempReg));
        QueueBranchFixup(Opcode::JLT, OkLabel);
        DefineLabel(HaltLabel);
        EmitSafetyHalt();
        DefineLabel(OkLabel);
    }

    void EmitDivisorNonZeroCheck() {
        if (!EnableSafetyChecks) {
            return;
        }
        const std::string OkLabel = NewLabel("ndiv");
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        QueueBranchFixup(Opcode::JNE, OkLabel);
        EmitSafetyHalt();
        DefineLabel(OkLabel);
    }

    void QueueBranchFixup(Opcode Op, const std::string& Label) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size();
        Entry.Label = Label;
        Entry.Type = Fixup::Kind::Branch;
        Fixups_.push_back(Entry);
        EmitInsn(EmitBranch(Op, 0));
    }

    void QueueJumpFixup(const std::string& Label) {
        QueueBranchFixup(Opcode::JMP, Label);
    }

    void QueueCallFixup(const std::string& Label) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size() + WordSize;
        Entry.Label = Label;
        Entry.Type = Fixup::Kind::CallTarget;
        Fixups_.push_back(Entry);
        EmitInsn(EmitBranchFar(Opcode::CALL, 0));
    }

    void QueueAddressFixup(const std::string& Name) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size() + WordSize;
        Entry.Label = Name;
        Entry.Type = Fixup::Kind::AbsoluteImm;
        Fixups_.push_back(Entry);
        EmitInsn(EmitLoadImm(ScratchReg, 0));
    }

    void QueueLoadSymbolFixup(const std::string& Name) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size() + WordSize;
        Entry.Label = Name;
        Entry.Type = Fixup::Kind::AbsoluteImm;
        Fixups_.push_back(Entry);
        EmitInsn(EmitLoadSymbol(ScratchReg));
    }

    void QueueStoreSymbolFixup(const std::string& Name) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size() + WordSize;
        Entry.Label = Name;
        Entry.Type = Fixup::Kind::AbsoluteImm;
        Fixups_.push_back(Entry);
        EmitInsn(EmitStoreSymbol(ScratchReg));
    }

    double EvalConstFloat(const Expr& Node) {
        if (const auto* Lit = std::get_if<FloatLiteralExpr>(&Node->Data)) {
            return Lit->Value;
        }
        if (const auto* Lit = std::get_if<IntLiteralExpr>(&Node->Data)) {
            return static_cast<double>(Lit->Value);
        }
        if (const auto* Cast = std::get_if<CastExpr>(&Node->Data)) {
            if (IsFloatingType(Cast->Target.Kind)) {
                return EvalConstFloat(Cast->Inner);
            }
            return static_cast<double>(EvalConstExpr(Cast->Inner));
        }
        if (const auto* Field = std::get_if<EnumFieldExpr>(&Node->Data)) {
            return static_cast<double>(ResolveEnumField(*Field));
        }
        if (const auto* Bin = std::get_if<BinaryExpr>(&Node->Data)) {
            const double L = EvalConstFloat(Bin->Left);
            const double R = EvalConstFloat(Bin->Right);
            switch (Bin->Op) {
                case BinOp::Add: return L + R;
                case BinOp::Sub: return L - R;
                case BinOp::Mul: return L * R;
                case BinOp::Div: return R == 0.0 ? 0.0 : L / R;
                default: break;
            }
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            const double V = EvalConstFloat(Un->Inner);
            return Un->Kind == UnaryExpr::Op::Neg ? -V : V;
        }
        throw std::runtime_error("expected constant float expression");
    }

    int64_t EvalConstExpr(const Expr& Node) {
        if (std::get_if<NullPtrExpr>(&Node->Data)) {
            return 0;
        }
        if (const auto* Lit = std::get_if<IntLiteralExpr>(&Node->Data)) {
            return Lit->Value;
        }
        if (const auto* Lit = std::get_if<FloatLiteralExpr>(&Node->Data)) {
            return static_cast<int64_t>(Lit->Value);
        }
        if (const auto* Cast = std::get_if<CastExpr>(&Node->Data)) {
            if (IsFloatingType(Cast->Target.Kind)) {
                return static_cast<int64_t>(EvalConstFloat(Cast->Inner));
            }
            return EvalConstExpr(Cast->Inner);
        }
        if (const auto* Field = std::get_if<EnumFieldExpr>(&Node->Data)) {
            return ResolveEnumField(*Field);
        }
        if (const auto* Bin = std::get_if<BinaryExpr>(&Node->Data)) {
            const int64_t L = EvalConstExpr(Bin->Left);
            const int64_t R = EvalConstExpr(Bin->Right);
            switch (Bin->Op) {
                case BinOp::Add: return L + R;
                case BinOp::Sub: return L - R;
                case BinOp::Mul: return L * R;
                case BinOp::Div: return R == 0 ? 0 : L / R;
                case BinOp::Mod: return R == 0 ? 0 : L % R;
                case BinOp::BitAnd: return L & R;
                case BinOp::BitOr: return L | R;
                case BinOp::BitXor: return L ^ R;
                case BinOp::Shl: return L << R;
                case BinOp::Shr: return L >> R;
                default: break;
            }
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            const int64_t V = EvalConstExpr(Un->Inner);
            if (Un->Kind == UnaryExpr::Op::Neg) {
                return -V;
            }
            if (Un->Kind == UnaryExpr::Op::BitNot) {
                return ~V;
            }
            return V ? 0 : 1;
        }
        throw std::runtime_error("global initializer must be constant");
    }

    void WriteTypedValue(TypeKind Type, size_t Offset, int64_t IntValue, double FloatValue) {
        switch (Type) {
            case TypeKind::Char: {
                Data_[Offset] = static_cast<uint8_t>(IntValue);
                break;
            }
            case TypeKind::Short: {
                const size_t At = Offset;
                if (Data_.size() < At + 2) {
                    Data_.resize(At + 2);
                }
                const int16_t Value = static_cast<int16_t>(IntValue);
                Data_[At] = static_cast<uint8_t>((Value >> 8) & 0xFF);
                Data_[At + 1] = static_cast<uint8_t>(Value & 0xFF);
                break;
            }
            case TypeKind::Float: {
                const size_t At = Offset;
                if (Data_.size() < At + 4) {
                    Data_.resize(At + 4);
                }
                const uint32_t Bits = static_cast<uint32_t>(FloatToBits(static_cast<float>(FloatValue)));
                Data_[At] = static_cast<uint8_t>((Bits >> 24) & 0xFF);
                Data_[At + 1] = static_cast<uint8_t>((Bits >> 16) & 0xFF);
                Data_[At + 2] = static_cast<uint8_t>((Bits >> 8) & 0xFF);
                Data_[At + 3] = static_cast<uint8_t>(Bits & 0xFF);
                break;
            }
            case TypeKind::Double: {
                const size_t At = Offset;
                Data_.resize(At + WordSize);
                WriteBE64(Data_.data() + At, DoubleToBits(FloatValue));
                break;
            }
            default: {
                const size_t At = Offset;
                Data_.resize(At + WordSize);
                WriteBE64(Data_.data() + At, static_cast<uint64_t>(IntValue));
                break;
            }
        }
    }

    void WriteTypedValue(const TypeSpec& Type, size_t Offset, int64_t IntValue, double FloatValue) {
        if (IsPointerType(Type)) {
            const size_t At = Offset;
            Data_.resize(At + WordSize);
            WriteBE64(Data_.data() + At, static_cast<uint64_t>(IntValue));
            return;
        }
        WriteTypedValue(Type.Kind, Offset, IntValue, FloatValue);
    }

    void EmitGlobalPointerInit(const Expr& Init, size_t Offset) {
        if (const auto* Un = std::get_if<UnaryExpr>(&Init->Data)) {
            if (Un->Kind == UnaryExpr::Op::Addr) {
                if (const auto* Id = std::get_if<IdentExpr>(&Un->Inner->Data)) {
                    if (Globals_.find(Id->Name) != Globals_.end()) {
                        Fixup Entry;
                        Entry.PatchOffset = Offset;
                        Entry.Label = Id->Name;
                        Entry.Type = Fixup::Kind::DataPointer;
                        Fixups_.push_back(Entry);
                        if (Data_.size() < Offset + WordSize) {
                            Data_.resize(Offset + WordSize);
                        }
                        WriteBE64(Data_.data() + Offset, 0);
                        return;
                    }
                }
            }
        }
        WriteTypedValue(TypeSpec{TypeKind::Int, false, std::nullopt, 1}, Offset,
                        EvalConstExpr(Init), 0.0);
    }

    void EmitBindingData(const TypeSpec& Type, const VarBinding& Binding, size_t BaseOffset) {
        const size_t Count = BindingElementCount(Binding);
        const size_t ElemSize = TypeSpecSize(Type);
        if (Binding.Init) {
            if (const auto* InitList = std::get_if<ArrayInitExpr>(&(**Binding.Init).Data)) {
                for (size_t I = 0; I < Count; ++I) {
                    if (I < InitList->Elements.size()) {
                        if (IsFloatingType(Type)) {
                            WriteTypedValue(Type, BaseOffset + I * ElemSize, 0,
                                            EvalConstFloat(InitList->Elements[I]));
                        } else if (IsPointerType(Type)) {
                            EmitGlobalPointerInit(InitList->Elements[I], BaseOffset + I * ElemSize);
                        } else {
                            WriteTypedValue(Type, BaseOffset + I * ElemSize,
                                            EvalConstExpr(InitList->Elements[I]), 0.0);
                        }
                    } else {
                        WriteTypedValue(Type, BaseOffset + I * ElemSize, 0, 0.0);
                    }
                }
                return;
            }
            if (IsFloatingType(Type)) {
                WriteTypedValue(Type, BaseOffset, 0, EvalConstFloat(*Binding.Init));
            } else if (IsPointerType(Type)) {
                EmitGlobalPointerInit(*Binding.Init, BaseOffset);
            } else {
                WriteTypedValue(Type, BaseOffset, EvalConstExpr(*Binding.Init), 0.0);
            }
            return;
        }
        for (size_t I = 0; I < Count; ++I) {
            WriteTypedValue(Type, BaseOffset + I * ElemSize, 0, 0.0);
        }
    }

    void EmitData(const Program& Prog) {
        for (const GlobalDecl& Glob : Prog.Globals) {
            for (const VarBinding& Binding : Glob.Bindings) {
                SymbolEntry Entry;
                Entry.Kind = SymbolKind::Global;
                Entry.Offset = Data_.size();
                Entry.Type = MergePointerDepth(Glob.Type, Binding.PointerDepth);
                Entry.ArrayLength = BindingElementCount(Binding);
                Globals_[Binding.Name] = Entry;
                EmitBindingData(Entry.Type, Binding, Entry.Offset);
                Data_.resize(AlignUp(Data_.size(), WordSize));
            }
        }
    }

    void EmitStart() {
        DefineLabel("_start");
        QueueCallFixup("main");
        EmitInsn(EmitSystem(Opcode::HLT));
    }

    SymbolEntry MakeLocalEntry(const TypeSpec& Type, size_t ArrayLength) {
        SymbolEntry Entry;
        Entry.Kind = SymbolKind::Local;
        Entry.Type = Type;
        Entry.ArrayLength = ArrayLength;
        Entry.Offset = NextFrameOffset_;
        return Entry;
    }

    void PlanBinding(const TypeSpec& Type, const VarBinding& Binding) {
        const TypeSpec Effective = MergePointerDepth(Type, Binding.PointerDepth);
        const size_t Bytes = BindingByteSize(Effective, Binding);
        SymbolEntry Entry = MakeLocalEntry(Effective, BindingElementCount(Binding));
        NextFrameOffset_ += Bytes;
        NextFrameOffset_ = AlignUp(NextFrameOffset_, WordSize);
        Locals_[Binding.Name] = Entry;
    }

    void PlanLocalsInStmt(const Stmt& Node) {
        if (const auto* Decl = std::get_if<VarDeclStmt>(&Node->Data)) {
            for (const VarBinding& Binding : Decl->Bindings) {
                PlanBinding(Decl->Type, Binding);
            }
            return;
        }
        if (const auto* Block = std::get_if<BlockStmt>(&Node->Data)) {
            for (const Stmt& Inner : Block->Body) {
                PlanLocalsInStmt(Inner);
            }
            return;
        }
        if (const auto* If = std::get_if<IfStmt>(&Node->Data)) {
            PlanLocalsInStmt(If->Then);
            if (If->Else) {
                PlanLocalsInStmt(*If->Else);
            }
            return;
        }
        if (const auto* While = std::get_if<WhileStmt>(&Node->Data)) {
            PlanLocalsInStmt(While->Body);
            return;
        }
        if (const auto* For = std::get_if<ForStmt>(&Node->Data)) {
            if (For->Init) {
                PlanLocalsInStmt(*For->Init);
            }
            PlanLocalsInStmt(For->Body);
        }
    }

    void PlanFunctionLocals(const FuncDecl& Fn) {
        NextFrameOffset_ = 0;
        for (const Stmt& S : Fn.Body.Body) {
            PlanLocalsInStmt(S);
        }
        FrameBytes_ = AlignUp(NextFrameOffset_, WordSize);
    }

    SymbolEntry* FindSymbol(const std::string& Name) {
        auto LocalIt = Locals_.find(Name);
        if (LocalIt != Locals_.end()) {
            return &LocalIt->second;
        }
        auto GlobalIt = Globals_.find(Name);
        if (GlobalIt != Globals_.end()) {
            return &GlobalIt->second;
        }
        return nullptr;
    }

    TypeSpec InferLvalueType(const Expr& Node) {
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            if (const SymbolEntry* Sym = FindSymbol(Id->Name)) {
                return Sym->Type;
            }
            TypeSpec Result;
            Result.Kind = TypeKind::Int;
            return Result;
        }
        if (const auto* Index = std::get_if<IndexExpr>(&Node->Data)) {
            TypeSpec BaseType = InferExprType(Index->Base);
            if (BaseType.PointerDepth > 0) {
                return PointeeType(BaseType);
            }
            if (const auto* Id = std::get_if<IdentExpr>(&Index->Base->Data)) {
                if (const SymbolEntry* Sym = FindSymbol(Id->Name)) {
                    if (Sym->ArrayLength > 1) {
                        return Sym->Type;
                    }
                }
            }
            return PointeeType(BaseType);
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                return PointeeType(InferExprType(Un->Inner));
            }
        }
        TypeSpec Result;
        Result.Kind = TypeKind::Int;
        return Result;
    }

    TypeSpec InferExprType(const Expr& Node) {
        TypeSpec Result;
        Result.Kind = TypeKind::Int;
        if (const auto* Lit = std::get_if<FloatLiteralExpr>(&Node->Data)) {
            Result.Kind = Lit->IsFloat ? TypeKind::Float : TypeKind::Double;
            return Result;
        }
        if (std::get_if<IntLiteralExpr>(&Node->Data) || std::get_if<NullPtrExpr>(&Node->Data)) {
            return Result;
        }
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            if (const SymbolEntry* Sym = FindSymbol(Id->Name)) {
                if (Sym->ArrayLength > 1 && Sym->Type.PointerDepth == 0) {
                    TypeSpec Decayed = Sym->Type;
                    ++Decayed.PointerDepth;
                    return Decayed;
                }
                return Sym->Type;
            }
            return Result;
        }
        if (const auto* Field = std::get_if<EnumFieldExpr>(&Node->Data)) {
            (void)Field;
            return Result;
        }
        if (std::get_if<IndexExpr>(&Node->Data)) {
            return InferLvalueType(Node);
        }
        if (const auto* Cast = std::get_if<CastExpr>(&Node->Data)) {
            return Cast->Target;
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                return PointeeType(InferExprType(Un->Inner));
            }
            if (Un->Kind == UnaryExpr::Op::Addr) {
                TypeSpec InnerType = InferLvalueType(Un->Inner);
                ++InnerType.PointerDepth;
                return InnerType;
            }
        }
        if (const auto* Bin = std::get_if<BinaryExpr>(&Node->Data)) {
            const TypeSpec L = InferExprType(Bin->Left);
            const TypeSpec R = InferExprType(Bin->Right);
            if (IsPointerType(L) && IsPointerType(R) && Bin->Op == BinOp::Sub) {
                return Result;
            }
            if (IsPointerType(L) &&
                (Bin->Op == BinOp::Add || Bin->Op == BinOp::Sub)) {
                return L;
            }
            if (IsPointerType(R) && Bin->Op == BinOp::Add) {
                return R;
            }
            if (IsFloatingType(L) || IsFloatingType(R)) {
                Result.Kind = TypeKind::Double;
                return Result;
            }
            return Result;
        }
        if (const auto* Tern = std::get_if<TernaryExpr>(&Node->Data)) {
            const TypeSpec T = InferExprType(Tern->TrueExpr);
            const TypeSpec F = InferExprType(Tern->FalseExpr);
            if (IsFloatingType(T) || IsFloatingType(F)) {
                Result.Kind = TypeKind::Double;
                return Result;
            }
            return Result;
        }
        return Result;
    }

    void EmitLoadFromMemory(const TypeSpec& ValueType, uint8_t BaseReg) {
        if (IsFloatingType(ValueType)) {
            EmitInsn(EmitFsLoadMem(FsScratch0, BaseReg, 0));
            return;
        }
        EmitInsn(EmitLoadMem(ScratchReg, BaseReg, 0));
        if (ValueType.PointerDepth == 0) {
            switch (ValueType.Kind) {
                case TypeKind::Char:
                    EmitInsn(EmitAluImm(Opcode::AND, ScratchReg, ScratchReg, 0xFF));
                    break;
                case TypeKind::Short:
                    EmitInsn(EmitAluImm(Opcode::LSH, ScratchReg, ScratchReg, 48));
                    EmitInsn(EmitAluImm(Opcode::RSH, ScratchReg, ScratchReg, 48));
                    break;
                default:
                    break;
            }
        }
    }

    void EmitStoreToMemory(const TypeSpec& ValueType, uint8_t BaseReg) {
        if (IsFloatingType(ValueType)) {
            EmitInsn(EmitFsStoreMem(FsScratch0, BaseReg, 0));
            return;
        }
        EmitInsn(EmitStoreMem(ScratchReg, BaseReg, 0));
    }

    void EmitIdentAddress(const std::string& Name) {
        SymbolEntry* Sym = FindSymbol(Name);
        if (!Sym) {
            throw std::runtime_error("undefined identifier: " + Name);
        }
        if (Sym->Kind == SymbolKind::Global) {
            QueueAddressFixup(Name);
            return;
        }
        if (Sym->Kind == SymbolKind::Local) {
            EmitInsn(EmitAluImm(Opcode::ADD, ScratchReg, SpReg, Sym->Offset));
            return;
        }
        if (Sym->Kind == SymbolKind::Param) {
            if (Sym->ArrayLength > 1 || IsPointerType(Sym->Type)) {
                EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, Sym->ParamReg, 0));
                return;
            }
        }
        throw std::runtime_error("cannot take address of value: " + Name);
    }

    void EmitLvalueAddress(const Expr& Node) {
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            EmitIdentAddress(Id->Name);
            return;
        }
        if (const auto* Index = std::get_if<IndexExpr>(&Node->Data)) {
            TypeSpec ElementType = InferLvalueType(Node);
            if (const auto* BaseId = std::get_if<IdentExpr>(&Index->Base->Data)) {
                SymbolEntry* Sym = FindSymbol(BaseId->Name);
                if (Sym && Sym->ArrayLength > 1 && Sym->Type.PointerDepth == 0 &&
                    !IsPointerType(Sym->Type)) {
                    EmitExpr(Index->Index);
                    if (EnableSafetyChecks) {
                        EmitBoundsCheck(static_cast<int64_t>(Sym->ArrayLength));
                        EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
                    }
                    if (Sym->ParamReg != 0) {
                        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, Sym->ParamReg, 0));
                    } else if (Sym->Kind == SymbolKind::Global) {
                        QueueAddressFixup(BaseId->Name);
                        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                    } else {
                        EmitInsn(EmitAluImm(Opcode::ADD, TempReg, SpReg, Sym->Offset));
                    }
                    if (EnableSafetyChecks) {
                        EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, BoolReg, 0));
                    }
                    EmitInsn(EmitAluImm(Opcode::MUL, ScratchReg, ScratchReg,
                                        TypeSpecSize(ElementType)));
                    EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, TempReg, ScratchReg));
                    return;
                }
            }
            EmitExpr(Index->Base);
            if (EnableSafetyChecks) {
                EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
            } else {
                EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
            }
            EmitExpr(Index->Index);
            if (EnableSafetyChecks) {
                if (const auto* BaseId = std::get_if<IdentExpr>(&Index->Base->Data)) {
                    if (SymbolEntry* Sym = FindSymbol(BaseId->Name)) {
                        if (Sym->ArrayLength > 1) {
                            EmitBoundsCheck(static_cast<int64_t>(Sym->ArrayLength));
                        }
                    }
                }
                EmitInsn(EmitAluRr(Opcode::COPY, TempReg, BoolReg, 0));
            }
            EmitInsn(EmitAluImm(Opcode::MUL, ScratchReg, ScratchReg, TypeSpecSize(ElementType)));
            EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, TempReg, ScratchReg));
            return;
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                EmitExpr(Un->Inner);
                return;
            }
        }
        throw std::runtime_error("invalid address expression");
    }

    void EmitLocalArrayIndex(const IndexExpr& Index, const SymbolEntry& Sym, bool Store) {
        const TypeSpec ElementType = Sym.Type;
        if (Store) {
            EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
        }
        EmitExpr(Index.Index);
        if (EnableSafetyChecks) {
            EmitBoundsCheck(static_cast<int64_t>(Sym.ArrayLength));
        }
        EmitInsn(EmitAluImm(Opcode::ADD, TempReg, SpReg, Sym.Offset));
        EmitInsn(EmitAluImm(Opcode::MUL, ScratchReg, ScratchReg, TypeSpecSize(ElementType)));
        EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, TempReg, ScratchReg));
        if (Store) {
            EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
            EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, BoolReg, 0));
            EmitStoreToMemory(ElementType, TempReg);
            return;
        }
        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
        EmitLoadFromMemory(ElementType, TempReg);
    }

    void EmitLoadLvalue(const Expr& Node) {
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            LoadIdent(Id->Name);
            return;
        }
        if (std::get_if<IndexExpr>(&Node->Data)) {
            EmitLvalueAddress(Node);
            EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
            EmitLoadFromMemory(InferLvalueType(Node), TempReg);
            return;
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                EmitExpr(Un->Inner);
                EmitNullCheck();
                EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                EmitLoadFromMemory(PointeeType(InferExprType(Un->Inner)), TempReg);
                return;
            }
        }
        throw std::runtime_error("invalid lvalue load");
    }

    void EmitStoreLvalue(const Expr& Node) {
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            StoreScratchToIdent(Id->Name);
            return;
        }
        if (std::get_if<IndexExpr>(&Node->Data)) {
            EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
            EmitLvalueAddress(Node);
            EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
            EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, BoolReg, 0));
            EmitStoreToMemory(InferLvalueType(Node), TempReg);
            return;
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
                EmitExpr(Un->Inner);
                EmitNullCheck();
                EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, BoolReg, 0));
                EmitStoreToMemory(PointeeType(InferExprType(Un->Inner)), TempReg);
                return;
            }
        }
        throw std::runtime_error("invalid lvalue store");
    }

    void EmitArrayAddress(const std::string& Name) {
        EmitIdentAddress(Name);
    }

    void EmitCallArg(const Expr& Arg) {
        if (const auto* Id = std::get_if<IdentExpr>(&Arg->Data)) {
            if (SymbolEntry* Sym = FindSymbol(Id->Name)) {
                if (Sym->ArrayLength > 1 &&
                    (Sym->Kind == SymbolKind::Local || Sym->Kind == SymbolKind::Global)) {
                    EmitArrayAddress(Id->Name);
                    return;
                }
            }
        }
        EmitExpr(Arg);
    }

    void LoadIdent(const std::string& Name) {
        SymbolEntry* Sym = FindSymbol(Name);
        if (!Sym) {
            throw std::runtime_error("undefined identifier: " + Name);
        }
        if (Sym->Kind == SymbolKind::Param) {
            EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, Sym->ParamReg, 0));
            return;
        }
        if (Sym->Kind == SymbolKind::Global) {
            if (IsFloatingType(Sym->Type)) {
                QueueFsLoadSymbolFixup(Name);
            } else {
                QueueLoadSymbolFixup(Name);
            }
            return;
        }
        if (IsFloatingType(Sym->Type)) {
            EmitInsn(EmitFsLoadSpOffset(FsScratch0, static_cast<int64_t>(Sym->Offset)));
            return;
        }
        EmitInsn(EmitLoadSpOffset(ScratchReg, static_cast<int64_t>(Sym->Offset)));
        if (!IsPointerType(Sym->Type)) {
            switch (Sym->Type.Kind) {
                case TypeKind::Char:
                    EmitInsn(EmitAluImm(Opcode::AND, ScratchReg, ScratchReg, 0xFF));
                    break;
                case TypeKind::Short:
                    EmitInsn(EmitAluImm(Opcode::LSH, ScratchReg, ScratchReg, 48));
                    EmitInsn(EmitAluImm(Opcode::RSH, ScratchReg, ScratchReg, 48));
                    break;
                default:
                    break;
            }
        }
    }

    void StoreScratchToIdent(const std::string& Name) {
        SymbolEntry* Sym = FindSymbol(Name);
        if (!Sym) {
            throw std::runtime_error("undefined identifier: " + Name);
        }
        if (Sym->Type.IsConst) {
            throw std::runtime_error("cannot assign to const variable: " + Name);
        }
        if (Sym->Kind == SymbolKind::Param) {
            EmitInsn(EmitAluRr(Opcode::COPY, Sym->ParamReg, ScratchReg, 0));
            return;
        }
        if (Sym->Kind == SymbolKind::Global) {
            QueueStoreSymbolFixup(Name);
            return;
        }
        if (IsFloatingType(Sym->Type)) {
            EmitInsn(EmitFsStoreSpOffset(FsScratch0, static_cast<int64_t>(Sym->Offset)));
            return;
        }
        EmitInsn(EmitStoreSpOffset(ScratchReg, static_cast<int64_t>(Sym->Offset)));
    }

    void EmitCompareBool(BinOp Op) {
        const std::string TrueLabel = NewLabel("cmpt");
        const std::string EndLabel = NewLabel("cmpe");
        const std::string FalseLabel = NewLabel("cmpf");
        EmitInsn(EmitCmpRegs(BoolReg, ScratchReg));
        switch (Op) {
            case BinOp::Eq: QueueBranchFixup(Opcode::JE, TrueLabel); break;
            case BinOp::Ne: QueueBranchFixup(Opcode::JNE, TrueLabel); break;
            case BinOp::Lt: QueueBranchFixup(Opcode::JLT, TrueLabel); break;
            case BinOp::Gt: QueueBranchFixup(Opcode::JGT, TrueLabel); break;
            case BinOp::Le: QueueBranchFixup(Opcode::JGT, FalseLabel); break;
            case BinOp::Ge: QueueBranchFixup(Opcode::JLT, FalseLabel); break;
            default: break;
        }
        if (Op == BinOp::Le || Op == BinOp::Ge) {
            DefineLabel(TrueLabel);
            EmitInsn(EmitLoadImm(ScratchReg, 1));
            QueueJumpFixup(EndLabel);
            DefineLabel(FalseLabel);
            EmitInsn(EmitLoadImm(ScratchReg, 0));
            DefineLabel(EndLabel);
            return;
        }
        QueueJumpFixup(FalseLabel);
        DefineLabel(TrueLabel);
        EmitInsn(EmitLoadImm(ScratchReg, 1));
        QueueJumpFixup(EndLabel);
        DefineLabel(FalseLabel);
        EmitInsn(EmitLoadImm(ScratchReg, 0));
        DefineLabel(EndLabel);
    }

    void QueueFsLoadSymbolFixup(const std::string& Name) {
        Fixup Entry;
        Entry.PatchOffset = Text_.size() + WordSize;
        Entry.Label = Name;
        Entry.Type = Fixup::Kind::AbsoluteImm;
        Fixups_.push_back(Entry);
        EmitInsn(EmitFsLoadPool(FsScratch0));
    }

    std::string EmitDoubleConstant(double Value) {
        const size_t At = Data_.size();
        Data_.resize(At + WordSize);
        WriteBE64(Data_.data() + At, DoubleToBits(Value));
        const std::string Name = "__hc_flt_" + std::to_string(At);
        SymbolEntry Entry;
        Entry.Kind = SymbolKind::Global;
        Entry.Offset = At;
        Entry.Type = TypeSpec{TypeKind::Double, false, std::nullopt, 0};
        Globals_[Name] = Entry;
        return Name;
    }

    void EmitTernary(const TernaryExpr& Tern) {
        EmitExpr(Tern.Cond);
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        const std::string FalseLabel = NewLabel("ternf");
        const std::string EndLabel = NewLabel("terne");
        QueueBranchFixup(Opcode::JE, FalseLabel);
        EmitExpr(Tern.TrueExpr);
        QueueJumpFixup(EndLabel);
        DefineLabel(FalseLabel);
        EmitExpr(Tern.FalseExpr);
        DefineLabel(EndLabel);
    }

    void EmitIntWidthCast(TypeKind Target) {
        switch (Target) {
            case TypeKind::Char:
                EmitInsn(EmitAluImm(Opcode::AND, ScratchReg, ScratchReg, 0xFF));
                break;
            case TypeKind::Short:
                EmitInsn(EmitAluImm(Opcode::LSH, ScratchReg, ScratchReg, 48));
                EmitInsn(EmitAluImm(Opcode::RSH, ScratchReg, ScratchReg, 48));
                break;
            case TypeKind::Bool: {
                const std::string TrueLabel = NewLabel("btrue");
                const std::string EndLabel = NewLabel("bend");
                EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
                QueueBranchFixup(Opcode::JNE, TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 0));
                QueueJumpFixup(EndLabel);
                DefineLabel(TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 1));
                DefineLabel(EndLabel);
                break;
            }
            case TypeKind::Enum:
            case TypeKind::Int:
            case TypeKind::Signed:
            case TypeKind::Unsigned:
            case TypeKind::Long:
                break;
            default:
                break;
        }
    }

    void EmitCast(const CastExpr& Cast) {
        const TypeSpec Target = Cast.Target;
        const TypeSpec Source = InferExprType(Cast.Inner);

        if (IsPointerType(Target)) {
            EmitExpr(Cast.Inner);
            return;
        }

        if (IsFloatingType(Target)) {
            if (IsFloatingType(Source)) {
                EmitExpr(Cast.Inner);
                return;
            }
            if (IsIntegerType(Source.Kind) || IsPointerType(Source)) {
                EmitExpr(Cast.Inner);
                EmitInsn(EmitItoF(FsScratch0, ScratchReg));
                return;
            }
            EmitExpr(Cast.Inner);
            return;
        }

        if (IsIntegerType(Target.Kind) || Target.Kind == TypeKind::Enum) {
            if (IsFloatingType(Source)) {
                EmitExpr(Cast.Inner);
                EmitInsn(EmitFtoI(ScratchReg, FsScratch0));
                EmitIntWidthCast(Target.Kind);
                return;
            }
            EmitExpr(Cast.Inner);
            EmitIntWidthCast(Target.Kind);
            return;
        }

        EmitExpr(Cast.Inner);
    }

    void EmitCondExit(const Expr& Cond, const std::string& ExitLabel) {
        if (const auto* Bin = std::get_if<BinaryExpr>(&Cond->Data)) {
            if (Bin->Op >= BinOp::Eq && Bin->Op <= BinOp::Ge) {
                EmitExpr(Bin->Left);
                EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
                EmitExpr(Bin->Right);
                EmitInsn(EmitCmpRegs(BoolReg, ScratchReg));
                switch (Bin->Op) {
                    case BinOp::Lt: {
                        const std::string ContinueLabel = NewLabel("cond");
                        QueueBranchFixup(Opcode::JLT, ContinueLabel);
                        QueueJumpFixup(ExitLabel);
                        DefineLabel(ContinueLabel);
                        return;
                    }
                    case BinOp::Gt: {
                        const std::string ContinueLabel = NewLabel("cond");
                        QueueBranchFixup(Opcode::JGT, ContinueLabel);
                        QueueJumpFixup(ExitLabel);
                        DefineLabel(ContinueLabel);
                        return;
                    }
                    case BinOp::Le:
                        QueueBranchFixup(Opcode::JGT, ExitLabel);
                        return;
                    case BinOp::Ge:
                        QueueBranchFixup(Opcode::JLT, ExitLabel);
                        return;
                    case BinOp::Eq:
                        QueueBranchFixup(Opcode::JNE, ExitLabel);
                        return;
                    case BinOp::Ne:
                        QueueBranchFixup(Opcode::JE, ExitLabel);
                        return;
                    default:
                        break;
                }
            }
        }
        EmitExpr(Cond);
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        QueueBranchFixup(Opcode::JE, ExitLabel);
    }

    void EmitFloatBinary(const BinaryExpr& Bin) {
        EmitExpr(Bin.Left);
        EmitInsn(EmitFsAluRr(Opcode::FCOPY, FsScratch1, FsScratch0, 0));
        EmitExpr(Bin.Right);
        switch (Bin.Op) {
            case BinOp::Add:
                EmitInsn(EmitFsAluRr(Opcode::FADD, FsScratch0, FsScratch1, FsScratch0));
                break;
            case BinOp::Sub:
                EmitInsn(EmitFsAluRr(Opcode::FSUB, FsScratch0, FsScratch1, FsScratch0));
                break;
            case BinOp::Mul:
                EmitInsn(EmitFsAluRr(Opcode::FMUL, FsScratch0, FsScratch1, FsScratch0));
                break;
            case BinOp::Div:
                EmitInsn(EmitFsAluRr(Opcode::FDIV, FsScratch0, FsScratch1, FsScratch0));
                break;
            default:
                throw std::runtime_error("unsupported float binary operator");
        }
    }

    void EmitPointerArithmetic(const BinaryExpr& Bin, const TypeSpec& PtrType, bool PtrOnLeft) {
        const TypeSpec Pointee = PointeeType(PtrType);
        const size_t Stride = TypeSpecSize(Pointee);
        const Expr& PtrExpr = PtrOnLeft ? Bin.Left : Bin.Right;
        const Expr& OffExpr = PtrOnLeft ? Bin.Right : Bin.Left;
        EmitExpr(PtrExpr);
        if (const auto* Lit = std::get_if<IntLiteralExpr>(&OffExpr->Data)) {
            const int64_t Scaled =
                static_cast<int64_t>(Lit->Value) * static_cast<int64_t>(Stride);
            if (Bin.Op == BinOp::Add) {
                EmitInsn(EmitAluImm(Opcode::ADD, ScratchReg, ScratchReg,
                                    static_cast<uint64_t>(Scaled)));
            } else if (Bin.Op == BinOp::Sub) {
                EmitInsn(EmitAluImm(Opcode::SUB, ScratchReg, ScratchReg,
                                    static_cast<uint64_t>(Scaled)));
            } else {
                throw std::runtime_error("unsupported pointer binary operator");
            }
            return;
        }
        EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
        EmitExpr(OffExpr);
        EmitInsn(EmitAluImm(Opcode::MUL, ScratchReg, ScratchReg, Stride));
        if (Bin.Op == BinOp::Add) {
            EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, BoolReg, ScratchReg));
        } else if (Bin.Op == BinOp::Sub) {
            EmitInsn(EmitAluRr(Opcode::SUB, ScratchReg, BoolReg, ScratchReg));
        } else {
            throw std::runtime_error("unsupported pointer binary operator");
        }
    }

    void EmitPointerDifference(const BinaryExpr& Bin, const TypeSpec& PtrType) {
        const size_t Stride = TypeSpecSize(PointeeType(PtrType));
        EmitExpr(Bin.Left);
        EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
        EmitExpr(Bin.Right);
        EmitInsn(EmitAluRr(Opcode::SUB, ScratchReg, BoolReg, ScratchReg));
        if (Stride > 1) {
            EmitInsn(EmitAluImm(Opcode::DIV, ScratchReg, ScratchReg, Stride));
        }
    }

    void EmitBinary(const BinaryExpr& Bin) {
        TypeSpec PointerLeftType;
        if (const auto* Id = std::get_if<IdentExpr>(&Bin.Left->Data)) {
            if (const SymbolEntry* Sym = FindSymbol(Id->Name)) {
                if (IsPointerType(Sym->Type)) {
                    PointerLeftType = Sym->Type;
                }
            }
        }
        if (!IsPointerType(PointerLeftType)) {
            PointerLeftType = InferExprType(Bin.Left);
        }
        const TypeSpec RightType = InferExprType(Bin.Right);
        if (IsPointerType(PointerLeftType) && IsPointerType(RightType)) {
            if (Bin.Op == BinOp::Sub) {
                EmitPointerDifference(Bin, PointerLeftType);
                return;
            }
            if (Bin.Op == BinOp::Add) {
                throw std::runtime_error("invalid operands to pointer addition");
            }
        }
        if (IsPointerType(PointerLeftType) && (Bin.Op == BinOp::Add || Bin.Op == BinOp::Sub)) {
            EmitPointerArithmetic(Bin, PointerLeftType, true);
            return;
        }
        if (IsPointerType(RightType) && Bin.Op == BinOp::Add) {
            EmitPointerArithmetic(Bin, RightType, false);
            return;
        }
        if (IsFloatingType(PointerLeftType) || IsFloatingType(RightType)) {
            EmitFloatBinary(Bin);
            return;
        }
        if (Bin.Op >= BinOp::Eq && Bin.Op <= BinOp::Ge) {
            EmitExpr(Bin.Left);
            EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
            EmitExpr(Bin.Right);
            EmitCompareBool(Bin.Op);
            return;
        }
        EmitExpr(Bin.Left);
        EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
        EmitExpr(Bin.Right);
        switch (Bin.Op) {
            case BinOp::Add:
                EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::Sub:
                EmitInsn(EmitAluRr(Opcode::SUB, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::Mul:
                EmitInsn(EmitAluRr(Opcode::MUL, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::Div:
                if (const auto* Lit = std::get_if<IntLiteralExpr>(&Bin.Right->Data)) {
                    if (Lit->Value == 0) {
                        throw std::runtime_error("division by zero");
                    }
                    EmitInsn(EmitAluImm(Opcode::DIV, ScratchReg, BoolReg,
                                        static_cast<uint64_t>(Lit->Value)));
                } else {
                    EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                    EmitDivisorNonZeroCheck();
                    EmitInsn(EmitAluRr(Opcode::DIV, ScratchReg, BoolReg, TempReg));
                }
                break;
            case BinOp::Mod:
                if (const auto* Lit = std::get_if<IntLiteralExpr>(&Bin.Right->Data)) {
                    if (Lit->Value == 0) {
                        throw std::runtime_error("division by zero");
                    }
                    EmitInsn(EmitAluImm(Opcode::MOD, ScratchReg, BoolReg,
                                        static_cast<uint64_t>(Lit->Value)));
                } else {
                    EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                    EmitDivisorNonZeroCheck();
                    EmitInsn(EmitAluRr(Opcode::MOD, ScratchReg, BoolReg, TempReg));
                }
                break;
            case BinOp::BitAnd:
                EmitInsn(EmitAluRr(Opcode::AND, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::BitOr:
                EmitInsn(EmitAluRr(Opcode::OR, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::BitXor:
                EmitInsn(EmitAluRr(Opcode::XOR, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::Shl:
                EmitInsn(EmitAluRr(Opcode::LSH, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::Shr:
                EmitInsn(EmitAluRr(Opcode::RSH, ScratchReg, BoolReg, ScratchReg));
                break;
            case BinOp::And: {
                const std::string FalseLabel = NewLabel("andf");
                const std::string EndLabel = NewLabel("ande");
                EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
                QueueBranchFixup(Opcode::JE, FalseLabel);
                EmitInsn(EmitCmpRegs(BoolReg, ZeroReg));
                QueueBranchFixup(Opcode::JE, FalseLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 1));
                QueueJumpFixup(EndLabel);
                DefineLabel(FalseLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 0));
                DefineLabel(EndLabel);
                break;
            }
            case BinOp::Or: {
                const std::string TrueLabel = NewLabel("ort");
                const std::string EndLabel = NewLabel("ore");
                EmitInsn(EmitCmpRegs(BoolReg, ZeroReg));
                QueueBranchFixup(Opcode::JNE, TrueLabel);
                EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
                QueueBranchFixup(Opcode::JNE, TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 0));
                QueueJumpFixup(EndLabel);
                DefineLabel(TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 1));
                DefineLabel(EndLabel);
                break;
            }
            default: break;
        }
    }

    void EmitCall(const CallExpr& Call) {
        if (Call.Args.size() > 6) {
            throw std::runtime_error("more than six arguments not supported in M1");
        }
        for (size_t I = 0; I < Call.Args.size(); ++I) {
            EmitCallArg(Call.Args[I]);
            EmitInsn(EmitAluRr(Opcode::COPY, static_cast<uint8_t>(ArgRegBase + I), ScratchReg, 0));
        }
        QueueCallFixup(Call.Callee);
        EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, RetReg, 0));
    }

    void EmitExpr(const Expr& Node) {
        if (const auto* Lit = std::get_if<IntLiteralExpr>(&Node->Data)) {
            EmitInsn(EmitLoadImm(ScratchReg, static_cast<uint64_t>(Lit->Value)));
            return;
        }
        if (std::get_if<NullPtrExpr>(&Node->Data)) {
            EmitInsn(EmitLoadImm(ScratchReg, 0));
            return;
        }
        if (const auto* Lit = std::get_if<FloatLiteralExpr>(&Node->Data)) {
            const std::string Pool = EmitDoubleConstant(Lit->Value);
            QueueFsLoadSymbolFixup(Pool);
            return;
        }
        if (const auto* Id = std::get_if<IdentExpr>(&Node->Data)) {
            if (SymbolEntry* Sym = FindSymbol(Id->Name)) {
                if (Sym->ArrayLength > 1 && Sym->Type.PointerDepth == 0) {
                    if (Sym->Kind == SymbolKind::Param) {
                        EmitInsn(EmitAluRr(Opcode::COPY, ScratchReg, Sym->ParamReg, 0));
                    } else {
                        EmitArrayAddress(Id->Name);
                    }
                    return;
                }
            }
            LoadIdent(Id->Name);
            return;
        }
        if (const auto* Field = std::get_if<EnumFieldExpr>(&Node->Data)) {
            EmitInsn(EmitLoadImm(ScratchReg, static_cast<uint64_t>(ResolveEnumField(*Field))));
            return;
        }
        if (std::get_if<IndexExpr>(&Node->Data)) {
            EmitLoadLvalue(Node);
            return;
        }
        if (const auto* Call = std::get_if<CallExpr>(&Node->Data)) {
            EmitCall(*Call);
            return;
        }
        if (const auto* Cast = std::get_if<CastExpr>(&Node->Data)) {
            EmitCast(*Cast);
            return;
        }
        if (const auto* Tern = std::get_if<TernaryExpr>(&Node->Data)) {
            EmitTernary(*Tern);
            return;
        }
        if (const auto* Un = std::get_if<UnaryExpr>(&Node->Data)) {
            if (Un->Kind == UnaryExpr::Op::Deref) {
                EmitExpr(Un->Inner);
                EmitNullCheck();
                EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                EmitLoadFromMemory(PointeeType(InferExprType(Un->Inner)), TempReg);
                return;
            }
            if (Un->Kind == UnaryExpr::Op::Addr) {
                EmitLvalueAddress(Un->Inner);
                return;
            }
            EmitExpr(Un->Inner);
            if (Un->Kind == UnaryExpr::Op::Neg) {
                EmitInsn(EmitAluRr(Opcode::SUB, ScratchReg, ZeroReg, ScratchReg));
            } else if (Un->Kind == UnaryExpr::Op::BitNot) {
                EmitInsn(EmitAluRr(Opcode::NOT, ScratchReg, ScratchReg, 0));
            } else {
                const std::string TrueLabel = NewLabel("nott");
                const std::string EndLabel = NewLabel("note");
                EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
                QueueBranchFixup(Opcode::JE, TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 0));
                QueueJumpFixup(EndLabel);
                DefineLabel(TrueLabel);
                EmitInsn(EmitLoadImm(ScratchReg, 1));
                DefineLabel(EndLabel);
            }
            return;
        }
        if (const auto* Bin = std::get_if<BinaryExpr>(&Node->Data)) {
            EmitBinary(*Bin);
            return;
        }
        if (const auto* Assign = std::get_if<AssignExpr>(&Node->Data)) {
            if (Assign->CompoundOp) {
                EmitLoadLvalue(Assign->Target);
                EmitInsn(EmitAluRr(Opcode::COPY, BoolReg, ScratchReg, 0));
                EmitExpr(Assign->Value);
                if (*Assign->CompoundOp == BinOp::Div || *Assign->CompoundOp == BinOp::Mod) {
                    if (const auto* Lit = std::get_if<IntLiteralExpr>(&Assign->Value->Data)) {
                        if (Lit->Value == 0) {
                            throw std::runtime_error("division by zero");
                        }
                        const Opcode Op = *Assign->CompoundOp == BinOp::Div ? Opcode::DIV : Opcode::MOD;
                        EmitInsn(EmitAluImm(Op, ScratchReg, BoolReg, static_cast<uint64_t>(Lit->Value)));
                        EmitStoreLvalue(Assign->Target);
                        return;
                    }
                }
                switch (*Assign->CompoundOp) {
                    case BinOp::Add:
                        EmitInsn(EmitAluRr(Opcode::ADD, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::Sub:
                        EmitInsn(EmitAluRr(Opcode::SUB, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::Mul:
                        EmitInsn(EmitAluRr(Opcode::MUL, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::Div:
                        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                        EmitDivisorNonZeroCheck();
                        EmitInsn(EmitAluRr(Opcode::DIV, ScratchReg, BoolReg, TempReg));
                        break;
                    case BinOp::Mod:
                        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));
                        EmitDivisorNonZeroCheck();
                        EmitInsn(EmitAluRr(Opcode::MOD, ScratchReg, BoolReg, TempReg));
                        break;
                    case BinOp::BitAnd:
                        EmitInsn(EmitAluRr(Opcode::AND, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::BitOr:
                        EmitInsn(EmitAluRr(Opcode::OR, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::BitXor:
                        EmitInsn(EmitAluRr(Opcode::XOR, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::Shl:
                        EmitInsn(EmitAluRr(Opcode::LSH, ScratchReg, BoolReg, ScratchReg));
                        break;
                    case BinOp::Shr:
                        EmitInsn(EmitAluRr(Opcode::RSH, ScratchReg, BoolReg, ScratchReg));
                        break;
                    default:
                        throw std::runtime_error("unsupported compound assignment operator");
                }
            } else {
                EmitExpr(Assign->Value);
            }
            EmitStoreLvalue(Assign->Target);
        }
    }

    void EmitAsm(const AsmStmt& Node) {
        if (Node.Lines.empty()) {
            return;
        }
        try {
            const std::vector<uint64_t> Words = AssembleInstructions(Node.Lines);
            for (uint64_t Word : Words) {
                const size_t At = Text_.size();
                Text_.resize(At + WordSize);
                WriteBE64(Text_.data() + At, Word);
            }
        } catch (const std::exception& Ex) {
            throw std::runtime_error(std::string("inline asm failed: ") + Ex.what());
        }
    }

    void EmitWhile(const WhileStmt& Node) {
        const std::string TopLabel = NewLabel("whl");
        const std::string EndLabel = NewLabel("whe");
        DefineLabel(TopLabel);
        EmitCondExit(Node.Cond, EndLabel);
        EmitStmt(Node.Body);
        QueueJumpFixup(TopLabel);
        DefineLabel(EndLabel);
    }

    void EmitFor(const ForStmt& Node) {
        if (Node.Init) {
            EmitStmt(*Node.Init);
        }
        const std::string TopLabel = NewLabel("fort");
        const std::string EndLabel = NewLabel("fore");
        DefineLabel(TopLabel);
        if (Node.Cond) {
            EmitCondExit(*Node.Cond, EndLabel);
        }
        EmitStmt(Node.Body);
        if (Node.Step) {
            EmitExpr(*Node.Step);
        }
        QueueJumpFixup(TopLabel);
        DefineLabel(EndLabel);
    }

    void EmitIf(const IfStmt& Node) {
        const std::string ElseLabel = NewLabel("else");
        const std::string EndLabel = NewLabel("ifend");
        EmitExpr(Node.Cond);
        EmitInsn(EmitCmpRegs(ScratchReg, ZeroReg));
        QueueBranchFixup(Opcode::JE, Node.Else ? ElseLabel : EndLabel);
        EmitStmt(Node.Then);
        if (Node.Else) {
            QueueJumpFixup(EndLabel);
            DefineLabel(ElseLabel);
            EmitStmt(*Node.Else);
            DefineLabel(EndLabel);
        } else {
            DefineLabel(EndLabel);
        }
    }

    void EmitSwitch(const SwitchStmt& Node) {
        EmitExpr(Node.Discriminant);
        EmitInsn(EmitAluRr(Opcode::COPY, TempReg, ScratchReg, 0));

        const std::string EndLabel = NewLabel("swend");
        BreakStack_.push_back(EndLabel);

        std::vector<std::string> CaseLabels;
        CaseLabels.reserve(Node.Cases.size());
        for (size_t I = 0; I < Node.Cases.size(); ++I) {
            CaseLabels.push_back(NewLabel("swcase"));
        }

        std::optional<std::string> DefaultLabel;
        for (size_t I = 0; I < Node.Cases.size(); ++I) {
            const SwitchCaseGroup& Group = Node.Cases[I];
            bool IsDefault = false;
            for (const std::optional<int64_t>& Value : Group.Values) {
                if (!Value) {
                    IsDefault = true;
                    DefaultLabel = CaseLabels[I];
                    continue;
                }
                EmitInsn(EmitLoadImm(ScratchReg, static_cast<uint64_t>(*Value)));
                EmitInsn(EmitCmpRegs(TempReg, ScratchReg));
                QueueBranchFixup(Opcode::JE, CaseLabels[I]);
            }
            if (IsDefault) {
                DefaultLabel = CaseLabels[I];
            }
        }

        if (DefaultLabel) {
            QueueJumpFixup(*DefaultLabel);
        } else {
            QueueJumpFixup(EndLabel);
        }

        for (size_t I = 0; I < Node.Cases.size(); ++I) {
            DefineLabel(CaseLabels[I]);
            for (const Stmt& S : Node.Cases[I].Body) {
                EmitStmt(S);
            }
        }

        DefineLabel(EndLabel);
        BreakStack_.pop_back();
    }

    void StoreToOffset(const SymbolEntry& Sym, size_t ElementIndex) {
        const int32_t Offset =
            static_cast<int32_t>(Sym.Offset + ElementIndex * TypeSpecSize(Sym.Type));
        if (IsFloatingType(Sym.Type)) {
            EmitInsn(EmitFsStoreSpOffset(FsScratch0, Offset));
            return;
        }
        EmitInsn(EmitStoreSpOffset(ScratchReg, Offset));
    }

    void EmitStmt(const Stmt& Node) {
        if (const auto* Decl = std::get_if<VarDeclStmt>(&Node->Data)) {
            for (const VarBinding& Binding : Decl->Bindings) {
                if (!Binding.Init) {
                    continue;
                }
                SymbolEntry* Sym = FindSymbol(Binding.Name);
                if (!Sym) {
                    throw std::runtime_error("internal error: undeclared binding " + Binding.Name);
                }
                if (const auto* InitList = std::get_if<ArrayInitExpr>(&(**Binding.Init).Data)) {
                    for (size_t I = 0; I < InitList->Elements.size(); ++I) {
                        EmitExpr(InitList->Elements[I]);
                        StoreToOffset(*Sym, I);
                    }
                    continue;
                }
                EmitExpr(*Binding.Init);
                StoreToOffset(*Sym, 0);
            }
            return;
        }
        if (const auto* Ret = std::get_if<ReturnStmt>(&Node->Data)) {
            if (Ret->Value) {
                EmitExpr(*Ret->Value);
                EmitInsn(EmitAluRr(Opcode::COPY, RetReg, ScratchReg, 0));
            }
            return;
        }
        if (const auto* ExprS = std::get_if<ExprStmt>(&Node->Data)) {
            EmitExpr(ExprS->Value);
            return;
        }
        if (const auto* If = std::get_if<IfStmt>(&Node->Data)) {
            EmitIf(*If);
            return;
        }
        if (const auto* While = std::get_if<WhileStmt>(&Node->Data)) {
            EmitWhile(*While);
            return;
        }
        if (const auto* For = std::get_if<ForStmt>(&Node->Data)) {
            EmitFor(*For);
            return;
        }
        if (const auto* Asm = std::get_if<AsmStmt>(&Node->Data)) {
            EmitAsm(*Asm);
            return;
        }
        if (const auto* Sw = std::get_if<SwitchStmt>(&Node->Data)) {
            EmitSwitch(*Sw);
            return;
        }
        if (std::get_if<BreakStmt>(&Node->Data)) {
            if (BreakStack_.empty()) {
                throw std::runtime_error("break statement outside of switch");
            }
            QueueJumpFixup(BreakStack_.back());
            return;
        }
        if (const auto* Block = std::get_if<BlockStmt>(&Node->Data)) {
            for (const Stmt& Inner : Block->Body) {
                EmitStmt(Inner);
            }
        }
    }

    void EmitFunction(const FuncDecl& Fn) {
        Locals_.clear();
        NextFrameOffset_ = 0;
        FrameBytes_ = 0;
        for (size_t I = 0; I < Fn.Params.size(); ++I) {
            SymbolEntry Entry;
            Entry.Kind = SymbolKind::Param;
            Entry.ParamReg = static_cast<uint8_t>(ArgRegBase + I);
            Entry.Type = Fn.Params[I].Type;
            Entry.ArrayLength = Fn.Params[I].ArrayLength.value_or(1);
            Locals_[Fn.Params[I].Name] = Entry;
        }
        PlanFunctionLocals(Fn);
        DefineLabel(Fn.Name);
        if (Future_) {
            (void)Future_->Overloads;
        }
        if (FrameBytes_ != 0) {
            EmitInsn(EmitAluImm(Opcode::SUB, SpReg, SpReg, FrameBytes_));
        }
        for (const Stmt& S : Fn.Body.Body) {
            EmitStmt(S);
        }
        if (FrameBytes_ != 0) {
            EmitInsn(EmitAluImm(Opcode::ADD, SpReg, SpReg, FrameBytes_));
        }
        if (Fn.ReturnType.Kind == TypeKind::Void) {
            EmitInsn(EmitLoadImm(RetReg, 0));
        }
        EmitInsn(EmitRet());
    }

    uint64_t ResolveLabel(const std::string& Name) const {
        auto It = Labels_.find(Name);
        if (It == Labels_.end()) {
            throw std::runtime_error("undefined label: " + Name);
        }
        return It->second;
    }

    uint64_t ResolveCallTarget(const std::string& Name) const {
        return ResolveLabel(Name) | 1ULL;
    }

    uint64_t ResolveDataSymbol(const std::string& Name) const {
        auto It = Globals_.find(Name);
        if (It == Globals_.end()) {
            throw std::runtime_error("undefined global: " + Name);
        }
        return Text_.size() + It->second.Offset;
    }

    void ApplyFixups() {
        for (const Fixup& Entry : Fixups_) {
            if (Entry.Type == Fixup::Kind::AbsoluteImm) {
                WriteBE64(Text_.data() + Entry.PatchOffset, ResolveDataSymbol(Entry.Label));
            } else if (Entry.Type == Fixup::Kind::DataPointer) {
                WriteBE64(Data_.data() + Entry.PatchOffset, ResolveDataSymbol(Entry.Label));
            } else if (Entry.Type == Fixup::Kind::CallTarget) {
                WriteBE64(Text_.data() + Entry.PatchOffset, ResolveCallTarget(Entry.Label));
            } else {
                const int64_t Disp = static_cast<int64_t>(ResolveLabel(Entry.Label)) -
                                     static_cast<int64_t>(Entry.PatchOffset);
                uint64_t Base = ReadBE64(Text_.data() + Entry.PatchOffset);
                const Opcode Op = static_cast<Opcode>((Base >> 53) & 0x7FF);
                Base = PackBase(Op, false, false, 0, 0, 0, OpmodeInt64, static_cast<int32_t>(Disp));
                WriteBE64(Text_.data() + Entry.PatchOffset, Base);
            }
        }
    }

    HyperCImage Finalize() {
        while (Text_.size() % WordSize != 0) {
            Text_.push_back(0);
        }
        ApplyFixups();
        HyperCImage Image;
        Image.TextSize = Text_.size();
        Image.Bytes = Text_;
        Image.Bytes.insert(Image.Bytes.end(), Data_.begin(), Data_.end());
        Image.EntryOffset = ResolveCallTarget("_start");
        return Image;
    }
};

} // namespace

HyperCImage CompileProgram(const Program& Prog, FutureContext* Future) {
    Codegen Gen(Future);
    return Gen.Build(Prog);
}

} // namespace HyperC
