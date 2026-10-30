//===--- CIRGenOpenMPClause.h - OpenMP clause emitter -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENOPENMPCLAUSE_H
#define LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENOPENMPCLAUSE_H

#include "CIRGenBuilder.h"
#include "CIRGenModule.h"
#include "mlir/Dialect/OpenMP/OpenMPClauseOperands.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"

#include <type_traits>

namespace clang::CIRGen {

class CIRGenFunction;

/// Create an omp.map.info describing the local variable \p vd.
mlir::Value emitMapInfoForVar(CIRGenFunction &cgf, mlir::OpBuilder &builder,
                              mlir::Location loc, const VarDecl *vd,
                              mlir::omp::ClauseMapFlags mapFlags);

/// What the map operands of a construct are about.
struct OpenMPMapSyms {
  /// The variable of each map operand, in order. Null for the operand that maps
  /// the object that 'this' points to, which is what mapping a member of it
  /// comes down to.
  llvm::SmallVector<const VarDecl *> vars;
  /// Of the variables, the pointers of which the pointee is mapped rather than
  /// the pointer itself.
  llvm::SmallPtrSet<const VarDecl *, 4> pointee;
  bool mapsThis = false;
};

/// A type-only list of OpenMP clause AST node types.
template <typename... Clauses> struct OpenMPNYIClauseList {};

/// Emits OpenMP clauses for a directive, writing results into the
/// auto-generated ClauseOps from the OMP dialect.
class OpenMPClauseEmitter {
  CIRGenFunction &cgf;
  CIRGenModule &cgm;
  CIRGenBuilderTy &builder;
  mlir::Location loc;
  llvm::ArrayRef<const OMPClause *> clauses;

public:
  OpenMPClauseEmitter(CIRGenFunction &cgf, CIRGenModule &cgm,
                      CIRGenBuilderTy &builder, mlir::Location loc,
                      llvm::ArrayRef<const OMPClause *> clauses)
      : cgf(cgf), cgm(cgm), builder(builder), loc(loc), clauses(clauses) {}

  bool emitIf(mlir::omp::IfClauseOps &result,
              llvm::omp::Directive directiveName) const;

  /// Emit map clauses. The optional \p syms parameter collects what each map
  /// operand is about.
  bool emitMap(mlir::omp::MapClauseOps &result,
               OpenMPMapSyms *syms = nullptr) const;

  /// Emit is_device_ptr clauses: the pointers are already device addresses, so
  /// they are passed to the region as they are. The optional \p syms collects
  /// the VarDecl of each.
  bool emitIsDevicePtr(
      mlir::omp::TargetOperands &result,
      llvm::SmallVectorImpl<const VarDecl *> *syms = nullptr) const;

  bool emitNumThreads(mlir::omp::NumThreadsClauseOps &result) const;

  bool emitProcBind(mlir::omp::ProcBindClauseOps &result) const;

  /// Emit a schedule clause. Any chunk size is evaluated at the current
  /// insertion point. Returns failure if emission failed.
  mlir::LogicalResult emitSchedule(mlir::omp::ScheduleClauseOps &result) const;

  /// Verify the clauses of a directive to make sure all legal cases are either
  /// implemented or give a NYI error. The \p SupportedClauses and \p
  /// NYIClauses type lists must be disjoint and cover all clauses eligible for
  /// the directive being processed. Returns failure if any not-yet-implemented
  /// clause was present (and an error was emitted), success otherwise.
  template <typename... SupportedClauses, typename... NYIClauses>
  mlir::LogicalResult emitNYI(OpenMPNYIClauseList<NYIClauses...> nyi,
                              llvm::omp::Directive directive) const;

private:
  /// True if T is the same type as any of Ts.
  template <typename T, typename... Ts>
  static constexpr bool isAnyOf = (std::is_same_v<T, Ts> || ...);
};

template <typename... SupportedClauses, typename... NYIClauses>
mlir::LogicalResult
OpenMPClauseEmitter::emitNYI(OpenMPNYIClauseList<NYIClauses...>,
                             llvm::omp::Directive directive) const {
  static_assert(
      (!isAnyOf<NYIClauses, SupportedClauses...> && ...),
      "the supported and not-yet-implemented clause lists must be disjoint");

  mlir::LogicalResult result = mlir::success();
  for (const OMPClause *c : clauses) {
    if (isa<NYIClauses...>(c)) {
      std::string msg =
          (llvm::Twine("OpenMP ") +
           llvm::omp::getOpenMPDirectiveName(directive).upper() + " '" +
           llvm::omp::getOpenMPClauseName(c->getClauseKind()) + "' clause")
              .str();
      cgm.errorNYI(c->getBeginLoc(), msg);
      result = mlir::failure();
    } else if (!isa<SupportedClauses...>(c)) {
      // Unknown/illegal clause encountered.
      llvm_unreachable("unexpected OpenMP clause");
    }
  }
  return result;
}

} // namespace clang::CIRGen

#endif // LLVM_CLANG_LIB_CIR_CODEGEN_CIRGENOPENMPCLAUSE_H
