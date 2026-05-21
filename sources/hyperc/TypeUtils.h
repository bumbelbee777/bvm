#pragma once

#include "Ast.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HyperC {

inline bool IsPointerType(const TypeSpec& Type) {
    return Type.PointerDepth > 0;
}

inline TypeSpec PointeeType(TypeSpec Type) {
    if (Type.PointerDepth > 0) {
        --Type.PointerDepth;
    }
    return Type;
}

inline TypeSpec MergePointerDepth(TypeSpec Type, int ExtraDepth) {
    Type.PointerDepth += ExtraDepth;
    return Type;
}

inline bool IsNullPointerExpr(const Expr& Node) {
    if (std::holds_alternative<NullPtrExpr>(Node->Data)) {
        return true;
    }
    if (const auto* Lit = std::get_if<IntLiteralExpr>(&Node->Data)) {
        return Lit->Value == 0;
    }
    return false;
}

inline bool IsFloatingType(TypeKind Kind) {
    return Kind == TypeKind::Float || Kind == TypeKind::Double;
}

inline bool IsFloatingType(const TypeSpec& Type) {
    return Type.PointerDepth == 0 && IsFloatingType(Type.Kind);
}

inline bool IsIntegerType(TypeKind Kind) {
    switch (Kind) {
        case TypeKind::Bool:
        case TypeKind::Char:
        case TypeKind::Short:
        case TypeKind::Int:
        case TypeKind::Signed:
        case TypeKind::Unsigned:
        case TypeKind::Long:
        case TypeKind::Enum:
            return true;
        default:
            return false;
    }
}

inline size_t TypeSize(TypeKind Kind) {
    switch (Kind) {
        case TypeKind::Char:
            return 1;
        case TypeKind::Short:
            return 2;
        case TypeKind::Float:
            return 4;
        case TypeKind::Bool:
        case TypeKind::Int:
        case TypeKind::Signed:
        case TypeKind::Unsigned:
        case TypeKind::Long:
        case TypeKind::Double:
        case TypeKind::Enum:
            return 8;
        case TypeKind::Void:
            return 0;
    }
    return 8;
}

inline size_t TypeSpecSize(const TypeSpec& Type) {
    if (Type.PointerDepth > 0) {
        return 8;
    }
    return TypeSize(Type.Kind);
}

inline TypeKind UnderlyingType(TypeKind Kind) {
    if (Kind == TypeKind::Enum) {
        return TypeKind::Int;
    }
    return Kind;
}

inline uint64_t FloatToBits(float Value) {
    uint32_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

inline uint64_t DoubleToBits(double Value) {
    uint64_t Bits = 0;
    std::memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

inline double BitsToDouble(uint64_t Bits) {
    double Value = 0.0;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

inline float BitsToFloat(uint32_t Bits) {
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

} // namespace HyperC
