//===---------- SubobjectVisitor.h - Subobject Visitor ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the SubobjectVisitor interface, which recursively
//  traverses subobject within a type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_SUBOBJECTVISITOR_H
#define LLVM_CLANG_AST_SUBOBJECTVISITOR_H

#include "clang/AST/Type.h"

namespace clang {
template <typename Derived> class SubobjectVisitor {
  public:
  /// Return a reference to the derived class.
  Derived &getDerived() { return *static_cast<Derived *>(this); }
  //ASTContext &Ctx;

  // These enable handler execution only when previous Handlers succeed.
  // template <typename... Tn>
  // bool handleField(FieldDecl *FD, QualType FDTy, Tn &&...tn) {
  //   bool result = true;
  //   (void)std::initializer_list<int>{(result = result && tn(FD, FDTy), 0)...};
  //   return result;
  // }
  // template <typename... Tn>
  // bool handleField(const CXXBaseSpecifier &BD, QualType BDTy, Tn &&...tn) {
  //   bool result = true;
  //   std::initializer_list<int>{(result = result && tn(BD, BDTy), 0)...};
  //   return result;
  // }

// #define KF_FOR_EACH(FUNC, Item, Qt)                                            \
//   handleField(Item, Qt, ([&](FieldDecl *FD, QualType FDTy) {                   \
//                 return Handlers.FUNC(FD, FDTy);                                \
//               })...)

  // Parent contains the FieldDecl or CXXBaseSpecifier that was used to enter
  // the Wrapper structure that we're currently visiting. Owner is the parent
  // type (which doesn't exist in cases where it is a FieldDecl in the
  // 'root'), and Wrapper is the current struct being unwrapped.
  // template <typename ParentTy, typename... HandlerTys>
  // void visitComplexRecord(const CXXRecordDecl *Owner, ParentTy &Parent,
  //                         const CXXRecordDecl *Wrapper, QualType RecordTy,
  //                         HandlerTys &...Handlers) {
  //   (void)std::initializer_list<int>{
  //       (Handlers.enterStruct(Owner, Parent, RecordTy), 0)...};
  //   visitRecordHelper(Wrapper, Wrapper->bases(), Handlers...);
  //   visitRecordHelper(Wrapper, Wrapper->fields(), Handlers...);
  //   (void)std::initializer_list<int>{
  //       (Handlers.leaveStruct(Owner, Parent, RecordTy), 0)...};
  // }

  bool walkUpFromRecord(CXXRecordDecl *Record) {
    return getDerived().visitRecord(Record);
  }
  bool walkUpFromUnion(CXXRecordDecl *Record) {
    return getDerived().visitUnion(Record);
  }

  bool visitRecord(CXXRecordDecl *Record) {
    return true;
  }

  bool visitUnion(CXXRecordDecl *Record) {
    return true;
  }

  bool visitReferenceType(QualType Ty) {
    return true;
  }
  bool visitPointerType(QualType Ty) {
    return true;
  }
  bool visitScalarType(QualType Ty) {
    return true;
  }
  bool visitOtherType(QualType Ty) {
    return true;
  }

  bool traverseRecord(CXXRecordDecl *Record) {
    getDerived().walkUpFromRecord(Record);

    for (const auto &Base : Record->bases()) {
      QualType BaseTy = Base.getType();
      getDerived().traverseType(BaseTy);
    }
    for (const auto Field : Record->fields()) {
      QualType FieldTy = Field->getType();
      getDerived().traverseType(FieldTy);
    }
    return true;
  }

  bool traverseUnion(CXXRecordDecl *Record) {
    getDerived().walkUpFromUnion(Record);

    for (const auto Field : Record->fields()) {
      QualType FieldTy = Field->getType();
      getDerived().traverseType(FieldTy);
    }
    return true;
  }
  bool traverseArray(QualType Ty) {
    //FIXME
    return true;
  }


  // THAT used to be visit field
  bool traverseType(QualType Ty) {
    if (Ty->isStructureOrClassType()) {
      CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
      getDerived().traverseRecord(RD);
    } else if (Ty->isUnionType()) {
      CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
      getDerived().traverseUnion(RD);
    } else if (Ty->isReferenceType())
      getDerived().visitReferenceType(Ty);
    else if (Ty->isPointerType())
      getDerived().visitPointerType(Ty);
    else if (Ty->isArrayType())
      getDerived().traverseArray(Ty);
    else if (Ty->isScalarType() || Ty->isVectorType())
      getDerived().visitScalarType(Ty);
    else
      getDerived().visitOtherType(Ty);
    return true;
  }

  //SubobjectVisitor(ASTContext &C) : Ctx(C) {}

  // ARRAY HANDLING.
  // template <typename... HandlerTys>
  // void visitArray(const CXXRecordDecl *Owner, FieldDecl *Field,
  //                 QualType ArrayTy, HandlerTys &...Handlers) {
  //   // TODO add support for simple array visiting, i.e. without entering array
  //   // elements.
  //   visitComplexArray(Owner, Field, ArrayTy, Handlers...);
  // }

  // template <typename... HandlerTys>
  // void visitArrayElementImpl(const CXXRecordDecl *Owner, FieldDecl *ArrayField,
  //                            QualType ElementTy, uint64_t Index,
  //                            HandlerTys &...Handlers) {
  //   visitField(Owner, ArrayField, ElementTy, Handlers...);
  // }

  // template <typename... HandlerTys>
  // void visitNthArrayElement(const CXXRecordDecl *Owner, FieldDecl *ArrayField,
  //                           QualType ElementTy, uint64_t Index,
  //                           HandlerTys &...Handlers) {
  //   visitArrayElementImpl(Owner, ArrayField, ElementTy, Index, Handlers...);
  // }

  // template <typename... HandlerTys>
  // void visitComplexArray(const CXXRecordDecl *Owner, FieldDecl *Field,
  //                        QualType ArrayTy, HandlerTys &...Handlers) {
  //   // Array workflow is:
  //   // handleArrayType
  //   // enterArray
  //   // visitField (same as before, note that The FieldDecl is the of array
  //   // itself, not the element)
  //   // ... repeat per element, opt-out for duplicates.
  //   // leaveArray

  //   if (!KF_FOR_EACH(handleArrayType, Field, ArrayTy))
  //     return;

  //   const ConstantArrayType *CAT = Ctx.getAsConstantArrayType(ArrayTy);
  //   assert(CAT && "Should only be called on constant-size array.");
  //   QualType ET = CAT->getElementType();
  //   uint64_t ElemCount = CAT->getSize().getZExtValue();

  //   (void)std::initializer_list<int>{
  //       (Handlers.enterArray(Field, ArrayTy, ET), 0)...};

  //   for (uint64_t Index = 0; Index < ElemCount; ++Index)
  //     visitNthArrayElement(Owner, Field, ET, Index, Handlers...);

  //   (void)std::initializer_list<int>{
  //       (Handlers.leaveArray(Field, ArrayTy, ET), 0)...};
  // }

};
} // end namespace clang
#endif // LLVM_CLANG_AST_SUBOBJECTVISITOR
