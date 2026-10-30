//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// OpenMP clause emitter implementation.
//
//===----------------------------------------------------------------------===//

#include "CIRGenOpenMPClause.h"
#include "CIRGenFunction.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "clang/Basic/OpenMPKinds.h"

using namespace clang;
using namespace clang::CIRGen;

static mlir::omp::ClauseMapFlags
mapClauseKindToFlags(OpenMPMapClauseKind kind) {
  switch (kind) {
  case OMPC_MAP_to:
    return mlir::omp::ClauseMapFlags::to;
  case OMPC_MAP_from:
    return mlir::omp::ClauseMapFlags::from;
  case OMPC_MAP_tofrom:
    return mlir::omp::ClauseMapFlags::to | mlir::omp::ClauseMapFlags::from;
  case OMPC_MAP_alloc:
  case OMPC_MAP_release:
    return mlir::omp::ClauseMapFlags::storage;
  case OMPC_MAP_delete:
    return mlir::omp::ClauseMapFlags::del;
  default:
    return mlir::omp::ClauseMapFlags::none;
  }
}

/// An omp.map.info for the memory of type \p elemType at \p varPtr, of which
/// only what \p bounds describe is mapped, if there are any.
static mlir::Value emitMapInfo(mlir::OpBuilder &builder, mlir::Location loc,
                               mlir::Value varPtr, mlir::Type elemType,
                               llvm::StringRef name,
                               mlir::omp::ClauseMapFlags mapFlags,
                               mlir::ValueRange bounds = {}) {
  auto varPtrType = mlir::cast<cir::PointerType>(varPtr.getType());

  // Cast to generic pointer if needed.
  if (varPtrType.getAddrSpace()) {
    auto genericPtrType =
        cir::PointerType::get(builder.getContext(), elemType);
    varPtr = cir::CastOp::create(builder, loc, genericPtrType,
                                 cir::CastKind::address_space, varPtr);
    varPtrType = genericPtrType;
  }

  return mlir::omp::MapInfoOp::create(
      builder, loc,
      /*omp_ptr=*/varPtrType,
      /*var_ptr=*/varPtr,
      /*var_ptr_type=*/mlir::TypeAttr::get(elemType),
      /*map_type=*/builder.getAttr<mlir::omp::ClauseMapFlagsAttr>(mapFlags),
      /*map_capture_type=*/
      builder.getAttr<mlir::omp::VariableCaptureKindAttr>(
          mlir::omp::VariableCaptureKind::ByRef),
      /*var_ptr_ptr=*/mlir::Value{},
      /*var_ptr_ptr_type=*/mlir::TypeAttr{},
      /*members=*/mlir::ValueRange{},
      /*members_index=*/mlir::ArrayAttr{},
      /*bounds=*/bounds,
      /*mapper_id=*/mlir::FlatSymbolRefAttr{},
      /*name=*/builder.getStringAttr(name),
      /*partial_map=*/builder.getBoolAttr(false));
}

mlir::Value clang::CIRGen::emitMapInfoForVar(
    CIRGenFunction &cgf, mlir::OpBuilder &builder, mlir::Location loc,
    const VarDecl *vd, mlir::omp::ClauseMapFlags mapFlags) {
  mlir::Value varPtr = cgf.getAddrOfLocalVar(vd).getPointer();
  mlir::Type elemType =
      mlir::cast<cir::PointerType>(varPtr.getType()).getPointee();
  return emitMapInfo(builder, loc, varPtr, elemType, vd->getName(), mapFlags);
}

/// What a map clause list item is about: the variable, and the part of it that
/// is mapped if that isn't all of it.
struct MappedItem {
  const VarDecl *vd = nullptr;
  /// Of a section or an element: the first element and how many of them.
  const Expr *lower = nullptr, *length = nullptr;
  bool isSection = false;
};

static std::optional<MappedItem> classifyMapItem(const Expr *e) {
  e = e->IgnoreImplicit();
  MappedItem item;
  const Expr *base = nullptr;
  if (const auto *section = dyn_cast<ArraySectionExpr>(e)) {
    if (section->isOMPArraySection() && section->getStride())
      return std::nullopt;
    base = section->getBase();
    item.lower = section->getLowerBound();
    item.length = section->getLength();
  } else if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(e)) {
    base = subscript->getBase();
    item.lower = subscript->getIdx();
  } else {
    return std::nullopt;
  }
  item.isSection = true;
  const auto *ref = dyn_cast<DeclRefExpr>(base->IgnoreParenImpCasts());
  item.vd = ref ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
  if (!item.vd)
    return std::nullopt;
  return item;
}

bool OpenMPClauseEmitter::emitMap(
    mlir::omp::MapClauseOps &result,
    llvm::SmallVectorImpl<const VarDecl *> *mapSyms,
    llvm::SmallPtrSetImpl<const VarDecl *> *pointeeSyms) const {
  bool found = false;
  for (const OMPClause *clause : clauses) {
    const auto *mc = dyn_cast<OMPMapClause>(clause);
    if (!mc)
      continue;

    found = true;

    for (OpenMPMapModifierKind mod : mc->getMapTypeModifiers()) {
      if (mod != OMPC_MAP_MODIFIER_unknown)
        cgm.errorNYI(mc->getBeginLoc(),
                     std::string("OpenMP map modifier '") +
                         getOpenMPSimpleClauseTypeName(
                             llvm::omp::Clause::OMPC_map, mod) +
                         "'");
    }

    mlir::omp::ClauseMapFlags mapFlags = mapClauseKindToFlags(mc->getMapType());
    // Variables that the region uses, and that can't be passed by copy.
    if (mc->isImplicit())
      mapFlags |= mlir::omp::ClauseMapFlags::implicit;

    for (const Expr *varExpr : mc->varlist()) {
      const auto *refExpr = dyn_cast<DeclRefExpr>(varExpr->IgnoreImplicit());
      if (!refExpr) {
        // An element or a section, of a local array or of what a local pointer
        // points to.
        std::optional<MappedItem> item = classifyMapItem(varExpr);
        QualType ty = item ? item->vd->getType() : QualType();
        if (!item || item->vd->hasGlobalStorage() ||
            !(ty->isConstantArrayType() || ty->isPointerType()) ||
            (ty->isConstantArrayType() &&
             cgf.getContext().getAsConstantArrayType(ty)
                 ->getElementType()
                 ->isArrayType())) {
          cgm.errorNYI(varExpr->getExprLoc(),
                       "OpenMP map clause with this kind of variable; only "
                       "variables, and one-dimensional sections and elements "
                       "of local arrays and pointers are supported");
          continue;
        }
        const VarDecl *vd = item->vd;
        mlir::Location itemLoc = cgf.getLoc(varExpr->getSourceRange());

        cir::IntType s64 = builder.getSInt64Ty();
        auto asIndex = [&](const Expr *e) {
          return builder.createIntCast(cgf.emitScalarExpr(e), s64);
        };
        mlir::Value lower = item->lower
                                ? asIndex(item->lower)
                                : builder.getConstInt(itemLoc, s64, 0);
        mlir::Value length;
        if (isa<ArraySubscriptExpr>(varExpr->IgnoreImplicit())) {
          length = builder.getConstInt(itemLoc, s64, 1);
        } else if (item->length) {
          length = asIndex(item->length);
        } else if (ty->isConstantArrayType()) {
          // Up to the end of the array.
          uint64_t size =
              cgf.getContext().getAsConstantArrayType(ty)->getZExtSize();
          length = builder.createSub(itemLoc,
                                     builder.getConstInt(itemLoc, s64, size),
                                     lower);
        } else {
          cgm.errorNYI(varExpr->getExprLoc(),
                       "OpenMP map of a pointer section without a length");
          continue;
        }
        mlir::Value one = builder.getConstInt(itemLoc, s64, 1);
        mlir::Value upper = builder.createSub(
            itemLoc, builder.createAdd(itemLoc, lower, length), one);

        mlir::Type i64 = builder.getIntegerType(64);
        auto builtin = [&](mlir::Value v) {
          return builder.createBuiltinIntCast(itemLoc, v, i64);
        };
        mlir::Type boundsTy = mlir::omp::MapBoundsType::get(builder.getContext());
        mlir::Value bounds = mlir::omp::MapBoundsOp::create(
            builder, itemLoc, boundsTy, builtin(lower), builtin(upper),
            builtin(length), builtin(one), /*stride_in_bytes=*/false,
            builtin(builder.getConstInt(itemLoc, s64, 0)));

        mlir::Value varPtr = cgf.getAddrOfLocalVar(vd).getPointer();
        mlir::Type elemTy =
            mlir::cast<cir::PointerType>(varPtr.getType()).getPointee();
        if (ty->isPointerType()) {
          // The pointer's value is what is mapped, from the pointee.
          varPtr = cir::LoadOp::create(builder, itemLoc, varPtr);
          elemTy = mlir::cast<cir::PointerType>(elemTy).getPointee();
        }
        result.mapVars.push_back(emitMapInfo(builder, itemLoc, varPtr, elemTy,
                                             vd->getName(), mapFlags, bounds));
        if (mapSyms)
          mapSyms->push_back(vd);
        if (pointeeSyms && ty->isPointerType())
          pointeeSyms->insert(vd);
        continue;
      }

      const auto *vd = dyn_cast<VarDecl>(refExpr->getDecl());
      if (!vd) {
        cgm.errorNYI(varExpr->getExprLoc(),
                     "OpenMP map clause with non-VarDecl variable");
        continue;
      }

      result.mapVars.push_back(
          emitMapInfoForVar(cgf, builder, loc, vd, mapFlags));
      if (mapSyms)
        mapSyms->push_back(vd);
    }
  }
  return found;
}

bool OpenMPClauseEmitter::emitProcBind(
    mlir::omp::ProcBindClauseOps &result) const {
  for (const OMPClause *clause : clauses) {
    const auto *pbc = dyn_cast<OMPProcBindClause>(clause);
    if (!pbc)
      continue;

    llvm::omp::ProcBindKind kind = pbc->getProcBindKind();
    assert(kind != llvm::omp::ProcBindKind::OMP_PROC_BIND_unknown &&
           "unknown proc-bind kind");
    // The 'default' kind has no dialect counterpart; leave the attribute unset.
    if (kind != llvm::omp::ProcBindKind::OMP_PROC_BIND_default)
      result.procBindKind = mlir::omp::ClauseProcBindKindAttr::get(
          builder.getContext(), mlir::omp::convertProcBindKind(kind));
    return true;
  }
  return false;
}

mlir::LogicalResult OpenMPClauseEmitter::emitSchedule(
    mlir::omp::ScheduleClauseOps &result) const {
  for (const OMPClause *clause : clauses) {
    const auto *sc = dyn_cast<OMPScheduleClause>(clause);
    if (!sc)
      continue;

    mlir::MLIRContext *ctx = builder.getContext();
    mlir::omp::ClauseScheduleKind kind;
    switch (sc->getScheduleKind()) {
    case OMPC_SCHEDULE_static:
      kind = mlir::omp::ClauseScheduleKind::Static;
      break;
    case OMPC_SCHEDULE_dynamic:
      kind = mlir::omp::ClauseScheduleKind::Dynamic;
      break;
    case OMPC_SCHEDULE_guided:
      kind = mlir::omp::ClauseScheduleKind::Guided;
      break;
    case OMPC_SCHEDULE_auto:
      kind = mlir::omp::ClauseScheduleKind::Auto;
      break;
    case OMPC_SCHEDULE_runtime:
      kind = mlir::omp::ClauseScheduleKind::Runtime;
      break;
    case OMPC_SCHEDULE_unknown:
      llvm_unreachable("unknown schedule kind survived Sema");
    }
    result.scheduleKind = mlir::omp::ClauseScheduleKindAttr::get(ctx, kind);

    // There can be up to two modifiers: an ordering one and 'simd'.
    for (OpenMPScheduleClauseModifier mod :
         {sc->getFirstScheduleModifier(), sc->getSecondScheduleModifier()}) {
      switch (mod) {
      case OMPC_SCHEDULE_MODIFIER_monotonic:
        result.scheduleMod = mlir::omp::ScheduleModifierAttr::get(
            ctx, mlir::omp::ScheduleModifier::monotonic);
        break;
      case OMPC_SCHEDULE_MODIFIER_nonmonotonic:
        result.scheduleMod = mlir::omp::ScheduleModifierAttr::get(
            ctx, mlir::omp::ScheduleModifier::nonmonotonic);
        break;
      case OMPC_SCHEDULE_MODIFIER_simd:
        result.scheduleSimd = builder.getUnitAttr();
        break;
      case OMPC_SCHEDULE_MODIFIER_last:
      case OMPC_SCHEDULE_MODIFIER_unknown:
        break;
      }
    }

    // A chunk size that isn't a constant is captured by a helper variable.
    if (const Stmt *preInit = sc->getPreInitStmt()) {
      if (const auto *declStmt = dyn_cast<DeclStmt>(preInit))
        for (const Decl *d : declStmt->decls())
          cgf.emitVarDecl(*cast<VarDecl>(d));
      else if (mlir::failed(cgf.emitStmt(preInit, /*useCurrentScope=*/true)))
        return mlir::failure();
    }

    if (const Expr *chunk = sc->getChunkSize()) {
      mlir::Value chunkVal = cgf.emitScalarExpr(chunk);
      auto cirTy = mlir::cast<cir::IntType>(chunkVal.getType());
      result.scheduleChunk = builder.createBuiltinIntCast(
          chunkVal, builder.getIntegerType(cirTy.getWidth()));
    }
    return mlir::success();
  }
  return mlir::success();
}

bool OpenMPClauseEmitter::emitNumThreads(
    mlir::omp::NumThreadsClauseOps &result) const {
  for (const OMPClause *clause : clauses) {
    const auto *ntc = dyn_cast<OMPNumThreadsClause>(clause);
    if (!ntc)
      continue;

    for (const Expr *expr : ntc->getNumThreads()) {
      mlir::Value numThreadsValue = cgf.emitScalarExpr(expr);
      auto intType = builder.getIntegerType(32);
      numThreadsValue = builder.createBuiltinIntCast(numThreadsValue, intType);
      result.numThreadsVars.push_back(numThreadsValue);
    }

    return true;
  }
  return false;
}

bool OpenMPClauseEmitter::emitIf(mlir::omp::IfClauseOps &result,
                                 llvm::omp::Directive directiveName) const {
  for (const OMPClause *clause : clauses) {
    const auto *ic = dyn_cast<OMPIfClause>(clause);
    if (!ic)
      continue;

    if (!(ic->getNameModifier() == llvm::omp::Directive::OMPD_unknown) &&
        ic->getNameModifier() != directiveName)
      continue;

    Expr *ifCondition = ic->getCondition();
    mlir::Value ifBoolValue = cgf.evaluateExprAsBool(ifCondition); // !cir.bool

    mlir::Type uIntType = builder.getUIntNTy(1);
    mlir::Value ifUIntValue =
        builder.createBoolToInt(ifBoolValue, uIntType); // u1

    mlir::Type intType = builder.getI1Type();
    mlir::Value ifExpr =
        builder.createBuiltinIntCast(ifUIntValue, intType); // i1

    result.ifExpr = ifExpr;
    return true;
  }
  return false;
}

bool OpenMPClauseEmitter::emitIsDevicePtr(
    mlir::omp::TargetOperands &result,
    llvm::SmallVectorImpl<const VarDecl *> *syms) const {
  bool found = false;
  for (const OMPClause *clause : clauses) {
    const auto *dc = dyn_cast<OMPIsDevicePtrClause>(clause);
    if (!dc)
      continue;
    found = true;

    for (const Expr *varExpr : dc->varlist()) {
      const auto *refExpr = dyn_cast<DeclRefExpr>(varExpr->IgnoreImplicit());
      const auto *vd =
          refExpr ? dyn_cast<VarDecl>(refExpr->getDecl()) : nullptr;
      if (!vd || !vd->getType()->isPointerType() || vd->hasGlobalStorage()) {
        cgm.errorNYI(varExpr->getExprLoc(),
                     "OpenMP is_device_ptr of something other than a local "
                     "pointer variable");
        continue;
      }

      // The pointer is mapped, so that the region gets a variable holding it;
      // the flag keeps the runtime from translating its value. The clause and
      // the region argument each need an operation of their own.
      mlir::omp::ClauseMapFlags flags =
          mlir::omp::ClauseMapFlags::is_device_ptr |
          mlir::omp::ClauseMapFlags::to;
      result.hasDeviceAddrVars.push_back(
          emitMapInfoForVar(cgf, builder, loc, vd, flags));
      result.isDevicePtrVars.push_back(
          emitMapInfoForVar(cgf, builder, loc, vd, flags));
      if (syms)
        syms->push_back(vd);
    }
  }
  return found;
}

