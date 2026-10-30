//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit OpenMP Stmt nodes as CIR code.
//
//===----------------------------------------------------------------------===//

#include "CIRGenBuilder.h"
#include "CIRGenFunction.h"
#include "CIRGenOpenMPClause.h"
#include "CIRGenOpenMPConstructDecomposition.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Frontend/OpenMP/OMP.h"
#include "llvm/Frontend/OpenMP/OMPConstants.h"

#include <list>
using namespace clang;
using namespace clang::CIRGen;

mlir::LogicalResult
CIRGenFunction::emitOMPScopeDirective(const OMPScopeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPScopeDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPErrorDirective(const OMPErrorDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPErrorDirective");
  return mlir::failure();
}

/// Report \p item's synthesized clauses as not-yet-implemented: having no AST
/// node, they cannot go through the clause emitters.
static mlir::LogicalResult
checkSynthesizedClauses(CIRGenFunction &cgf, const OMPExecutableDirective &s,
                        omp::ConstructQueue::const_iterator item) {
  mlir::LogicalResult res = mlir::success();
  for (llvm::omp::Clause synth : item->synthesized) {
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(),
                                   (llvm::Twine("OpenMP synthesized '") +
                                    llvm::omp::getOpenMPClauseName(synth) +
                                    "' clause from construct decomposition")
                                       .str());
    res = mlir::failure();
  }
  return res;
}

static mlir::LogicalResult
emitParallelClauses(CIRGenFunction &cgf, CIRGenModule &cgm,
                    CIRGenBuilderTy &builder, mlir::Location loc,
                    llvm::ArrayRef<const OMPClause *> clauses,
                    mlir::omp::ParallelOperands &clauseOps) {
  OpenMPClauseEmitter ce(cgf, cgm, builder, loc, clauses);
  ce.emitIf(clauseOps, llvm::omp::Directive::OMPD_parallel);
  ce.emitNumThreads(clauseOps);
  ce.emitProcBind(clauseOps);
  return ce.emitNYI</*supported=*/OMPIfClause, OMPNumThreadsClause,
                    OMPProcBindClause>(
      /*nyi=*/OpenMPNYIClauseList<OMPAllocateClause, OMPCopyinClause,
                                  OMPDefaultClause, OMPFirstprivateClause,
                                  OMPPrivateClause, OMPReductionClause,
                                  OMPSharedClause>{},
      llvm::omp::Directive::OMPD_parallel);
}

template <typename DirectiveTy>
static mlir::LogicalResult
emitParallelOp(CIRGenFunction &cgf, const DirectiveTy &s,
               const omp::ConstructQueue &queue,
               omp::ConstructQueue::const_iterator item, mlir::Location begin,
               mlir::Location end, const mlir::omp::ParallelOperands &clauseOps,
               llvm::function_ref<mlir::LogicalResult()> emitBody) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  CIRGenModule &cgm = cgf.getCIRGenModule();

  auto parallelOp = mlir::omp::ParallelOp::create(builder, begin, clauseOps);
  if (!omp::isLastItemInQueue(item, queue))
    parallelOp.setCombined(true);

  mlir::Block &block = parallelOp.getRegion().emplaceBlock();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&block);

  CIRGenFunction::LexicalScope ls{cgf, begin, builder.getInsertionBlock()};

  // hasCancel() reports a nested cancel/cancellation point directive in the
  // body, not a clause on `parallel`, so it can't be an emitParallelClauses()
  // NYI check.
  if (s.hasCancel()) {
    cgm.errorNYI(s.getBeginLoc(), "OpenMP Parallel with Cancel");
    return mlir::failure();
  }
  // Only set for reduction(task: ...), already rejected as NYI above.
  assert(!s.getTaskReductionRefExpr() &&
         "reduction(task: ...) should already be rejected as NYI");

  mlir::LogicalResult res = emitBody();
  mlir::omp::TerminatorOp::create(builder, end);
  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPParallelDirective(const OMPParallelDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  omp::ConstructQueue queue =
      omp::buildConstructQueue(getContext().getLangOpts().OpenMP, s);
  omp::ConstructQueue::const_iterator item = queue.begin();

  if (mlir::failed(checkSynthesizedClauses(*this, s, item)))
    return mlir::failure();

  mlir::omp::ParallelOperands clauseOps;
  if (mlir::failed(emitParallelClauses(*this, getCIRGenModule(), builder, begin,
                                       item->clauses, clauseOps)))
    return mlir::failure();

  return emitParallelOp(
      *this, s, queue, item, begin, end, clauseOps,
      [&]() -> mlir::LogicalResult {
        // emitStmt() rejects CapturedStmt directly; the parent construct
        // must unwrap it, so emit the inner statement instead.
        const CapturedStmt *cs = s.getCapturedStmt(llvm::omp::OMPD_parallel);
        return emitStmt(cs->getCapturedStmt(), /*useCurrentScope=*/true);
      });
}

mlir::LogicalResult
CIRGenFunction::emitOMPTaskwaitDirective(const OMPTaskwaitDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPTaskwaitDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskyieldDirective(const OMPTaskyieldDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTaskyieldDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPBarrierDirective(const OMPBarrierDirective &s) {
  mlir::omp::BarrierOp::create(builder, getLoc(s.getBeginLoc()));
  assert(s.clauses().empty() && "omp barrier doesn't support clauses");
  return mlir::success();
}
mlir::LogicalResult
CIRGenFunction::emitOMPMetaDirective(const OMPMetaDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPMetaDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPCanonicalLoop(const OMPCanonicalLoop &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPCanonicalLoop");
  return mlir::failure();
}
/// Emit the helper variables holding the loop bounds. OMPCapturedExprDecls are
/// skipped by emitDecl(), so they have to be emitted explicitly.
static mlir::LogicalResult emitLoopPreInits(CIRGenFunction &cgf,
                                            const OMPLoopDirective &s) {
  const Stmt *preInits = s.getPreInits();
  if (!preInits)
    return mlir::success();
  llvm::SmallVector<const Stmt *> preInitStmts;
  if (const auto *compound = dyn_cast<CompoundStmt>(preInits))
    llvm::append_range(preInitStmts, compound->body());
  else
    preInitStmts.push_back(preInits);
  for (const Stmt *preInit : preInitStmts) {
    if (const auto *declStmt = dyn_cast<DeclStmt>(preInit)) {
      for (const Decl *d : declStmt->decls())
        cgf.emitVarDecl(*cast<VarDecl>(d));
      continue;
    }
    if (mlir::failed(cgf.emitStmt(preInit, /*useCurrentScope=*/true)))
      return mlir::failure();
  }
  return mlir::success();
}

/// Non-rectangular loop nests need the dependent counters to be materialized
/// before the pre-condition is evaluated, which isn't implemented.
static mlir::LogicalResult checkRectangularLoopNest(CIRGenFunction &cgf,
                                                    const OMPLoopDirective &s,
                                                    llvm::StringRef what) {
  bool nonRectangular = llvm::any_of(s.dependent_counters(),
                                     [](const Expr *e) { return e; }) ||
                        llvm::any_of(s.finals_conditions(),
                                     [](const Expr *e) { return e; });
  if (nonRectangular) {
    cgf.getCIRGenModule().errorNYI(s.getSourceRange(),
                                   (llvm::Twine("OpenMP ") + what +
                                    " with non-rectangular loop")
                                       .str());
    return mlir::failure();
  }
  return mlir::success();
}

/// Emit `#pragma omp simd` the way classic codegen does (emitOMPSimdRegion):
/// an ordinary counted loop, `cir.for`, over the normalized iteration variable,
/// carrying vectorization hints. No omp dialect operations are generated.
///
///   <pre-inits>
///   <counter = init>
///   if (PreCond) {
///     IV = init; [LastIteration = ...;]
///     <privatize 'private' clause variables>
///     for (; IV <= LastIteration; ++IV) { <counter = update(IV)>; BODY; }
///     <final counter values>
///   }
///
/// Currently only the 'private' and 'collapse' clauses, and rectangular loop
/// nests, are supported.
mlir::LogicalResult
CIRGenFunction::emitOMPSimdDirective(const OMPSimdDirective &s) {
  CIRGenModule &cgm = getCIRGenModule();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  llvm::SmallVector<const OMPClause *> clauses(s.clauses().begin(),
                                               s.clauses().end());
  OpenMPClauseEmitter ce(*this, cgm, builder, begin, clauses);
  if (mlir::failed(ce.emitNYI</*supported=*/OMPPrivateClause,
                              OMPCollapseClause, OMPReductionClause>(
          /*nyi=*/OpenMPNYIClauseList<OMPAlignedClause, OMPAllocateClause,
                                      OMPIfClause, OMPLastprivateClause,
                                      OMPLinearClause, OMPNontemporalClause,
                                      OMPOrderClause,
                                      OMPSafelenClause, OMPSimdlenClause>{},
          llvm::omp::Directive::OMPD_simd)))
    return mlir::failure();

  if (mlir::failed(checkRectangularLoopNest(*this, s, "simd")))
    return mlir::failure();

  mlir::LogicalResult res = mlir::success();

  // Registrations in localDeclMap that must be undone once the construct has
  // been emitted. A list, because the RAII object isn't movable.
  std::list<DeclMapRevertingRAII> privatized;

  // Makes references to \p orig use the storage at \p addr for the lifetime of
  // this construct.
  auto privatize = [&](const VarDecl *orig, Address addr) {
    privatized.emplace_back(*this, orig);
    replaceAddrOfLocalVar(orig, addr);
  };

  // Everything the construct declares (captured expressions, the iteration
  // variable, privatized variables) lives in a scope of its own.
  auto simdBuilder = [&](mlir::OpBuilder &, mlir::Location scopeLoc) {
    LexicalScope lexScope{*this, scopeLoc, builder.getInsertionBlock()};

    if (mlir::failed(emitLoopPreInits(*this, s))) {
      res = mlir::failure();
      return;
    }

    // The loop counters. A counter declared by the loop itself, as in
    // `for (int i...)`, has no storage yet: give it the private copy that
    // Sema made. A counter declared before the loop is used in place, and
    // gets its final value assigned after the loop (see below).
    llvm::SmallVector<bool> counterIsOuter;
    for (auto [counter, privateCounter] :
         llvm::zip(s.counters(), s.private_counters())) {
      const auto *vd = cast<VarDecl>(cast<DeclRefExpr>(counter)->getDecl());
      if (vd->hasGlobalStorage()) {
        cgm.errorNYI(counter->getSourceRange(),
                     "OpenMP simd with a global loop counter");
        res = mlir::failure();
        return;
      }
      bool isOuter = localDeclMap.count(vd);
      counterIsOuter.push_back(isOuter);
      if (isOuter)
        continue;
      const auto *privateVD =
          cast<VarDecl>(cast<DeclRefExpr>(privateCounter)->getDecl());
      privatized.emplace_back(*this, privateVD);
      AutoVarEmission emission = emitAutoVarAlloca(*privateVD);
      emitAutoVarCleanups(emission);
      privatize(vd, emission.getAllocatedAddress());
    }

    // Initial values of the counters, which the pre-condition is made of.
    for (const Expr *init : s.inits())
      emitIgnoredExpr(init);

    auto loopBuilder = [&](mlir::OpBuilder &, mlir::Location loopLoc) {
      LexicalScope lexScope{*this, loopLoc, builder.getInsertionBlock()};

      // The normalized iteration variable and the iteration count.
      const auto *ivDecl =
          cast<VarDecl>(cast<DeclRefExpr>(s.getIterationVariable())->getDecl());
      emitVarDecl(*ivDecl);
      emitIgnoredExpr(s.getInit());
      // If the iteration count isn't a variable, Sema has decided to
      // recompute it at every use.
      if (const auto *lastIter = dyn_cast<DeclRefExpr>(s.getLastIteration())) {
        emitVarDecl(*cast<VarDecl>(lastIter->getDecl()));
        emitIgnoredExpr(s.getCalcLastIteration());
      }

      // private(x): each variable gets storage of its own, which is used
      // instead of the original inside the construct.
      llvm::SmallPtrSet<const VarDecl *, 4> seen;
      for (const auto *c : s.getClausesOfKind<OMPPrivateClause>()) {
        for (auto [ref, copy] : llvm::zip(c->varlist(), c->private_copies())) {
          const auto *orig =
              dyn_cast<VarDecl>(cast<DeclRefExpr>(ref)->getDecl());
          if (!orig) {
            cgm.errorNYI(ref->getSourceRange(),
                         "OpenMP simd private clause on a non-variable");
            res = mlir::failure();
            return;
          }
          if (!seen.insert(orig->getCanonicalDecl()).second)
            continue;
          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(copy)->getDecl());
          privatized.emplace_back(*this, privateVD);
          AutoVarEmission emission = emitAutoVarAlloca(*privateVD);
          emitAutoVarInit(emission);
          emitAutoVarCleanups(emission);
          privatize(orig, emission.getAllocatedAddress());
        }
      }

      // reduction(op: x): the loop works on a copy of x that starts out as the
      // identity of op, which keeps the iterations independent of each other.
      // The copies are combined into the originals after the loop.
      struct Reduction {
        const VarDecl *lhs, *rhs;
        Address orig, priv;
        const Expr *combiner;
      };
      llvm::SmallVector<Reduction> reductions;
      for (const auto *c : s.getClausesOfKind<OMPReductionClause>()) {
        if (c->getModifier() != OMPC_REDUCTION_unknown &&
            c->getModifier() != OMPC_REDUCTION_default) {
          cgm.errorNYI(c->getBeginLoc(), "OpenMP simd reduction modifier");
          res = mlir::failure();
          return;
        }
        for (auto [ref, privRef, lhs, rhs, op] :
             llvm::zip(c->varlist(), c->privates(), c->lhs_exprs(),
                       c->rhs_exprs(), c->reduction_ops())) {
          const auto *orig =
              dyn_cast<VarDecl>(cast<DeclRefExpr>(ref)->getDecl());
          // Only the operators of the language, applied to a scalar.
          const auto *combiner =
              dyn_cast<BinaryOperator>(op->IgnoreParenImpCasts());
          if (!orig || !orig->getType()->isScalarType() ||
              orig->getType()->isReferenceType() || !combiner ||
              !combiner->isAssignmentOp()) {
            cgm.errorNYI(ref->getSourceRange(),
                         "OpenMP simd reduction of this variable or with a "
                         "user-defined reduction");
            res = mlir::failure();
            return;
          }
          Address origAddr = emitLValue(ref).getAddress();
          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(privRef)->getDecl());
          privatized.emplace_back(*this, privateVD);
          AutoVarEmission emission = emitAutoVarAlloca(*privateVD);
          emitAutoVarInit(emission);
          emitAutoVarCleanups(emission);
          Address privAddr = emission.getAllocatedAddress();
          privatize(orig, privAddr);
          reductions.push_back(
              {cast<VarDecl>(cast<DeclRefExpr>(lhs)->getDecl()),
               cast<VarDecl>(cast<DeclRefExpr>(rhs)->getDecl()), origAddr,
               privAddr, op});
        }
      }

      // The loop proper.
      cir::ForOp forOp = builder.createFor(
          loopLoc,
          /*condBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            builder.createCondition(evaluateExprAsBool(s.getCond()));
          },
          /*bodyBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location) {
            RunCleanupsScope bodyScope(*this);
            // Counters of this iteration, from the iteration variable.
            for (const Expr *update : s.updates())
              emitIgnoredExpr(update);
            if (mlir::failed(emitStmt(s.getBody(), /*useCurrentScope=*/false)))
              res = mlir::failure();
            emitStopPoint(&s);
          },
          /*stepBuilder=*/
          [&](mlir::OpBuilder &, mlir::Location stepLoc) {
            emitIgnoredExpr(s.getInc());
            builder.createYield(stepLoc);
          });

      // Iterations may run concurrently (there is no safelen), and the loop is
      // to be vectorized.
      mlir::MLIRContext *ctx = &getMLIRContext();
      mlir::UnitAttr unit = mlir::UnitAttr::get(ctx);
      auto hints = cir::LoopHintAttr::get(
          ctx, /*must_progress=*/mlir::UnitAttr(), /*vectorize_enable=*/unit,
          /*vectorize_width=*/0, /*vectorize_scalable=*/mlir::BoolAttr(),
          /*vectorize_predicate=*/mlir::BoolAttr(), /*interleave_count=*/0,
          /*unroll=*/std::nullopt, /*unroll_count=*/0,
          /*unroll_and_jam=*/std::nullopt, /*unroll_and_jam_count=*/0,
          /*distribute=*/mlir::BoolAttr(),
          /*pipeline_disabled=*/mlir::UnitAttr(),
          /*pipeline_initiation_interval=*/0,
          /*licm_disabled=*/mlir::UnitAttr(), /*parallel=*/unit,
          /*code_align=*/0);
      cir::LoopOpInterface loopOp = forOp;
      loopOp.setLoopHintsAttr(hints);
      loopOp.setLoopIdAttr(mlir::DistinctAttr::create(unit));
      terminateStructuredRegionBody(forOp.getBody(), end);

      // The combiners are written in terms of two helper variables: the
      // original (the result) and the private copy.
      for (const Reduction &r : reductions) {
        privatized.emplace_back(*this, r.lhs);
        privatize(r.lhs, r.orig);
        privatized.emplace_back(*this, r.rhs);
        privatize(r.rhs, r.priv);
        emitIgnoredExpr(r.combiner);
      }

      // A counter that exists outside of the loop holds, afterwards, the value
      // it would have after the last iteration.
      for (auto [isOuter, finalExpr] : llvm::zip(counterIsOuter, s.finals()))
        if (isOuter)
          emitIgnoredExpr(finalExpr);
    };

    // Run the loop only if it iterates at least once, unless that is known.
    bool preCond = true;
    if (constantFoldsToBool(s.getPreCond(), preCond)) {
      if (preCond)
        loopBuilder(builder, begin);
      return;
    }
    emitIfOnBoolExpr(s.getPreCond(), /*thenBuilder=*/loopBuilder, begin,
                     /*elseBuilder=*/nullptr);
  };

  cir::ScopeOp::create(builder, begin, /*scopeBuilder=*/simdBuilder);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPTileDirective(const OMPTileDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPTileDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPUnrollDirective(const OMPUnrollDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPUnrollDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPFuseDirective(const OMPFuseDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPFuseDirective");
  return mlir::failure();
}
/// Emit `#pragma omp for` as a worksharing loop over the normalized iteration
/// variable, the way classic codegen sees it (the loop nest is collapsed into
/// a single iteration space by Sema):
///
///   <pre-inits>
///   <privatize counters>
///   if (PreCond) {
///     IV; LastIteration = ...;
///     <privatize 'private' clause variables>
///     omp.wsloop { omp.loop_nest (%iv) = (0) to (LastIteration) inclusive
///                  step (1) { IV = %iv; <counter = update(IV)>; BODY } }
///   }
///
/// Currently only the 'private', 'collapse' and 'nowait' clauses, and
/// rectangular loop nests, are supported.
mlir::LogicalResult
CIRGenFunction::emitOMPForDirective(const OMPForDirective &s) {
  CIRGenModule &cgm = getCIRGenModule();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  llvm::SmallVector<const OMPClause *> clauses(s.clauses().begin(),
                                               s.clauses().end());
  OpenMPClauseEmitter ce(*this, cgm, builder, begin, clauses);
  if (mlir::failed(ce.emitNYI</*supported=*/OMPPrivateClause, OMPCollapseClause,
                              OMPNowaitClause, OMPScheduleClause>(
          /*nyi=*/OpenMPNYIClauseList<OMPAllocateClause, OMPFirstprivateClause,
                                      OMPLastprivateClause, OMPLinearClause,
                                      OMPOrderClause, OMPOrderedClause,
                                      OMPReductionClause>{},
          llvm::omp::Directive::OMPD_for)))
    return mlir::failure();

  if (mlir::failed(checkRectangularLoopNest(*this, s, "for")))
    return mlir::failure();

  if (s.hasCancel()) {
    cgm.errorNYI(s.getSourceRange(), "OpenMP for with Cancel");
    return mlir::failure();
  }

  const auto *ivDecl =
      cast<VarDecl>(cast<DeclRefExpr>(s.getIterationVariable())->getDecl());
  // The omp dialect only has signed loops. An unsigned iteration variable is
  // widened to a signed 64-bit loop, which has room for all of its values; a
  // 64-bit one has nothing to widen to.
  if (!ivDecl->getType()->isSignedIntegerType() &&
      getContext().getTypeSize(ivDecl->getType()) >= 64) {
    cgm.errorNYI(s.getSourceRange(),
                 "OpenMP for with a 64-bit unsigned iteration variable");
    return mlir::failure();
  }

  mlir::LogicalResult res = mlir::success();

  // Registrations in localDeclMap that must be undone once the construct has
  // been emitted. A list, because the RAII object isn't movable.
  std::list<DeclMapRevertingRAII> privatized;
  auto privatize = [&](const VarDecl *orig, Address addr) {
    privatized.emplace_back(*this, orig);
    replaceAddrOfLocalVar(orig, addr);
  };

  auto forBuilder = [&](mlir::OpBuilder &, mlir::Location scopeLoc) {
    LexicalScope lexScope{*this, scopeLoc, builder.getInsertionBlock()};

    if (mlir::failed(emitLoopPreInits(*this, s))) {
      res = mlir::failure();
      return;
    }

    // Loop counters are private to each thread, including those declared
    // before the loop, so the original is left untouched.
    for (auto [counter, privateCounter] :
         llvm::zip(s.counters(), s.private_counters())) {
      const auto *vd = cast<VarDecl>(cast<DeclRefExpr>(counter)->getDecl());
      if (vd->hasGlobalStorage()) {
        cgm.errorNYI(counter->getSourceRange(),
                     "OpenMP for with a global loop counter");
        res = mlir::failure();
        return;
      }
      const auto *privateVD =
          cast<VarDecl>(cast<DeclRefExpr>(privateCounter)->getDecl());
      privatized.emplace_back(*this, privateVD);
      AutoVarEmission emission = emitAutoVarAlloca(*privateVD);
      emitAutoVarCleanups(emission);
      privatize(vd, emission.getAllocatedAddress());
    }

    // Initial values of the counters, which the pre-condition is made of.
    for (const Expr *init : s.inits())
      emitIgnoredExpr(init);

    auto loopBuilder = [&](mlir::OpBuilder &, mlir::Location loopLoc) {
      LexicalScope lexScope{*this, loopLoc, builder.getInsertionBlock()};

      // The normalized iteration variable (assigned by the loop itself) and
      // the iteration count.
      emitVarDecl(*ivDecl);
      // If the iteration count isn't a variable, Sema has decided to
      // recompute it at every use.
      if (const auto *lastIter = dyn_cast<DeclRefExpr>(s.getLastIteration())) {
        emitVarDecl(*cast<VarDecl>(lastIter->getDecl()));
        emitIgnoredExpr(s.getCalcLastIteration());
      }

      // private(x): each variable gets storage of its own, which is used
      // instead of the original inside the construct.
      llvm::SmallPtrSet<const VarDecl *, 4> seen;
      for (const auto *c : s.getClausesOfKind<OMPPrivateClause>()) {
        for (auto [ref, copy] : llvm::zip(c->varlist(), c->private_copies())) {
          const auto *orig =
              dyn_cast<VarDecl>(cast<DeclRefExpr>(ref)->getDecl());
          if (!orig) {
            cgm.errorNYI(ref->getSourceRange(),
                         "OpenMP for private clause on a non-variable");
            res = mlir::failure();
            return;
          }
          if (!seen.insert(orig->getCanonicalDecl()).second)
            continue;
          const auto *privateVD =
              cast<VarDecl>(cast<DeclRefExpr>(copy)->getDecl());
          privatized.emplace_back(*this, privateVD);
          AutoVarEmission emission = emitAutoVarAlloca(*privateVD);
          emitAutoVarInit(emission);
          emitAutoVarCleanups(emission);
          privatize(orig, emission.getAllocatedAddress());
        }
      }

      // The iteration space, as builtin integers as the omp dialect wants.
      mlir::Type cirIVTy = convertType(ivDecl->getType());
      cir::IntType loopTy = cast<cir::IntType>(cirIVTy);
      if (!loopTy.isSigned())
        loopTy = builder.getSInt64Ty();
      mlir::Type builtinIVTy = builder.getIntegerType(loopTy.getWidth());
      mlir::Value lb = builder.createBuiltinIntCast(
          builder.getConstInt(loopLoc, loopTy, 0), builtinIVTy);
      mlir::Value ub = builder.createBuiltinIntCast(
          builder.createIntCast(emitScalarExpr(s.getLastIteration()), loopTy),
          builtinIVTy);
      mlir::Value step = builder.createBuiltinIntCast(
          builder.getConstInt(loopLoc, loopTy, 1), builtinIVTy);

      mlir::omp::WsloopOperands wsOps;
      if (mlir::failed(ce.emitSchedule(wsOps))) {
        res = mlir::failure();
        return;
      }
      wsOps.nowait = s.getSingleClause<OMPNowaitClause>() ? builder.getUnitAttr()
                                                          : mlir::UnitAttr();
      auto wsloop = mlir::omp::WsloopOp::create(builder, loopLoc, wsOps);
      mlir::Block &wsBlock = wsloop.getRegion().emplaceBlock();

      mlir::OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToEnd(&wsBlock);

      mlir::omp::LoopNestOperands nestOps;
      nestOps.loopLowerBounds.push_back(lb);
      nestOps.loopUpperBounds.push_back(ub);
      nestOps.loopSteps.push_back(step);
      nestOps.loopInclusive = builder.getUnitAttr();
      auto nest = mlir::omp::LoopNestOp::create(builder, loopLoc, nestOps);
      mlir::Block &body = nest.getRegion().emplaceBlock();
      mlir::Value ivArg = body.addArgument(builtinIVTy, loopLoc);
      builder.setInsertionPointToEnd(&body);

      {
        LexicalScope bodyScope{*this, loopLoc, &body};
        RunCleanupsScope cleanups(*this);

        // Make the iteration variable hold the value of this iteration, then
        // derive the counters of this iteration from it.
        mlir::Value iv = builder.createIntCast(
            builder.createBuiltinIntCast(ivArg, loopTy), cirIVTy);
        builder.createStore(loopLoc, iv,
                            emitLValue(s.getIterationVariable()).getAddress());
        for (const Expr *update : s.updates())
          emitIgnoredExpr(update);
        if (mlir::failed(emitStmt(s.getBody(), /*useCurrentScope=*/false)))
          res = mlir::failure();
        emitStopPoint(&s);
        cleanups.forceCleanup();
        mlir::omp::YieldOp::create(builder, end);
      }
    };

    // Run the loop only if it iterates at least once, unless that is known.
    bool preCond = true;
    if (constantFoldsToBool(s.getPreCond(), preCond)) {
      if (preCond)
        loopBuilder(builder, begin);
      return;
    }
    emitIfOnBoolExpr(s.getPreCond(), /*thenBuilder=*/loopBuilder, begin,
                     /*elseBuilder=*/nullptr);
  };

  cir::ScopeOp::create(builder, begin, /*scopeBuilder=*/forBuilder);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPForSimdDirective(const OMPForSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPSectionsDirective(const OMPSectionsDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPSectionsDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPSectionDirective(const OMPSectionDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPSectionDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPSingleDirective(const OMPSingleDirective &s) {
  CIRGenModule &cgm = getCIRGenModule();
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  llvm::SmallVector<const OMPClause *> clauses(s.clauses().begin(),
                                               s.clauses().end());
  OpenMPClauseEmitter ce(*this, cgm, builder, begin, clauses);
  if (mlir::failed(ce.emitNYI</*supported=*/OMPNowaitClause>(
          /*nyi=*/OpenMPNYIClauseList<OMPAllocateClause, OMPCopyprivateClause,
                                      OMPFirstprivateClause,
                                      OMPPrivateClause>{},
          llvm::omp::Directive::OMPD_single)))
    return mlir::failure();

  // The threads that don't execute the block wait for the one that does at
  // the end of the construct, unless told not to.
  mlir::omp::SingleOperands clauseOps;
  if (s.getSingleClause<OMPNowaitClause>())
    clauseOps.nowait = builder.getUnitAttr();

  auto singleOp = mlir::omp::SingleOp::create(builder, begin, clauseOps);
  mlir::Block &block = singleOp.getRegion().emplaceBlock();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&block);

  CIRGenFunction::LexicalScope ls{*this, begin, builder.getInsertionBlock()};
  mlir::LogicalResult res =
      emitStmt(s.getStructuredBlock(), /*useCurrentScope=*/true);
  mlir::omp::TerminatorOp::create(builder, end);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPMasterDirective(const OMPMasterDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  // 'master' has no clauses.
  assert(s.clauses().empty() && "omp master doesn't support clauses");

  auto masterOp = mlir::omp::MasterOp::create(builder, begin);
  mlir::Block &block = masterOp.getRegion().emplaceBlock();
  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&block);

  CIRGenFunction::LexicalScope ls{*this, begin, builder.getInsertionBlock()};
  mlir::LogicalResult res =
      emitStmt(s.getStructuredBlock(), /*useCurrentScope=*/true);
  mlir::omp::TerminatorOp::create(builder, end);
  return res;
}
mlir::LogicalResult
CIRGenFunction::emitOMPCriticalDirective(const OMPCriticalDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPCriticalDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPParallelForDirective(const OMPParallelForDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelForSimdDirective(
    const OMPParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterDirective(
    const OMPParallelMasterDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMasterDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelSectionsDirective(
    const OMPParallelSectionsDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelSectionsDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskDirective(const OMPTaskDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPTaskDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskgroupDirective(const OMPTaskgroupDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTaskgroupDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPFlushDirective(const OMPFlushDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPFlushDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPDepobjDirective(const OMPDepobjDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPDepobjDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPScanDirective(const OMPScanDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPScanDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPOrderedStandaloneDirective(
    const OMPOrderedStandaloneDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPOrderedStandaloneDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPOrderedBlockAssocDirective(
    const OMPOrderedBlockAssocDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPOrderedBlockAssocDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPAtomicDirective(const OMPAtomicDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPAtomicDirective");
  return mlir::failure();
}

/// Check for unsupported implicit captures in a target region.
static mlir::LogicalResult
emitOMPTargetImplicitCaptures(CIRGenFunction &cgf,
                              const OMPExecutableDirective &s,
                              llvm::ArrayRef<const VarDecl *> mapSyms,
                              llvm::ArrayRef<const VarDecl *> privateSyms) {
  const CapturedStmt *cs = s.getCapturedStmt(llvm::omp::OMPD_target);
  mlir::LogicalResult res = mlir::success();
  for (const auto &capture : cs->captures()) {
    if (capture.capturesThis()) {
      cgf.getCIRGenModule().errorNYI(s.getBeginLoc(),
                                     "OpenMP target capture of 'this' pointer");
      res = mlir::failure();
      continue;
    }
    if (capture.capturesVariableByCopy()) {
      // The variable is privatized by a firstprivate clause.
      if (llvm::is_contained(privateSyms, capture.getCapturedVar()))
        continue;
      cgf.getCIRGenModule().errorNYI(s.getBeginLoc(),
                                     "OpenMP target capture by copy");
      res = mlir::failure();
      continue;
    }
    if (capture.capturesVariableArrayType()) {
      cgf.getCIRGenModule().errorNYI(
          s.getBeginLoc(),
          "OpenMP target capture of variable-length array type");
      res = mlir::failure();
      continue;
    }
    if (capture.capturesVariable()) {
      const VarDecl *vd = capture.getCapturedVar();
      if (llvm::is_contained(mapSyms, vd))
        continue;

      cgf.getCIRGenModule().errorNYI(s.getBeginLoc(),
                                     "OpenMP target implicit by-ref capture");
      res = mlir::failure();
    }
  }
  return res;
}

/// The recipe for privatizing a scalar of type \p elemTy with the value it has
/// outside of the construct, creating it if this is the first use.
static mlir::omp::PrivateClauseOp
getOrCreateFirstprivateRecipe(CIRGenFunction &cgf, mlir::Location loc,
                              mlir::Type elemTy) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::ModuleOp module = cgf.getCIRGenModule().getModule();

  std::string name;
  llvm::raw_string_ostream os(name);
  os << "firstprivate.";
  elemTy.print(os);
  // Only characters that can be part of a symbol name without quoting.
  for (char &c : name)
    if (!llvm::isAlnum(c) && c != '.')
      c = '_';

  if (auto existing = module.lookupSymbol<mlir::omp::PrivateClauseOp>(name))
    return existing;

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(module.getBody());
  auto recipe = mlir::omp::PrivateClauseOp::create(
      builder, loc, name, /*sym_visibility=*/mlir::StringAttr{}, elemTy,
      mlir::omp::DataSharingClauseType::FirstPrivate);

  // %arg0 is the original variable, %arg1 the storage of the private copy.
  auto ptrTy = cir::PointerType::get(builder.getContext(), elemTy);
  mlir::Block *copy = builder.createBlock(&recipe.getCopyRegion(), {},
                                          {ptrTy, ptrTy}, {loc, loc});
  mlir::Value orig = cir::LoadOp::create(builder, loc, copy->getArgument(0));
  cir::StoreOp::create(builder, loc, orig, copy->getArgument(1));
  mlir::omp::YieldOp::create(builder, loc, copy->getArgument(1));
  return recipe;
}

static mlir::LogicalResult
emitTargetClauses(CIRGenFunction &cgf, CIRGenModule &cgm,
                  CIRGenBuilderTy &builder, mlir::Location loc,
                  llvm::ArrayRef<const OMPClause *> clauses,
                  llvm::ArrayRef<const VarDecl *> firstprivateVars,
                  mlir::omp::TargetExtOperands &clauseOps,
                  llvm::SmallVectorImpl<const VarDecl *> &mapSyms,
                  llvm::SmallVectorImpl<const VarDecl *> &privateSyms,
                  llvm::SmallVectorImpl<int64_t> &privateMaps) {
  mlir::LogicalResult res = mlir::success();
  OpenMPClauseEmitter ce(cgf, cgm, builder, loc, clauses);
  ce.emitMap(clauseOps, &mapSyms);

  // firstprivate(x): the target region gets a copy of x, initialized from the
  // value it has outside, which is mapped over to the device for that.
  for (const VarDecl *vd : firstprivateVars) {
    if (!vd->getType()->isScalarType() || vd->getType()->isReferenceType() ||
        vd->hasGlobalStorage()) {
      cgm.errorNYI(vd->getSourceRange(),
                   "OpenMP target firstprivate of a variable that is not a "
                   "local scalar");
      res = mlir::failure();
      continue;
    }
    Address addr = cgf.getAddrOfLocalVar(vd);
    mlir::Type elemTy =
        mlir::cast<cir::PointerType>(addr.getPointer().getType()).getPointee();
    mlir::omp::PrivateClauseOp recipe =
        getOrCreateFirstprivateRecipe(cgf, loc, elemTy);

    privateMaps.push_back(clauseOps.mapVars.size());
    clauseOps.mapVars.push_back(emitMapInfoForVar(
        cgf, builder, loc, vd, mlir::omp::ClauseMapFlags::to));
    clauseOps.privateVars.push_back(addr.getPointer());
    clauseOps.privateSyms.push_back(mlir::SymbolRefAttr::get(recipe));
    privateSyms.push_back(vd);
  }
  if (mlir::failed(res))
    return res;

  return ce.emitNYI</*supported=*/OMPMapClause>(
      /*nyi=*/OpenMPNYIClauseList<
          OMPAllocateClause, OMPDefaultClause, OMPDefaultmapClause,
          OMPDependClause, OMPDeviceClause, OMPFirstprivateClause,
          OMPHasDeviceAddrClause, OMPIfClause, OMPInReductionClause,
          OMPIsDevicePtrClause, OMPNowaitClause, OMPPrivateClause,
          OMPThreadLimitClause, OMPUsesAllocatorsClause, OMPXBareClause>{},
      llvm::omp::Directive::OMPD_target);
}

template <typename DirectiveTy>
static mlir::LogicalResult
emitTargetOp(CIRGenFunction &cgf, const DirectiveTy &s,
             const omp::ConstructQueue &queue,
             omp::ConstructQueue::const_iterator item, mlir::Location begin,
             mlir::Location end, mlir::omp::TargetExtOperands &clauseOps,
             llvm::ArrayRef<const VarDecl *> mapSyms,
             llvm::ArrayRef<const VarDecl *> privateSyms,
             llvm::ArrayRef<int64_t> privateMaps,
             llvm::function_ref<mlir::LogicalResult()> emitBody) {
  CIRGenBuilderTy &builder = cgf.getBuilder();

  if (mlir::failed(emitOMPTargetImplicitCaptures(cgf, s, mapSyms, privateSyms)))
    return mlir::failure();

  // Use generic for now.
  clauseOps.kernelType = mlir::omp::TargetExecModeAttr::get(
      &cgf.getMLIRContext(), mlir::omp::TargetExecMode::generic);

  auto targetOp = mlir::omp::TargetOp::create(builder, begin, clauseOps);
  if (!privateMaps.empty())
    targetOp.setPrivateMapsAttr(builder.getDenseI64ArrayAttr(privateMaps));
  if (!omp::isLastItemInQueue(item, queue))
    targetOp.setCombined(true);

  mlir::Block &block = targetOp.getRegion().emplaceBlock();
  for (mlir::Value mapVar : clauseOps.mapVars)
    block.addArgument(mapVar.getType(), begin);
  for (mlir::Value privateVar : clauseOps.privateVars)
    block.addArgument(privateVar.getType(), begin);

  mlir::OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(&block);

  CIRGenFunction::LexicalScope ls{cgf, begin, builder.getInsertionBlock()};

  llvm::SmallVector<std::pair<const VarDecl *, Address>> savedAddrs;
  for (auto [idx, vd] : llvm::enumerate(mapSyms)) {
    Address origAddr = cgf.getAddrOfLocalVar(vd);
    savedAddrs.push_back({vd, origAddr});
    mlir::Value blockArg = block.getArgument(idx);
    cgf.replaceAddrOfLocalVar(vd, Address(blockArg, origAddr.getAlignment()));
  }

  // The region sees a firstprivate variable through its private copy.
  for (auto [idx, vd] : llvm::enumerate(privateSyms)) {
    Address origAddr = cgf.getAddrOfLocalVar(vd);
    savedAddrs.push_back({vd, origAddr});
    mlir::Value blockArg = block.getArgument(clauseOps.mapVars.size() + idx);
    cgf.replaceAddrOfLocalVar(vd, Address(blockArg, origAddr.getAlignment()));
  }

  mlir::LogicalResult res = emitBody();
  mlir::omp::TerminatorOp::create(builder, end);

  for (auto &[vd, addr] : savedAddrs)
    cgf.replaceAddrOfLocalVar(vd, addr);

  return res;
}

mlir::LogicalResult
CIRGenFunction::emitOMPTargetDirective(const OMPTargetDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  omp::ConstructQueue queue =
      omp::buildConstructQueue(getContext().getLangOpts().OpenMP, s);
  omp::ConstructQueue::const_iterator item = queue.begin();

  if (mlir::failed(checkSynthesizedClauses(*this, s, item)))
    return mlir::failure();

  mlir::omp::TargetExtOperands clauseOps;
  llvm::SmallVector<const VarDecl *> mapSyms, privateSyms;
  llvm::SmallVector<int64_t> privateMaps;
  if (mlir::failed(emitTargetClauses(*this, getCIRGenModule(), builder, begin,
                                     item->clauses, item->firstprivateVars,
                                     clauseOps, mapSyms, privateSyms,
                                     privateMaps)))
    return mlir::failure();

  return emitTargetOp(*this, s, queue, item, begin, end, clauseOps, mapSyms,
                      privateSyms, privateMaps,
                      [&]() -> mlir::LogicalResult {
                        const CapturedStmt *cs =
                            s.getCapturedStmt(llvm::omp::OMPD_target);
                        return emitStmt(cs->getCapturedStmt(),
                                        /*useCurrentScope=*/true);
                      });
}
mlir::LogicalResult
CIRGenFunction::emitOMPTeamsDirective(const OMPTeamsDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPTeamsDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPCancellationPointDirective(
    const OMPCancellationPointDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPCancellationPointDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPCancelDirective(const OMPCancelDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPCancelDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetDataDirective(const OMPTargetDataDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetEnterDataDirective(
    const OMPTargetEnterDataDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetEnterDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetExitDataDirective(
    const OMPTargetExitDataDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetExitDataDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelDirective(
    const OMPTargetParallelDirective &s) {
  mlir::Location begin = getLoc(s.getBeginLoc());
  mlir::Location end = getLoc(s.getEndLoc());

  omp::ConstructQueue queue =
      omp::buildConstructQueue(getContext().getLangOpts().OpenMP, s);
  omp::ConstructQueue::const_iterator targetItem = queue.begin();
  assert(targetItem->id == llvm::omp::OMPD_target &&
         "expected 'target' to be the outermost leaf");

  if (mlir::failed(checkSynthesizedClauses(*this, s, targetItem)))
    return mlir::failure();

  mlir::omp::TargetExtOperands targetOps;
  llvm::SmallVector<const VarDecl *> mapSyms, privateSyms;
  llvm::SmallVector<int64_t> privateMaps;
  if (mlir::failed(emitTargetClauses(
          *this, getCIRGenModule(), builder, begin, targetItem->clauses,
          targetItem->firstprivateVars, targetOps, mapSyms, privateSyms,
          privateMaps)))
    return mlir::failure();

  return emitTargetOp(
      *this, s, queue, targetItem, begin, end, targetOps, mapSyms, privateSyms,
      privateMaps,
      [&]() -> mlir::LogicalResult {
        omp::ConstructQueue::const_iterator parallelItem =
            std::next(targetItem);
        assert(parallelItem != queue.end() &&
               parallelItem->id == llvm::omp::OMPD_parallel &&
               "expected a 'parallel' leaf nested in 'target'");

        if (mlir::failed(checkSynthesizedClauses(*this, s, parallelItem)))
          return mlir::failure();

        mlir::omp::ParallelOperands parallelOps;
        if (mlir::failed(emitParallelClauses(*this, getCIRGenModule(), builder,
                                             begin, parallelItem->clauses,
                                             parallelOps)))
          return mlir::failure();

        return emitParallelOp(*this, s, queue, parallelItem, begin, end,
                              parallelOps, [&]() -> mlir::LogicalResult {
                                const CapturedStmt *cs =
                                    s.getCapturedStmt(llvm::omp::OMPD_parallel);
                                return emitStmt(cs->getCapturedStmt(),
                                                /*useCurrentScope=*/true);
                              });
      });
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelForDirective(
    const OMPTargetParallelForDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTaskLoopDirective(const OMPTaskLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPTaskLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTaskLoopSimdDirective(
    const OMPTaskLoopSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTaskLoopSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPMaskedTaskLoopDirective(
    const OMPMaskedTaskLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPMaskedTaskLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPMaskedTaskLoopSimdDirective(
    const OMPMaskedTaskLoopSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPMaskedTaskLoopSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPMasterTaskLoopDirective(
    const OMPMasterTaskLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPMasterTaskLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPMasterTaskLoopSimdDirective(
    const OMPMasterTaskLoopSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPMasterTaskLoopSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelGenericLoopDirective(
    const OMPParallelGenericLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelGenericLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedDirective(
    const OMPParallelMaskedDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMaskedDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedTaskLoopDirective(
    const OMPParallelMaskedTaskLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMaskedTaskLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMaskedTaskLoopSimdDirective(
    const OMPParallelMaskedTaskLoopSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMaskedTaskLoopSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterTaskLoopDirective(
    const OMPParallelMasterTaskLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMasterTaskLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPParallelMasterTaskLoopSimdDirective(
    const OMPParallelMasterTaskLoopSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPParallelMasterTaskLoopSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPDistributeDirective(const OMPDistributeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPDistributeDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPDistributeParallelForDirective(
    const OMPDistributeParallelForDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPDistributeParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPDistributeParallelForSimdDirective(
    const OMPDistributeParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPDistributeParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPDistributeSimdDirective(
    const OMPDistributeSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPDistributeSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelGenericLoopDirective(
    const OMPTargetParallelGenericLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetParallelGenericLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetParallelForSimdDirective(
    const OMPTargetParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetSimdDirective(const OMPTargetSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsGenericLoopDirective(
    const OMPTargetTeamsGenericLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetTeamsGenericLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetUpdateDirective(
    const OMPTargetUpdateDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetUpdateDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeDirective(
    const OMPTeamsDistributeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeSimdDirective(
    const OMPTeamsDistributeSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTeamsDistributeParallelForSimdDirective(
    const OMPTeamsDistributeParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(
      s.getSourceRange(), "OpenMP OMPTeamsDistributeParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsDistributeParallelForDirective(
    const OMPTeamsDistributeParallelForDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsDistributeParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTeamsGenericLoopDirective(
    const OMPTeamsGenericLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTeamsGenericLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDirective(const OMPTargetTeamsDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetTeamsDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsDistributeDirective(
    const OMPTargetTeamsDistributeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetTeamsDistributeDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDistributeParallelForDirective(
    const OMPTargetTeamsDistributeParallelForDirective &s) {
  getCIRGenModule().errorNYI(
      s.getSourceRange(),
      "OpenMP OMPTargetTeamsDistributeParallelForDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPTargetTeamsDistributeParallelForSimdDirective(
    const OMPTargetTeamsDistributeParallelForSimdDirective &s) {
  getCIRGenModule().errorNYI(
      s.getSourceRange(),
      "OpenMP OMPTargetTeamsDistributeParallelForSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult CIRGenFunction::emitOMPTargetTeamsDistributeSimdDirective(
    const OMPTargetTeamsDistributeSimdDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPTargetTeamsDistributeSimdDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPInteropDirective(const OMPInteropDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPInteropDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPDispatchDirective(const OMPDispatchDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPDispatchDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPGenericLoopDirective(const OMPGenericLoopDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPGenericLoopDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPReverseDirective(const OMPReverseDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPReverseDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPSplitDirective(const OMPSplitDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPSplitDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPInterchangeDirective(const OMPInterchangeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(),
                             "OpenMP OMPInterchangeDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPFlattenDirective(const OMPFlattenDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPFlattenDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPAssumeDirective(const OMPAssumeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPAssumeDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPMaskedDirective(const OMPMaskedDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPMaskedDirective");
  return mlir::failure();
}
mlir::LogicalResult
CIRGenFunction::emitOMPStripeDirective(const OMPStripeDirective &s) {
  getCIRGenModule().errorNYI(s.getSourceRange(), "OpenMP OMPStripeDirective");
  return mlir::failure();
}
