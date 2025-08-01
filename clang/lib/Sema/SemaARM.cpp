//===------ SemaARM.cpp ---------- ARM target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to ARM.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaARM.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

namespace clang {

SemaARM::SemaARM(Sema &S) : SemaBase(S) {}

/// BuiltinARMMemoryTaggingCall - Handle calls of memory tagging extensions
bool SemaARM::BuiltinARMMemoryTaggingCall(unsigned BuiltinID,
                                          CallExpr *TheCall) {
  ASTContext &Context = getASTContext();

  if (BuiltinID == AArch64::BI__builtin_arm_irg) {
    if (SemaRef.checkArgCount(TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = SemaRef.DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    ExprResult SecArg = SemaRef.DefaultLvalueConversion(Arg1);
    if (SecArg.isInvalid())
      return true;
    QualType SecArgType = SecArg.get()->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_addg) {
    if (SemaRef.checkArgCount(TheCall, 2))
      return true;

    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = SemaRef.DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    TheCall->setType(FirstArgType);

    // Second arg must be an constant in range [0,15]
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_gmi) {
    if (SemaRef.checkArgCount(TheCall, 2))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    Expr *Arg1 = TheCall->getArg(1);

    ExprResult FirstArg = SemaRef.DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;
    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();

    QualType SecArgType = Arg1->getType();
    if (!SecArgType->isIntegerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_integer)
             << "second" << SecArgType << Arg1->getSourceRange();
    TheCall->setType(Context.IntTy);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg) {
    if (SemaRef.checkArgCount(TheCall, 1))
      return true;
    Expr *Arg0 = TheCall->getArg(0);
    ExprResult FirstArg = SemaRef.DefaultFunctionArrayLvalueConversion(Arg0);
    if (FirstArg.isInvalid())
      return true;

    QualType FirstArgType = FirstArg.get()->getType();
    if (!FirstArgType->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_must_be_pointer)
             << "first" << FirstArgType << Arg0->getSourceRange();
    TheCall->setArg(0, FirstArg.get());

    // Derive the return type from the pointer argument.
    if (BuiltinID == AArch64::BI__builtin_arm_ldg)
      TheCall->setType(FirstArgType);
    return false;
  }

  if (BuiltinID == AArch64::BI__builtin_arm_subp) {
    Expr *ArgA = TheCall->getArg(0);
    Expr *ArgB = TheCall->getArg(1);

    ExprResult ArgExprA = SemaRef.DefaultFunctionArrayLvalueConversion(ArgA);
    ExprResult ArgExprB = SemaRef.DefaultFunctionArrayLvalueConversion(ArgB);

    if (ArgExprA.isInvalid() || ArgExprB.isInvalid())
      return true;

    QualType ArgTypeA = ArgExprA.get()->getType();
    QualType ArgTypeB = ArgExprB.get()->getType();

    auto isNull = [&](Expr *E) -> bool {
      return E->isNullPointerConstant(Context,
                                      Expr::NPC_ValueDependentIsNotNull);
    };

    // argument should be either a pointer or null
    if (!ArgTypeA->isAnyPointerType() && !isNull(ArgA))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "first" << ArgTypeA << ArgA->getSourceRange();

    if (!ArgTypeB->isAnyPointerType() && !isNull(ArgB))
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_arg_null_or_pointer)
             << "second" << ArgTypeB << ArgB->getSourceRange();

    // Ensure Pointee types are compatible
    if (ArgTypeA->isAnyPointerType() && !isNull(ArgA) &&
        ArgTypeB->isAnyPointerType() && !isNull(ArgB)) {
      QualType pointeeA = ArgTypeA->getPointeeType();
      QualType pointeeB = ArgTypeB->getPointeeType();
      if (!Context.typesAreCompatible(
              Context.getCanonicalType(pointeeA).getUnqualifiedType(),
              Context.getCanonicalType(pointeeB).getUnqualifiedType())) {
        return Diag(TheCall->getBeginLoc(),
                    diag::err_typecheck_sub_ptr_compatible)
               << ArgTypeA << ArgTypeB << ArgA->getSourceRange()
               << ArgB->getSourceRange();
      }
    }

    // at least one argument should be pointer type
    if (!ArgTypeA->isAnyPointerType() && !ArgTypeB->isAnyPointerType())
      return Diag(TheCall->getBeginLoc(), diag::err_memtag_any2arg_pointer)
             << ArgTypeA << ArgTypeB << ArgA->getSourceRange();

    if (isNull(ArgA)) // adopt type of the other pointer
      ArgExprA =
          SemaRef.ImpCastExprToType(ArgExprA.get(), ArgTypeB, CK_NullToPointer);

    if (isNull(ArgB))
      ArgExprB =
          SemaRef.ImpCastExprToType(ArgExprB.get(), ArgTypeA, CK_NullToPointer);

    TheCall->setArg(0, ArgExprA.get());
    TheCall->setArg(1, ArgExprB.get());
    TheCall->setType(Context.LongLongTy);
    return false;
  }
  assert(false && "Unhandled ARM MTE intrinsic");
  return true;
}

/// BuiltinARMSpecialReg - Handle a check if argument ArgNum of CallExpr
/// TheCall is an ARM/AArch64 special register string literal.
bool SemaARM::BuiltinARMSpecialReg(unsigned BuiltinID, CallExpr *TheCall,
                                   int ArgNum, unsigned ExpectedFieldNum,
                                   bool AllowName) {
  bool IsARMBuiltin = BuiltinID == ARM::BI__builtin_arm_rsr64 ||
                      BuiltinID == ARM::BI__builtin_arm_wsr64 ||
                      BuiltinID == ARM::BI__builtin_arm_rsr ||
                      BuiltinID == ARM::BI__builtin_arm_rsrp ||
                      BuiltinID == ARM::BI__builtin_arm_wsr ||
                      BuiltinID == ARM::BI__builtin_arm_wsrp;
  bool IsAArch64Builtin = BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr128 ||
                          BuiltinID == AArch64::BI__builtin_arm_rsr ||
                          BuiltinID == AArch64::BI__builtin_arm_rsrp ||
                          BuiltinID == AArch64::BI__builtin_arm_wsr ||
                          BuiltinID == AArch64::BI__builtin_arm_wsrp;
  assert((IsARMBuiltin || IsAArch64Builtin) && "Unexpected ARM builtin.");

  // We can't check the value of a dependent argument.
  Expr *Arg = TheCall->getArg(ArgNum);
  if (Arg->isTypeDependent() || Arg->isValueDependent())
    return false;

  // Check if the argument is a string literal.
  if (!isa<StringLiteral>(Arg->IgnoreParenImpCasts()))
    return Diag(TheCall->getBeginLoc(), diag::err_expr_not_string_literal)
           << Arg->getSourceRange();

  // Check the type of special register given.
  StringRef Reg = cast<StringLiteral>(Arg->IgnoreParenImpCasts())->getString();
  SmallVector<StringRef, 6> Fields;
  Reg.split(Fields, ":");

  if (Fields.size() != ExpectedFieldNum && !(AllowName && Fields.size() == 1))
    return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
           << Arg->getSourceRange();

  // If the string is the name of a register then we cannot check that it is
  // valid here but if the string is of one the forms described in ACLE then we
  // can check that the supplied fields are integers and within the valid
  // ranges.
  if (Fields.size() > 1) {
    bool FiveFields = Fields.size() == 5;

    bool ValidString = true;
    if (IsARMBuiltin) {
      ValidString &= Fields[0].starts_with_insensitive("cp") ||
                     Fields[0].starts_with_insensitive("p");
      if (ValidString)
        Fields[0] = Fields[0].drop_front(
            Fields[0].starts_with_insensitive("cp") ? 2 : 1);

      ValidString &= Fields[2].starts_with_insensitive("c");
      if (ValidString)
        Fields[2] = Fields[2].drop_front(1);

      if (FiveFields) {
        ValidString &= Fields[3].starts_with_insensitive("c");
        if (ValidString)
          Fields[3] = Fields[3].drop_front(1);
      }
    }

    SmallVector<int, 5> FieldBitWidths;
    if (FiveFields)
      FieldBitWidths.append({IsAArch64Builtin ? 2 : 4, 3, 4, 4, 3});
    else
      FieldBitWidths.append({4, 3, 4});

    for (unsigned i = 0; i < Fields.size(); ++i) {
      int IntField;
      ValidString &= !Fields[i].getAsInteger(10, IntField);
      ValidString &= (IntField >= 0 && IntField < (1 << FieldBitWidths[i]));
    }

    if (!ValidString)
      return Diag(TheCall->getBeginLoc(), diag::err_arm_invalid_specialreg)
             << Arg->getSourceRange();
  } else if (IsAArch64Builtin && Fields.size() == 1) {
    // This code validates writes to PSTATE registers.

    // Not a write.
    if (TheCall->getNumArgs() != 2)
      return false;

    // The 128-bit system register accesses do not touch PSTATE.
    if (BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
        BuiltinID == AArch64::BI__builtin_arm_wsr128)
      return false;

    // These are the named PSTATE accesses using "MSR (immediate)" instructions,
    // along with the upper limit on the immediates allowed.
    auto MaxLimit = llvm::StringSwitch<std::optional<unsigned>>(Reg)
                        .CaseLower("spsel", 15)
                        .CaseLower("daifclr", 15)
                        .CaseLower("daifset", 15)
                        .CaseLower("pan", 15)
                        .CaseLower("uao", 15)
                        .CaseLower("dit", 15)
                        .CaseLower("ssbs", 15)
                        .CaseLower("tco", 15)
                        .CaseLower("allint", 1)
                        .CaseLower("pm", 1)
                        .Default(std::nullopt);

    // If this is not a named PSTATE, just continue without validating, as this
    // will be lowered to an "MSR (register)" instruction directly
    if (!MaxLimit)
      return false;

    // Here we only allow constants in the range for that pstate, as required by
    // the ACLE.
    //
    // While clang also accepts the names of system registers in its ACLE
    // intrinsics, we prevent this with the PSTATE names used in MSR (immediate)
    // as the value written via a register is different to the value used as an
    // immediate to have the same effect. e.g., for the instruction `msr tco,
    // x0`, it is bit 25 of register x0 that is written into PSTATE.TCO, but
    // with `msr tco, #imm`, it is bit 0 of xN that is written into PSTATE.TCO.
    //
    // If a programmer wants to codegen the MSR (register) form of `msr tco,
    // xN`, they can still do so by specifying the register using five
    // colon-separated numbers in a string.
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, *MaxLimit);
  }

  return false;
}

/// getNeonEltType - Return the QualType corresponding to the elements of
/// the vector type specified by the NeonTypeFlags.  This is used to check
/// the pointer arguments for Neon load/store intrinsics.
static QualType getNeonEltType(NeonTypeFlags Flags, ASTContext &Context,
                               bool IsPolyUnsigned, bool IsInt64Long) {
  switch (Flags.getEltType()) {
  case NeonTypeFlags::Int8:
    return Flags.isUnsigned() ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Int16:
    return Flags.isUnsigned() ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Int32:
    return Flags.isUnsigned() ? Context.UnsignedIntTy : Context.IntTy;
  case NeonTypeFlags::Int64:
    if (IsInt64Long)
      return Flags.isUnsigned() ? Context.UnsignedLongTy : Context.LongTy;
    else
      return Flags.isUnsigned() ? Context.UnsignedLongLongTy
                                : Context.LongLongTy;
  case NeonTypeFlags::Poly8:
    return IsPolyUnsigned ? Context.UnsignedCharTy : Context.SignedCharTy;
  case NeonTypeFlags::Poly16:
    return IsPolyUnsigned ? Context.UnsignedShortTy : Context.ShortTy;
  case NeonTypeFlags::Poly64:
    if (IsInt64Long)
      return Context.UnsignedLongTy;
    else
      return Context.UnsignedLongLongTy;
  case NeonTypeFlags::Poly128:
    break;
  case NeonTypeFlags::Float16:
    return Context.HalfTy;
  case NeonTypeFlags::Float32:
    return Context.FloatTy;
  case NeonTypeFlags::Float64:
    return Context.DoubleTy;
  case NeonTypeFlags::BFloat16:
    return Context.BFloat16Ty;
  case NeonTypeFlags::MFloat8:
    return Context.MFloat8Ty;
  }
  llvm_unreachable("Invalid NeonTypeFlag!");
}

enum ArmSMEState : unsigned {
  ArmNoState = 0,

  ArmInZA = 0b01,
  ArmOutZA = 0b10,
  ArmInOutZA = 0b11,
  ArmZAMask = 0b11,

  ArmInZT0 = 0b01 << 2,
  ArmOutZT0 = 0b10 << 2,
  ArmInOutZT0 = 0b11 << 2,
  ArmZT0Mask = 0b11 << 2
};

bool SemaARM::CheckImmediateArg(CallExpr *TheCall, unsigned CheckTy,
                                unsigned ArgIdx, unsigned EltBitWidth,
                                unsigned ContainerBitWidth) {
  // Function that checks whether the operand (ArgIdx) is an immediate
  // that is one of a given set of values.
  auto CheckImmediateInSet = [&](std::initializer_list<int64_t> Set,
                                 int ErrDiag) -> bool {
    // We can't check the value of a dependent argument.
    Expr *Arg = TheCall->getArg(ArgIdx);
    if (Arg->isTypeDependent() || Arg->isValueDependent())
      return false;

    // Check constant-ness first.
    llvm::APSInt Imm;
    if (SemaRef.BuiltinConstantArg(TheCall, ArgIdx, Imm))
      return true;

    if (!llvm::is_contained(Set, Imm.getSExtValue()))
      return Diag(TheCall->getBeginLoc(), ErrDiag) << Arg->getSourceRange();
    return false;
  };

  switch ((ImmCheckType)CheckTy) {
  case ImmCheckType::ImmCheck0_31:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 31))
      return true;
    break;
  case ImmCheckType::ImmCheck0_13:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 13))
      return true;
    break;
  case ImmCheckType::ImmCheck0_63:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 63))
      return true;
    break;
  case ImmCheckType::ImmCheck1_16:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 16))
      return true;
    break;
  case ImmCheckType::ImmCheck0_7:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 7))
      return true;
    break;
  case ImmCheckType::ImmCheck1_1:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 1))
      return true;
    break;
  case ImmCheckType::ImmCheck1_3:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 3))
      return true;
    break;
  case ImmCheckType::ImmCheck1_7:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 7))
      return true;
    break;
  case ImmCheckType::ImmCheckExtract:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0,
                                        (2048 / EltBitWidth) - 1))
      return true;
    break;
  case ImmCheckType::ImmCheckCvt:
  case ImmCheckType::ImmCheckShiftRight:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, EltBitWidth))
      return true;
    break;
  case ImmCheckType::ImmCheckShiftRightNarrow:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, EltBitWidth / 2))
      return true;
    break;
  case ImmCheckType::ImmCheckShiftLeft:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, EltBitWidth - 1))
      return true;
    break;
  case ImmCheckType::ImmCheckLaneIndex:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0,
                                        (ContainerBitWidth / EltBitWidth) - 1))
      return true;
    break;
  case ImmCheckType::ImmCheckLaneIndexCompRotate:
    if (SemaRef.BuiltinConstantArgRange(
            TheCall, ArgIdx, 0, (ContainerBitWidth / (2 * EltBitWidth)) - 1))
      return true;
    break;
  case ImmCheckType::ImmCheckLaneIndexDot:
    if (SemaRef.BuiltinConstantArgRange(
            TheCall, ArgIdx, 0, (ContainerBitWidth / (4 * EltBitWidth)) - 1))
      return true;
    break;
  case ImmCheckType::ImmCheckComplexRot90_270:
    if (CheckImmediateInSet({90, 270}, diag::err_rotation_argument_to_cadd))
      return true;
    break;
  case ImmCheckType::ImmCheckComplexRotAll90:
    if (CheckImmediateInSet({0, 90, 180, 270},
                            diag::err_rotation_argument_to_cmla))
      return true;
    break;
  case ImmCheckType::ImmCheck0_1:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 1))
      return true;
    break;
  case ImmCheckType::ImmCheck0_2:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 2))
      return true;
    break;
  case ImmCheckType::ImmCheck0_3:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 3))
      return true;
    break;
  case ImmCheckType::ImmCheck0_0:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 0))
      return true;
    break;
  case ImmCheckType::ImmCheck0_15:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 15))
      return true;
    break;
  case ImmCheckType::ImmCheck0_255:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 0, 255))
      return true;
    break;
  case ImmCheckType::ImmCheck1_32:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 32))
      return true;
    break;
  case ImmCheckType::ImmCheck1_64:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 1, 64))
      return true;
    break;
  case ImmCheckType::ImmCheck2_4_Mul2:
    if (SemaRef.BuiltinConstantArgRange(TheCall, ArgIdx, 2, 4) ||
        SemaRef.BuiltinConstantArgMultiple(TheCall, ArgIdx, 2))
      return true;
    break;
  }
  return false;
}

bool SemaARM::PerformNeonImmChecks(
    CallExpr *TheCall,
    SmallVectorImpl<std::tuple<int, int, int, int>> &ImmChecks,
    int OverloadType) {
  bool HasError = false;

  for (const auto &I : ImmChecks) {
    auto [ArgIdx, CheckTy, ElementBitWidth, VecBitWidth] = I;

    if (OverloadType >= 0)
      ElementBitWidth = NeonTypeFlags(OverloadType).getEltSizeInBits();

    HasError |= CheckImmediateArg(TheCall, CheckTy, ArgIdx, ElementBitWidth,
                                  VecBitWidth);
  }

  return HasError;
}

bool SemaARM::PerformSVEImmChecks(
    CallExpr *TheCall, SmallVectorImpl<std::tuple<int, int, int>> &ImmChecks) {
  bool HasError = false;

  for (const auto &I : ImmChecks) {
    auto [ArgIdx, CheckTy, ElementBitWidth] = I;
    HasError |=
        CheckImmediateArg(TheCall, CheckTy, ArgIdx, ElementBitWidth, 128);
  }

  return HasError;
}

SemaARM::ArmStreamingType getArmStreamingFnType(const FunctionDecl *FD) {
  if (FD->hasAttr<ArmLocallyStreamingAttr>())
    return SemaARM::ArmStreaming;
  if (const Type *Ty = FD->getType().getTypePtrOrNull()) {
    if (const auto *FPT = Ty->getAs<FunctionProtoType>()) {
      if (FPT->getAArch64SMEAttributes() &
          FunctionType::SME_PStateSMEnabledMask)
        return SemaARM::ArmStreaming;
      if (FPT->getAArch64SMEAttributes() &
          FunctionType::SME_PStateSMCompatibleMask)
        return SemaARM::ArmStreamingCompatible;
    }
  }
  return SemaARM::ArmNonStreaming;
}

static bool checkArmStreamingBuiltin(Sema &S, CallExpr *TheCall,
                                     const FunctionDecl *FD,
                                     SemaARM::ArmStreamingType BuiltinType,
                                     unsigned BuiltinID) {
  SemaARM::ArmStreamingType FnType = getArmStreamingFnType(FD);

  // Check if the intrinsic is available in the right mode, i.e.
  // * When compiling for SME only, the caller must be in streaming mode.
  // * When compiling for SVE only, the caller must be in non-streaming mode.
  // * When compiling for both SVE and SME, the caller can be in either mode.
  if (BuiltinType == SemaARM::VerifyRuntimeMode) {
    llvm::StringMap<bool> CallerFeatureMapWithoutSVE;
    S.Context.getFunctionFeatureMap(CallerFeatureMapWithoutSVE, FD);
    CallerFeatureMapWithoutSVE["sve"] = false;

    // Avoid emitting diagnostics for a function that can never compile.
    if (FnType == SemaARM::ArmStreaming && !CallerFeatureMapWithoutSVE["sme"])
      return false;

    llvm::StringMap<bool> CallerFeatureMapWithoutSME;
    S.Context.getFunctionFeatureMap(CallerFeatureMapWithoutSME, FD);
    CallerFeatureMapWithoutSME["sme"] = false;

    // We know the builtin requires either some combination of SVE flags, or
    // some combination of SME flags, but we need to figure out which part
    // of the required features is satisfied by the target features.
    //
    // For a builtin with target guard 'sve2p1|sme2', if we compile with
    // '+sve2p1,+sme', then we know that it satisfies the 'sve2p1' part if we
    // evaluate the features for '+sve2p1,+sme,+nosme'.
    //
    // Similarly, if we compile with '+sve2,+sme2', then we know it satisfies
    // the 'sme2' part if we evaluate the features for '+sve2,+sme2,+nosve'.
    StringRef BuiltinTargetGuards(
        S.Context.BuiltinInfo.getRequiredFeatures(BuiltinID));
    bool SatisfiesSVE = Builtin::evaluateRequiredTargetFeatures(
        BuiltinTargetGuards, CallerFeatureMapWithoutSME);
    bool SatisfiesSME = Builtin::evaluateRequiredTargetFeatures(
        BuiltinTargetGuards, CallerFeatureMapWithoutSVE);

    if ((SatisfiesSVE && SatisfiesSME) ||
        (SatisfiesSVE && FnType == SemaARM::ArmStreamingCompatible))
      return false;
    else if (SatisfiesSVE)
      BuiltinType = SemaARM::ArmNonStreaming;
    else if (SatisfiesSME)
      BuiltinType = SemaARM::ArmStreaming;
    else
      // This should be diagnosed by CodeGen
      return false;
  }

  if (FnType != SemaARM::ArmNonStreaming &&
      BuiltinType == SemaARM::ArmNonStreaming)
    S.Diag(TheCall->getBeginLoc(), diag::err_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "non-streaming";
  else if (FnType != SemaARM::ArmStreaming &&
           BuiltinType == SemaARM::ArmStreaming)
    S.Diag(TheCall->getBeginLoc(), diag::err_attribute_arm_sm_incompat_builtin)
        << TheCall->getSourceRange() << "streaming";
  else
    return false;

  return true;
}

static ArmSMEState getSMEState(unsigned BuiltinID) {
  switch (BuiltinID) {
  default:
    return ArmNoState;
#define GET_SME_BUILTIN_GET_STATE
#include "clang/Basic/arm_sme_builtins_za_state.inc"
#undef GET_SME_BUILTIN_GET_STATE
  }
}

bool SemaARM::CheckSMEBuiltinFunctionCall(unsigned BuiltinID,
                                          CallExpr *TheCall) {
  if (const FunctionDecl *FD =
          SemaRef.getCurFunctionDecl(/*AllowLambda=*/true)) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SME_STREAMING_ATTRS
#include "clang/Basic/arm_sme_streaming_attrs.inc"
#undef GET_SME_STREAMING_ATTRS
    }

    if (BuiltinType &&
        checkArmStreamingBuiltin(SemaRef, TheCall, FD, *BuiltinType, BuiltinID))
      return true;

    if ((getSMEState(BuiltinID) & ArmZAMask) && !hasArmZAState(FD))
      Diag(TheCall->getBeginLoc(),
           diag::warn_attribute_arm_za_builtin_no_za_state)
          << TheCall->getSourceRange();

    if ((getSMEState(BuiltinID) & ArmZT0Mask) && !hasArmZT0State(FD))
      Diag(TheCall->getBeginLoc(),
           diag::warn_attribute_arm_zt0_builtin_no_zt0_state)
          << TheCall->getSourceRange();
  }

  // Range check SME intrinsics that take immediate values.
  SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SME_IMMEDIATE_CHECK
#include "clang/Basic/arm_sme_sema_rangechecks.inc"
#undef GET_SME_IMMEDIATE_CHECK
  }

  return PerformSVEImmChecks(TheCall, ImmChecks);
}

bool SemaARM::CheckSVEBuiltinFunctionCall(unsigned BuiltinID,
                                          CallExpr *TheCall) {
  if (const FunctionDecl *FD =
          SemaRef.getCurFunctionDecl(/*AllowLambda=*/true)) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
#define GET_SVE_STREAMING_ATTRS
#include "clang/Basic/arm_sve_streaming_attrs.inc"
#undef GET_SVE_STREAMING_ATTRS
    }
    if (BuiltinType &&
        checkArmStreamingBuiltin(SemaRef, TheCall, FD, *BuiltinType, BuiltinID))
      return true;
  }
  // Range check SVE intrinsics that take immediate values.
  SmallVector<std::tuple<int, int, int>, 3> ImmChecks;

  switch (BuiltinID) {
  default:
    return false;
#define GET_SVE_IMMEDIATE_CHECK
#include "clang/Basic/arm_sve_sema_rangechecks.inc"
#undef GET_SVE_IMMEDIATE_CHECK
  }

  return PerformSVEImmChecks(TheCall, ImmChecks);
}

bool SemaARM::CheckNeonBuiltinFunctionCall(const TargetInfo &TI,
                                           unsigned BuiltinID,
                                           CallExpr *TheCall) {
  if (const FunctionDecl *FD =
          SemaRef.getCurFunctionDecl(/*AllowLambda=*/true)) {
    std::optional<ArmStreamingType> BuiltinType;

    switch (BuiltinID) {
    default:
      break;
#define GET_NEON_STREAMING_COMPAT_FLAG
#include "clang/Basic/arm_neon.inc"
#undef GET_NEON_STREAMING_COMPAT_FLAG
    }
    if (BuiltinType &&
        checkArmStreamingBuiltin(SemaRef, TheCall, FD, *BuiltinType, BuiltinID))
      return true;
  }

  llvm::APSInt Result;
  uint64_t mask = 0;
  int TV = -1;
  int PtrArgNum = -1;
  bool HasConstPtr = false;
  switch (BuiltinID) {
#define GET_NEON_OVERLOAD_CHECK
#include "clang/Basic/arm_fp16.inc"
#include "clang/Basic/arm_neon.inc"
#undef GET_NEON_OVERLOAD_CHECK
  }

  // For NEON intrinsics which are overloaded on vector element type, validate
  // the immediate which specifies which variant to emit.
  unsigned ImmArg = TheCall->getNumArgs() - 1;
  if (mask) {
    if (SemaRef.BuiltinConstantArg(TheCall, ImmArg, Result))
      return true;

    TV = Result.getLimitedValue(64);
    if ((TV > 63) || (mask & (1ULL << TV)) == 0)
      return Diag(TheCall->getBeginLoc(), diag::err_invalid_neon_type_code)
             << TheCall->getArg(ImmArg)->getSourceRange();
  }

  if (PtrArgNum >= 0) {
    // Check that pointer arguments have the specified type.
    Expr *Arg = TheCall->getArg(PtrArgNum);
    if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg))
      Arg = ICE->getSubExpr();
    ExprResult RHS = SemaRef.DefaultFunctionArrayLvalueConversion(Arg);
    QualType RHSTy = RHS.get()->getType();

    llvm::Triple::ArchType Arch = TI.getTriple().getArch();
    bool IsPolyUnsigned = Arch == llvm::Triple::aarch64 ||
                          Arch == llvm::Triple::aarch64_32 ||
                          Arch == llvm::Triple::aarch64_be;
    bool IsInt64Long = TI.getInt64Type() == TargetInfo::SignedLong;
    QualType EltTy = getNeonEltType(NeonTypeFlags(TV), getASTContext(),
                                    IsPolyUnsigned, IsInt64Long);
    if (HasConstPtr)
      EltTy = EltTy.withConst();
    QualType LHSTy = getASTContext().getPointerType(EltTy);
    AssignConvertType ConvTy;
    ConvTy = SemaRef.CheckSingleAssignmentConstraints(LHSTy, RHS);
    if (RHS.isInvalid())
      return true;
    if (SemaRef.DiagnoseAssignmentResult(ConvTy, Arg->getBeginLoc(), LHSTy,
                                         RHSTy, RHS.get(),
                                         AssignmentAction::Assigning))
      return true;
  }

  // For NEON intrinsics which take an immediate value as part of the
  // instruction, range check them here.
  SmallVector<std::tuple<int, int, int, int>, 2> ImmChecks;
  switch (BuiltinID) {
  default:
    return false;
#define GET_NEON_IMMEDIATE_CHECK
#include "clang/Basic/arm_fp16.inc"
#include "clang/Basic/arm_neon.inc"
#undef GET_NEON_IMMEDIATE_CHECK
  }

  return PerformNeonImmChecks(TheCall, ImmChecks, TV);
}

bool SemaARM::CheckMVEBuiltinFunctionCall(unsigned BuiltinID,
                                          CallExpr *TheCall) {
  switch (BuiltinID) {
  default:
    return false;
#include "clang/Basic/arm_mve_builtin_sema.inc"
  }
}

bool SemaARM::CheckCDEBuiltinFunctionCall(const TargetInfo &TI,
                                          unsigned BuiltinID,
                                          CallExpr *TheCall) {
  bool Err = false;
  switch (BuiltinID) {
  default:
    return false;
#include "clang/Basic/arm_cde_builtin_sema.inc"
  }

  if (Err)
    return true;

  return CheckARMCoprocessorImmediate(TI, TheCall->getArg(0), /*WantCDE*/ true);
}

bool SemaARM::CheckARMCoprocessorImmediate(const TargetInfo &TI,
                                           const Expr *CoprocArg,
                                           bool WantCDE) {
  ASTContext &Context = getASTContext();
  if (SemaRef.isConstantEvaluatedContext())
    return false;

  // We can't check the value of a dependent argument.
  if (CoprocArg->isTypeDependent() || CoprocArg->isValueDependent())
    return false;

  llvm::APSInt CoprocNoAP = *CoprocArg->getIntegerConstantExpr(Context);
  int64_t CoprocNo = CoprocNoAP.getExtValue();
  assert(CoprocNo >= 0 && "Coprocessor immediate must be non-negative");

  uint32_t CDECoprocMask = TI.getARMCDECoprocMask();
  bool IsCDECoproc = CoprocNo <= 7 && (CDECoprocMask & (1 << CoprocNo));

  if (IsCDECoproc != WantCDE)
    return Diag(CoprocArg->getBeginLoc(), diag::err_arm_invalid_coproc)
           << (int)CoprocNo << (int)WantCDE << CoprocArg->getSourceRange();

  return false;
}

bool SemaARM::CheckARMBuiltinExclusiveCall(unsigned BuiltinID,
                                           CallExpr *TheCall,
                                           unsigned MaxWidth) {
  assert((BuiltinID == ARM::BI__builtin_arm_ldrex ||
          BuiltinID == ARM::BI__builtin_arm_ldaex ||
          BuiltinID == ARM::BI__builtin_arm_strex ||
          BuiltinID == ARM::BI__builtin_arm_stlex ||
          BuiltinID == AArch64::BI__builtin_arm_ldrex ||
          BuiltinID == AArch64::BI__builtin_arm_ldaex ||
          BuiltinID == AArch64::BI__builtin_arm_strex ||
          BuiltinID == AArch64::BI__builtin_arm_stlex) &&
         "unexpected ARM builtin");
  bool IsLdrex = BuiltinID == ARM::BI__builtin_arm_ldrex ||
                 BuiltinID == ARM::BI__builtin_arm_ldaex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldrex ||
                 BuiltinID == AArch64::BI__builtin_arm_ldaex;

  ASTContext &Context = getASTContext();
  DeclRefExpr *DRE =
      cast<DeclRefExpr>(TheCall->getCallee()->IgnoreParenCasts());

  // Ensure that we have the proper number of arguments.
  if (SemaRef.checkArgCount(TheCall, IsLdrex ? 1 : 2))
    return true;

  // Inspect the pointer argument of the atomic builtin.  This should always be
  // a pointer type, whose element is an integral scalar or pointer type.
  // Because it is a pointer type, we don't have to worry about any implicit
  // casts here.
  Expr *PointerArg = TheCall->getArg(IsLdrex ? 0 : 1);
  ExprResult PointerArgRes =
      SemaRef.DefaultFunctionArrayLvalueConversion(PointerArg);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  const PointerType *pointerType = PointerArg->getType()->getAs<PointerType>();
  if (!pointerType) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer)
        << PointerArg->getType() << 0 << PointerArg->getSourceRange();
    return true;
  }

  // ldrex takes a "const volatile T*" and strex takes a "volatile T*". Our next
  // task is to insert the appropriate casts into the AST. First work out just
  // what the appropriate type is.
  QualType ValType = pointerType->getPointeeType();
  QualType AddrType = ValType.getUnqualifiedType().withVolatile();
  if (IsLdrex)
    AddrType.addConst();

  // Issue a warning if the cast is dodgy.
  CastKind CastNeeded = CK_NoOp;
  if (!AddrType.isAtLeastAsQualifiedAs(ValType, getASTContext())) {
    CastNeeded = CK_BitCast;
    Diag(DRE->getBeginLoc(), diag::ext_typecheck_convert_discards_qualifiers)
        << PointerArg->getType() << Context.getPointerType(AddrType)
        << AssignmentAction::Passing << PointerArg->getSourceRange();
  }

  // Finally, do the cast and replace the argument with the corrected version.
  AddrType = Context.getPointerType(AddrType);
  PointerArgRes = SemaRef.ImpCastExprToType(PointerArg, AddrType, CastNeeded);
  if (PointerArgRes.isInvalid())
    return true;
  PointerArg = PointerArgRes.get();

  TheCall->setArg(IsLdrex ? 0 : 1, PointerArg);

  // In general, we allow ints, floats and pointers to be loaded and stored.
  if (!ValType->isIntegerType() && !ValType->isAnyPointerType() &&
      !ValType->isBlockPointerType() && !ValType->isFloatingType()) {
    Diag(DRE->getBeginLoc(), diag::err_atomic_builtin_must_be_pointer_intfltptr)
        << PointerArg->getType() << 0 << PointerArg->getSourceRange();
    return true;
  }

  // But ARM doesn't have instructions to deal with 128-bit versions.
  if (Context.getTypeSize(ValType) > MaxWidth) {
    assert(MaxWidth == 64 && "Diagnostic unexpectedly inaccurate");
    Diag(DRE->getBeginLoc(), diag::err_atomic_exclusive_builtin_pointer_size)
        << PointerArg->getType() << PointerArg->getSourceRange();
    return true;
  }

  switch (ValType.getObjCLifetime()) {
  case Qualifiers::OCL_None:
  case Qualifiers::OCL_ExplicitNone:
    // okay
    break;

  case Qualifiers::OCL_Weak:
  case Qualifiers::OCL_Strong:
  case Qualifiers::OCL_Autoreleasing:
    Diag(DRE->getBeginLoc(), diag::err_arc_atomic_ownership)
        << ValType << PointerArg->getSourceRange();
    return true;
  }

  if (IsLdrex) {
    TheCall->setType(ValType);
    return false;
  }

  // Initialize the argument to be stored.
  ExprResult ValArg = TheCall->getArg(0);
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      Context, ValType, /*consume*/ false);
  ValArg = SemaRef.PerformCopyInitialization(Entity, SourceLocation(), ValArg);
  if (ValArg.isInvalid())
    return true;
  TheCall->setArg(0, ValArg.get());

  // __builtin_arm_strex always returns an int. It's marked as such in the .def,
  // but the custom checker bypasses all default analysis.
  TheCall->setType(Context.IntTy);
  return false;
}

bool SemaARM::CheckARMBuiltinFunctionCall(const TargetInfo &TI,
                                          unsigned BuiltinID,
                                          CallExpr *TheCall) {
  if (BuiltinID == ARM::BI__builtin_arm_ldrex ||
      BuiltinID == ARM::BI__builtin_arm_ldaex ||
      BuiltinID == ARM::BI__builtin_arm_strex ||
      BuiltinID == ARM::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 64);
  }

  if (BuiltinID == ARM::BI__builtin_arm_prefetch) {
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 1);
  }

  if (BuiltinID == ARM::BI__builtin_arm_rsr64 ||
      BuiltinID == ARM::BI__builtin_arm_wsr64)
    return BuiltinARMSpecialReg(BuiltinID, TheCall, 0, 3, false);

  if (BuiltinID == ARM::BI__builtin_arm_rsr ||
      BuiltinID == ARM::BI__builtin_arm_rsrp ||
      BuiltinID == ARM::BI__builtin_arm_wsr ||
      BuiltinID == ARM::BI__builtin_arm_wsrp)
    return BuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  if (CheckNeonBuiltinFunctionCall(TI, BuiltinID, TheCall))
    return true;
  if (CheckMVEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;
  if (CheckCDEBuiltinFunctionCall(TI, BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  // FIXME: VFP Intrinsics should error if VFP not present.
  switch (BuiltinID) {
  default:
    return false;
  case ARM::BI__builtin_arm_ssat:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 1, 32);
  case ARM::BI__builtin_arm_usat:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case ARM::BI__builtin_arm_ssat16:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 1, 16);
  case ARM::BI__builtin_arm_usat16:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case ARM::BI__builtin_arm_vcvtr_f:
  case ARM::BI__builtin_arm_vcvtr_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1);
  case ARM::BI__builtin_arm_dmb:
  case ARM::BI__dmb:
  case ARM::BI__builtin_arm_dsb:
  case ARM::BI__dsb:
  case ARM::BI__builtin_arm_isb:
  case ARM::BI__isb:
  case ARM::BI__builtin_arm_dbg:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 15);
  case ARM::BI__builtin_arm_cdp:
  case ARM::BI__builtin_arm_cdp2:
  case ARM::BI__builtin_arm_mcr:
  case ARM::BI__builtin_arm_mcr2:
  case ARM::BI__builtin_arm_mrc:
  case ARM::BI__builtin_arm_mrc2:
  case ARM::BI__builtin_arm_mcrr:
  case ARM::BI__builtin_arm_mcrr2:
  case ARM::BI__builtin_arm_mrrc:
  case ARM::BI__builtin_arm_mrrc2:
  case ARM::BI__builtin_arm_ldc:
  case ARM::BI__builtin_arm_ldcl:
  case ARM::BI__builtin_arm_ldc2:
  case ARM::BI__builtin_arm_ldc2l:
  case ARM::BI__builtin_arm_stc:
  case ARM::BI__builtin_arm_stcl:
  case ARM::BI__builtin_arm_stc2:
  case ARM::BI__builtin_arm_stc2l:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 15) ||
           CheckARMCoprocessorImmediate(TI, TheCall->getArg(0),
                                        /*WantCDE*/ false);
  }
}

bool SemaARM::CheckAArch64BuiltinFunctionCall(const TargetInfo &TI,
                                              unsigned BuiltinID,
                                              CallExpr *TheCall) {
  if (BuiltinID == AArch64::BI__builtin_arm_ldrex ||
      BuiltinID == AArch64::BI__builtin_arm_ldaex ||
      BuiltinID == AArch64::BI__builtin_arm_strex ||
      BuiltinID == AArch64::BI__builtin_arm_stlex) {
    return CheckARMBuiltinExclusiveCall(BuiltinID, TheCall, 128);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_prefetch) {
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 1) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 4, 0, 1);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr64 ||
      BuiltinID == AArch64::BI__builtin_arm_rsr128 ||
      BuiltinID == AArch64::BI__builtin_arm_wsr128)
    return BuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Memory Tagging Extensions (MTE) Intrinsics
  if (BuiltinID == AArch64::BI__builtin_arm_irg ||
      BuiltinID == AArch64::BI__builtin_arm_addg ||
      BuiltinID == AArch64::BI__builtin_arm_gmi ||
      BuiltinID == AArch64::BI__builtin_arm_ldg ||
      BuiltinID == AArch64::BI__builtin_arm_stg ||
      BuiltinID == AArch64::BI__builtin_arm_subp) {
    return BuiltinARMMemoryTaggingCall(BuiltinID, TheCall);
  }

  if (BuiltinID == AArch64::BI__builtin_arm_rsr ||
      BuiltinID == AArch64::BI__builtin_arm_rsrp ||
      BuiltinID == AArch64::BI__builtin_arm_wsr ||
      BuiltinID == AArch64::BI__builtin_arm_wsrp)
    return BuiltinARMSpecialReg(BuiltinID, TheCall, 0, 5, true);

  // Only check the valid encoding range. Any constant in this range would be
  // converted to a register of the form S1_2_C3_C4_5. Let the hardware throw
  // an exception for incorrect registers. This matches MSVC behavior.
  if (BuiltinID == AArch64::BI_ReadStatusReg ||
      BuiltinID == AArch64::BI_WriteStatusReg || BuiltinID == AArch64::BI__sys)
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 0x7fff);

  if (BuiltinID == AArch64::BI__getReg)
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 31);

  if (BuiltinID == AArch64::BI__break)
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 0xffff);

  if (BuiltinID == AArch64::BI__hlt)
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 0xffff);

  if (CheckNeonBuiltinFunctionCall(TI, BuiltinID, TheCall))
    return true;

  if (CheckSVEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  if (CheckSMEBuiltinFunctionCall(BuiltinID, TheCall))
    return true;

  // For intrinsics which take an immediate value as part of the instruction,
  // range check them here.
  unsigned i = 0, l = 0, u = 0;
  switch (BuiltinID) {
  default: return false;
  case AArch64::BI__builtin_arm_dmb:
  case AArch64::BI__dmb:
  case AArch64::BI__builtin_arm_dsb:
  case AArch64::BI__dsb:
  case AArch64::BI__builtin_arm_isb:
  case AArch64::BI__isb:
    l = 0;
    u = 15;
    break;
  case AArch64::BI__builtin_arm_tcancel: l = 0; u = 65535; break;
  }

  return SemaRef.BuiltinConstantArgRange(TheCall, i, l, u + l);
}

namespace {
struct IntrinToName {
  uint32_t Id;
  int32_t FullName;
  int32_t ShortName;
};
} // unnamed namespace

static bool BuiltinAliasValid(unsigned BuiltinID, StringRef AliasName,
                              ArrayRef<IntrinToName> Map,
                              const char *IntrinNames) {
  AliasName.consume_front("__arm_");
  const IntrinToName *It =
      llvm::lower_bound(Map, BuiltinID, [](const IntrinToName &L, unsigned Id) {
        return L.Id < Id;
      });
  if (It == Map.end() || It->Id != BuiltinID)
    return false;
  StringRef FullName(&IntrinNames[It->FullName]);
  if (AliasName == FullName)
    return true;
  if (It->ShortName == -1)
    return false;
  StringRef ShortName(&IntrinNames[It->ShortName]);
  return AliasName == ShortName;
}

bool SemaARM::MveAliasValid(unsigned BuiltinID, StringRef AliasName) {
#include "clang/Basic/arm_mve_builtin_aliases.inc"
  // The included file defines:
  // - ArrayRef<IntrinToName> Map
  // - const char IntrinNames[]
  return BuiltinAliasValid(BuiltinID, AliasName, Map, IntrinNames);
}

bool SemaARM::CdeAliasValid(unsigned BuiltinID, StringRef AliasName) {
#include "clang/Basic/arm_cde_builtin_aliases.inc"
  return BuiltinAliasValid(BuiltinID, AliasName, Map, IntrinNames);
}

bool SemaARM::SveAliasValid(unsigned BuiltinID, StringRef AliasName) {
  if (getASTContext().BuiltinInfo.isAuxBuiltinID(BuiltinID))
    BuiltinID = getASTContext().BuiltinInfo.getAuxBuiltinID(BuiltinID);
  return BuiltinID >= AArch64::FirstSVEBuiltin &&
         BuiltinID <= AArch64::LastSVEBuiltin;
}

bool SemaARM::SmeAliasValid(unsigned BuiltinID, StringRef AliasName) {
  if (getASTContext().BuiltinInfo.isAuxBuiltinID(BuiltinID))
    BuiltinID = getASTContext().BuiltinInfo.getAuxBuiltinID(BuiltinID);
  return BuiltinID >= AArch64::FirstSMEBuiltin &&
         BuiltinID <= AArch64::LastSMEBuiltin;
}

void SemaARM::handleBuiltinAliasAttr(Decl *D, const ParsedAttr &AL) {
  ASTContext &Context = getASTContext();
  if (!AL.isArgIdent(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *Ident = AL.getArgAsIdent(0)->getIdentifierInfo();
  unsigned BuiltinID = Ident->getBuiltinID();
  StringRef AliasName = cast<FunctionDecl>(D)->getIdentifier()->getName();

  bool IsAArch64 = Context.getTargetInfo().getTriple().isAArch64();
  if ((IsAArch64 && !SveAliasValid(BuiltinID, AliasName) &&
       !SmeAliasValid(BuiltinID, AliasName)) ||
      (!IsAArch64 && !MveAliasValid(BuiltinID, AliasName) &&
       !CdeAliasValid(BuiltinID, AliasName))) {
    Diag(AL.getLoc(), diag::err_attribute_arm_builtin_alias);
    return;
  }

  D->addAttr(::new (Context) ArmBuiltinAliasAttr(Context, AL, Ident));
}

static bool checkNewAttrMutualExclusion(
    Sema &S, const ParsedAttr &AL, const FunctionProtoType *FPT,
    FunctionType::ArmStateValue CurrentState, StringRef StateName) {
  auto CheckForIncompatibleAttr =
      [&](FunctionType::ArmStateValue IncompatibleState,
          StringRef IncompatibleStateName) {
        if (CurrentState == IncompatibleState) {
          S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible)
              << (std::string("'__arm_new(\"") + StateName.str() + "\")'")
              << (std::string("'") + IncompatibleStateName.str() + "(\"" +
                  StateName.str() + "\")'")
              << true;
          AL.setInvalid();
        }
      };

  CheckForIncompatibleAttr(FunctionType::ARM_In, "__arm_in");
  CheckForIncompatibleAttr(FunctionType::ARM_Out, "__arm_out");
  CheckForIncompatibleAttr(FunctionType::ARM_InOut, "__arm_inout");
  CheckForIncompatibleAttr(FunctionType::ARM_Preserves, "__arm_preserves");
  return AL.isInvalid();
}

void SemaARM::handleNewAttr(Decl *D, const ParsedAttr &AL) {
  if (!AL.getNumArgs()) {
    Diag(AL.getLoc(), diag::err_missing_arm_state) << AL;
    AL.setInvalid();
    return;
  }

  std::vector<StringRef> NewState;
  if (const auto *ExistingAttr = D->getAttr<ArmNewAttr>()) {
    for (StringRef S : ExistingAttr->newArgs())
      NewState.push_back(S);
  }

  bool HasZA = false;
  bool HasZT0 = false;
  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    StringRef StateName;
    SourceLocation LiteralLoc;
    if (!SemaRef.checkStringLiteralArgumentAttr(AL, I, StateName, &LiteralLoc))
      return;

    if (StateName == "za")
      HasZA = true;
    else if (StateName == "zt0")
      HasZT0 = true;
    else {
      Diag(LiteralLoc, diag::err_unknown_arm_state) << StateName;
      AL.setInvalid();
      return;
    }

    if (!llvm::is_contained(NewState, StateName)) // Avoid adding duplicates.
      NewState.push_back(StateName);
  }

  if (auto *FPT = dyn_cast<FunctionProtoType>(D->getFunctionType())) {
    FunctionType::ArmStateValue ZAState =
        FunctionType::getArmZAState(FPT->getAArch64SMEAttributes());
    if (HasZA && ZAState != FunctionType::ARM_None &&
        checkNewAttrMutualExclusion(SemaRef, AL, FPT, ZAState, "za"))
      return;
    FunctionType::ArmStateValue ZT0State =
        FunctionType::getArmZT0State(FPT->getAArch64SMEAttributes());
    if (HasZT0 && ZT0State != FunctionType::ARM_None &&
        checkNewAttrMutualExclusion(SemaRef, AL, FPT, ZT0State, "zt0"))
      return;
  }

  D->dropAttr<ArmNewAttr>();
  D->addAttr(::new (getASTContext()) ArmNewAttr(
      getASTContext(), AL, NewState.data(), NewState.size()));
}

void SemaARM::handleCmseNSEntryAttr(Decl *D, const ParsedAttr &AL) {
  if (getLangOpts().CPlusPlus && !D->getDeclContext()->isExternCContext()) {
    Diag(AL.getLoc(), diag::err_attribute_not_clinkage) << AL;
    return;
  }

  const auto *FD = cast<FunctionDecl>(D);
  if (!FD->isExternallyVisible()) {
    Diag(AL.getLoc(), diag::warn_attribute_cmse_entry_static);
    return;
  }

  D->addAttr(::new (getASTContext()) CmseNSEntryAttr(getASTContext(), AL));
}

void SemaARM::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  // Check the attribute arguments.
  if (AL.getNumArgs() > 1) {
    Diag(AL.getLoc(), diag::err_attribute_too_many_arguments) << AL << 1;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;

  if (AL.getNumArgs() == 0)
    Str = "";
  else if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  ARMInterruptAttr::InterruptType Kind;
  if (!ARMInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << Str << ArgLoc;
    return;
  }

  if (!D->hasAttr<ARMSaveFPAttr>()) {
    const TargetInfo &TI = getASTContext().getTargetInfo();
    if (TI.hasFeature("vfp"))
      Diag(D->getLocation(), diag::warn_arm_interrupt_vfp_clobber);
  }

  D->addAttr(::new (getASTContext())
                 ARMInterruptAttr(getASTContext(), AL, Kind));
}

void SemaARM::handleInterruptSaveFPAttr(Decl *D, const ParsedAttr &AL) {
  // Go ahead and add ARMSaveFPAttr because handleInterruptAttr() checks for
  // it when deciding to issue a diagnostic about clobbering floating point
  // registers, which ARMSaveFPAttr prevents.
  D->addAttr(::new (SemaRef.Context) ARMSaveFPAttr(SemaRef.Context, AL));
  SemaRef.ARM().handleInterruptAttr(D, AL);

  // If ARM().handleInterruptAttr() failed, remove ARMSaveFPAttr.
  if (!D->hasAttr<ARMInterruptAttr>()) {
    D->dropAttr<ARMSaveFPAttr>();
    return;
  }

  // If VFP not enabled, remove ARMSaveFPAttr but leave ARMInterruptAttr.
  bool VFP = SemaRef.Context.getTargetInfo().hasFeature("vfp");

  if (!VFP) {
    SemaRef.Diag(D->getLocation(), diag::warn_arm_interrupt_save_fp_without_vfp_unit);
    D->dropAttr<ARMSaveFPAttr>();
  }
}

// Check if the function definition uses any AArch64 SME features without
// having the '+sme' feature enabled and warn user if sme locally streaming
// function returns or uses arguments with VL-based types.
void SemaARM::CheckSMEFunctionDefAttributes(const FunctionDecl *FD) {
  const auto *Attr = FD->getAttr<ArmNewAttr>();
  bool UsesSM = FD->hasAttr<ArmLocallyStreamingAttr>();
  bool UsesZA = Attr && Attr->isNewZA();
  bool UsesZT0 = Attr && Attr->isNewZT0();

  if (UsesZA || UsesZT0) {
    if (const auto *FPT = FD->getType()->getAs<FunctionProtoType>()) {
      FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
      if (EPI.AArch64SMEAttributes & FunctionType::SME_AgnosticZAStateMask)
        Diag(FD->getLocation(), diag::err_sme_unsupported_agnostic_new);
    }
  }

  if (FD->hasAttr<ArmLocallyStreamingAttr>()) {
    if (FD->getReturnType()->isSizelessVectorType())
      Diag(FD->getLocation(),
           diag::warn_sme_locally_streaming_has_vl_args_returns)
          << /*IsArg=*/false;
    if (llvm::any_of(FD->parameters(), [](ParmVarDecl *P) {
          return P->getOriginalType()->isSizelessVectorType();
        }))
      Diag(FD->getLocation(),
           diag::warn_sme_locally_streaming_has_vl_args_returns)
          << /*IsArg=*/true;
  }
  if (const auto *FPT = FD->getType()->getAs<FunctionProtoType>()) {
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    UsesSM |= EPI.AArch64SMEAttributes & FunctionType::SME_PStateSMEnabledMask;
    UsesZA |= FunctionType::getArmZAState(EPI.AArch64SMEAttributes) !=
              FunctionType::ARM_None;
    UsesZT0 |= FunctionType::getArmZT0State(EPI.AArch64SMEAttributes) !=
               FunctionType::ARM_None;
  }

  ASTContext &Context = getASTContext();
  if (UsesSM || UsesZA) {
    llvm::StringMap<bool> FeatureMap;
    Context.getFunctionFeatureMap(FeatureMap, FD);
    if (!FeatureMap.contains("sme")) {
      if (UsesSM)
        Diag(FD->getLocation(),
             diag::err_sme_definition_using_sm_in_non_sme_target);
      else
        Diag(FD->getLocation(),
             diag::err_sme_definition_using_za_in_non_sme_target);
    }
  }
  if (UsesZT0) {
    llvm::StringMap<bool> FeatureMap;
    Context.getFunctionFeatureMap(FeatureMap, FD);
    if (!FeatureMap.contains("sme2")) {
      Diag(FD->getLocation(),
           diag::err_sme_definition_using_zt0_in_non_sme2_target);
    }
  }
}

/// getSVETypeSize - Return SVE vector or predicate register size.
static uint64_t getSVETypeSize(ASTContext &Context, const BuiltinType *Ty) {
  assert(Ty->isSveVLSBuiltinType() && "Invalid SVE Type");
  if (Ty->getKind() == BuiltinType::SveBool ||
      Ty->getKind() == BuiltinType::SveCount)
    return (Context.getLangOpts().VScaleMin * 128) / Context.getCharWidth();
  return Context.getLangOpts().VScaleMin * 128;
}

bool SemaARM::areCompatibleSveTypes(QualType FirstType, QualType SecondType) {
  auto IsValidCast = [this](QualType FirstType, QualType SecondType) {
    if (const auto *BT = FirstType->getAs<BuiltinType>()) {
      if (const auto *VT = SecondType->getAs<VectorType>()) {
        ASTContext &Context = getASTContext();
        // Predicates have the same representation as uint8 so we also have to
        // check the kind to make these types incompatible.
        if (VT->getVectorKind() == VectorKind::SveFixedLengthPredicate)
          return BT->getKind() == BuiltinType::SveBool;
        else if (VT->getVectorKind() == VectorKind::SveFixedLengthData)
          return VT->getElementType().getCanonicalType() ==
                 FirstType->getSveEltType(Context);
        else if (VT->getVectorKind() == VectorKind::Generic)
          return Context.getTypeSize(SecondType) ==
                     getSVETypeSize(Context, BT) &&
                 Context.hasSameType(
                     VT->getElementType(),
                     Context.getBuiltinVectorTypeInfo(BT).ElementType);
      }
    }
    return false;
  };

  return IsValidCast(FirstType, SecondType) ||
         IsValidCast(SecondType, FirstType);
}

bool SemaARM::areLaxCompatibleSveTypes(QualType FirstType,
                                       QualType SecondType) {
  auto IsLaxCompatible = [this](QualType FirstType, QualType SecondType) {
    const auto *BT = FirstType->getAs<BuiltinType>();
    if (!BT)
      return false;

    const auto *VecTy = SecondType->getAs<VectorType>();
    if (VecTy && (VecTy->getVectorKind() == VectorKind::SveFixedLengthData ||
                  VecTy->getVectorKind() == VectorKind::Generic)) {
      const LangOptions::LaxVectorConversionKind LVCKind =
          getLangOpts().getLaxVectorConversions();
      ASTContext &Context = getASTContext();

      // Can not convert between sve predicates and sve vectors because of
      // different size.
      if (BT->getKind() == BuiltinType::SveBool &&
          VecTy->getVectorKind() == VectorKind::SveFixedLengthData)
        return false;

      // If __ARM_FEATURE_SVE_BITS != N do not allow GNU vector lax conversion.
      // "Whenever __ARM_FEATURE_SVE_BITS==N, GNUT implicitly
      // converts to VLAT and VLAT implicitly converts to GNUT."
      // ACLE Spec Version 00bet6, 3.7.3.2. Behavior common to vectors and
      // predicates.
      if (VecTy->getVectorKind() == VectorKind::Generic &&
          Context.getTypeSize(SecondType) != getSVETypeSize(Context, BT))
        return false;

      // If -flax-vector-conversions=all is specified, the types are
      // certainly compatible.
      if (LVCKind == LangOptions::LaxVectorConversionKind::All)
        return true;

      // If -flax-vector-conversions=integer is specified, the types are
      // compatible if the elements are integer types.
      if (LVCKind == LangOptions::LaxVectorConversionKind::Integer)
        return VecTy->getElementType().getCanonicalType()->isIntegerType() &&
               FirstType->getSveEltType(Context)->isIntegerType();
    }

    return false;
  };

  return IsLaxCompatible(FirstType, SecondType) ||
         IsLaxCompatible(SecondType, FirstType);
}

} // namespace clang
