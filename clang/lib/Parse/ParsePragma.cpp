//===--- ParsePragma.cpp - Language specific pragma parsing ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the language specific #pragma handlers.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/Basic/DiagnosticParse.h"
#include "clang/Basic/PragmaKinds.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/LoopHint.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaRISCV.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <optional>
using namespace clang;

namespace {

struct PragmaPackHandler : public PragmaHandler {
  explicit PragmaPackHandler() : PragmaHandler("pack") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};

struct PragmaAlignHandler : public PragmaHandler {
  explicit PragmaAlignHandler() : PragmaHandler("align") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};

struct PragmaOptionsHandler : public PragmaHandler {
  explicit PragmaOptionsHandler() : PragmaHandler("options") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};

struct PragmaMSVtorDisp : public PragmaHandler {
  explicit PragmaMSVtorDisp() : PragmaHandler("vtordisp") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};

struct PragmaClangSectionHandler : public PragmaHandler {
  explicit PragmaClangSectionHandler(Sema &S)
             : PragmaHandler("section"), Actions(S) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};

/// PragmaCommentHandler - "\#pragma comment ...".
struct PragmaCommentHandler : public PragmaHandler {
  PragmaCommentHandler(Sema &Actions)
    : PragmaHandler("comment"), Actions(Actions) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaDetectMismatchHandler : public PragmaHandler {
  PragmaDetectMismatchHandler(Sema &Actions)
    : PragmaHandler("detect_mismatch"), Actions(Actions) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};

struct PragmaFloatControlHandler : public PragmaHandler {
  PragmaFloatControlHandler(Sema &Actions)
      : PragmaHandler("float_control") {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;
};


/// PragmaOptimizeHandler - "\#pragma clang optimize on/off".
struct PragmaOptimizeHandler : public PragmaHandler {
  PragmaOptimizeHandler(Sema &S)
    : PragmaHandler("optimize"), Actions(S) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};


struct PragmaForceCUDAHostDeviceHandler : public PragmaHandler {
  PragmaForceCUDAHostDeviceHandler(Sema &Actions)
      : PragmaHandler("force_cuda_host_device"), Actions(Actions) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};

/// PragmaAttributeHandler - "\#pragma clang attribute ...".
struct PragmaAttributeHandler : public PragmaHandler {
  PragmaAttributeHandler(AttributeFactory &AttrFactory)
      : PragmaHandler("attribute"), AttributesForPragmaAttribute(AttrFactory) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

  /// A pool of attributes that were parsed in \#pragma clang attribute.
  ParsedAttributes AttributesForPragmaAttribute;
};


struct PragmaRISCVHandler : public PragmaHandler {
  PragmaRISCVHandler(Sema &Actions)
      : PragmaHandler("riscv"), Actions(Actions) {}
  void HandlePragma(Preprocessor &PP, PragmaIntroducer Introducer,
                    Token &FirstToken) override;

private:
  Sema &Actions;
};

void markAsReinjectedForRelexing(llvm::MutableArrayRef<clang::Token> Toks) {
  for (auto &T : Toks)
    T.setFlag(clang::Token::IsReinjected);
}
}  // end namespace

// #pragma pack(...) comes in the following delicious flavors:
//   pack '(' [integer] ')'
//   pack '(' 'show' ')'
//   pack '(' ('push' | 'pop') [',' identifier] [, integer] ')'
//   pack '(' 'packed' | 'full' | 'twobyte' | 'reset' ')' with -fzos-extensions
void PragmaPackHandler::HandlePragma(Preprocessor &PP,
                                     PragmaIntroducer Introducer,
                                     Token &PackTok) {
  SourceLocation PackLoc = PackTok.getLocation();

  Token Tok;
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen) << "pack";
    return;
  }

  Sema::PragmaMsStackAction Action = Sema::PSK_Reset;
  StringRef SlotLabel;
  Token Alignment;
  Alignment.startToken();
  PP.Lex(Tok);
  if (Tok.is(tok::numeric_constant)) {
    Alignment = Tok;

    PP.Lex(Tok);

    // In MSVC/gcc, #pragma pack(4) sets the alignment without affecting
    // the push/pop stack.
    // In Apple gcc/XL, #pragma pack(4) is equivalent to #pragma pack(push, 4)
    Action = (PP.getLangOpts().ApplePragmaPack || PP.getLangOpts().XLPragmaPack)
                 ? Sema::PSK_Push_Set
                 : Sema::PSK_Set;
  } else if (Tok.is(tok::identifier)) {
    // Map pragma pack options to pack (integer).
    auto MapPack = [&](const char *Literal) {
      Action = Sema::PSK_Push_Set;
      Alignment = Tok;
      Alignment.setKind(tok::numeric_constant);
      Alignment.setLiteralData(Literal);
      Alignment.setLength(1);
    };

    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II->isStr("show")) {
      Action = Sema::PSK_Show;
      PP.Lex(Tok);
    } else if (II->isStr("packed") && PP.getLangOpts().ZOSExt) {
      // #pragma pack(packed) is the same as #pragma pack(1)
      MapPack("1");
      PP.Lex(Tok);
    } else if (II->isStr("full") && PP.getLangOpts().ZOSExt) {
      // #pragma pack(full) is the same as #pragma pack(4)
      MapPack("4");
      PP.Lex(Tok);
    } else if (II->isStr("twobyte") && PP.getLangOpts().ZOSExt) {
      // #pragma pack(twobyte) is the same as #pragma pack(2)
      MapPack("2");
      PP.Lex(Tok);
    } else if (II->isStr("reset") && PP.getLangOpts().ZOSExt) {
      // #pragma pack(reset) is the same as #pragma pack(pop) on XL
      Action = Sema::PSK_Pop;
      PP.Lex(Tok);
    } else {
      if (II->isStr("push")) {
        Action = Sema::PSK_Push;
      } else if (II->isStr("pop")) {
        Action = Sema::PSK_Pop;
      } else {
        PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_action) << "pack";
        return;
      }
      PP.Lex(Tok);

      if (Tok.is(tok::comma)) {
        PP.Lex(Tok);

        if (Tok.is(tok::numeric_constant)) {
          Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
          Alignment = Tok;

          PP.Lex(Tok);
        } else if (Tok.is(tok::identifier)) {
          SlotLabel = Tok.getIdentifierInfo()->getName();
          PP.Lex(Tok);

          if (Tok.is(tok::comma)) {
            PP.Lex(Tok);

            if (Tok.isNot(tok::numeric_constant)) {
              PP.Diag(Tok.getLocation(), diag::warn_pragma_pack_malformed);
              return;
            }

            Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
            Alignment = Tok;

            PP.Lex(Tok);
          }
        } else {
          PP.Diag(Tok.getLocation(), diag::warn_pragma_pack_malformed);
          return;
        }
      }
    }
  } else if (PP.getLangOpts().ApplePragmaPack ||
             PP.getLangOpts().XLPragmaPack) {
    // In MSVC/gcc, #pragma pack() resets the alignment without affecting
    // the push/pop stack.
    // In Apple gcc and IBM XL, #pragma pack() is equivalent to #pragma
    // pack(pop).
    Action = Sema::PSK_Pop;
  }

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen) << "pack";
    return;
  }

  SourceLocation RParenLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol) << "pack";
    return;
  }

  Sema::PragmaPackInfo *Info =
      PP.getPreprocessorAllocator().Allocate<Sema::PragmaPackInfo>(1);
  Info->Action = Action;
  Info->SlotLabel = SlotLabel;
  Info->Alignment = Alignment;

  MutableArrayRef<Token> Toks(PP.getPreprocessorAllocator().Allocate<Token>(1),
                              1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_pack);
  Toks[0].setLocation(PackLoc);
  Toks[0].setAnnotationEndLoc(RParenLoc);
  Toks[0].setAnnotationValue(static_cast<void*>(Info));
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/false);
}

// #pragma 'align' '=' {'native','natural','mac68k','power','reset'}
// #pragma 'options 'align' '=' {'native','natural','mac68k','power','reset'}
// #pragma 'align' '(' {'native','natural','mac68k','power','reset'} ')'
static void ParseAlignPragma(Preprocessor &PP, Token &FirstTok,
                             bool IsOptions) {
  Token Tok;

  if (IsOptions) {
    PP.Lex(Tok);
    if (Tok.isNot(tok::identifier) ||
        !Tok.getIdentifierInfo()->isStr("align")) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_options_expected_align);
      return;
    }
  }

  PP.Lex(Tok);
  if (PP.getLangOpts().XLPragmaPack) {
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_lparen) << "align";
      return;
    }
  } else if (Tok.isNot(tok::equal)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_align_expected_equal)
      << IsOptions;
    return;
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
      << (IsOptions ? "options" : "align");
    return;
  }

  Sema::PragmaOptionsAlignKind Kind = Sema::POAK_Natural;
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II->isStr("native"))
    Kind = Sema::POAK_Native;
  else if (II->isStr("natural"))
    Kind = Sema::POAK_Natural;
  else if (II->isStr("packed"))
    Kind = Sema::POAK_Packed;
  else if (II->isStr("power"))
    Kind = Sema::POAK_Power;
  else if (II->isStr("mac68k"))
    Kind = Sema::POAK_Mac68k;
  else if (II->isStr("reset"))
    Kind = Sema::POAK_Reset;
  else {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_align_invalid_option)
      << IsOptions;
    return;
  }

  if (PP.getLangOpts().XLPragmaPack) {
    PP.Lex(Tok);
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_rparen) << "align";
      return;
    }
  }

  SourceLocation EndLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
      << (IsOptions ? "options" : "align");
    return;
  }

  MutableArrayRef<Token> Toks(PP.getPreprocessorAllocator().Allocate<Token>(1),
                              1);
  Toks[0].startToken();
  Toks[0].setKind(tok::annot_pragma_align);
  Toks[0].setLocation(FirstTok.getLocation());
  Toks[0].setAnnotationEndLoc(EndLoc);
  Toks[0].setAnnotationValue(reinterpret_cast<void*>(
                             static_cast<uintptr_t>(Kind)));
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject=*/false);
}

void PragmaAlignHandler::HandlePragma(Preprocessor &PP,
                                      PragmaIntroducer Introducer,
                                      Token &AlignTok) {
  ParseAlignPragma(PP, AlignTok, /*IsOptions=*/false);
}

void PragmaOptionsHandler::HandlePragma(Preprocessor &PP,
                                        PragmaIntroducer Introducer,
                                        Token &OptionsTok) {
  ParseAlignPragma(PP, OptionsTok, /*IsOptions=*/true);
}

/// Handle '#pragma vtordisp'
// The grammar for this pragma is as follows:
//
// <vtordisp-mode> ::= ('off' | 'on' | '0' | '1' | '2' )
//
// #pragma vtordisp '(' ['push' ','] vtordisp-mode ')'
// #pragma vtordisp '(' 'pop' ')'
// #pragma vtordisp '(' ')'
void PragmaMSVtorDisp::HandlePragma(Preprocessor &PP,
                                    PragmaIntroducer Introducer, Token &Tok) {
  SourceLocation VtorDispLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(VtorDispLoc, diag::warn_pragma_expected_lparen) << "vtordisp";
    return;
  }
  PP.Lex(Tok);

  Sema::PragmaMsStackAction Action = Sema::PSK_Set;
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (II) {
    if (II->isStr("push")) {
      // #pragma vtordisp(push, mode)
      PP.Lex(Tok);
      if (Tok.isNot(tok::comma)) {
        PP.Diag(VtorDispLoc, diag::warn_pragma_expected_punc) << "vtordisp";
        return;
      }
      PP.Lex(Tok);
      Action = Sema::PSK_Push_Set;
      // not push, could be on/off
    } else if (II->isStr("pop")) {
      // #pragma vtordisp(pop)
      PP.Lex(Tok);
      Action = Sema::PSK_Pop;
    }
    // not push or pop, could be on/off
  } else {
    if (Tok.is(tok::r_paren)) {
      // #pragma vtordisp()
      Action = Sema::PSK_Reset;
    }
  }


  uint64_t Value = 0;
  if (Action & Sema::PSK_Push || Action & Sema::PSK_Set) {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II && II->isStr("off")) {
      PP.Lex(Tok);
      Value = 0;
    } else if (II && II->isStr("on")) {
      PP.Lex(Tok);
      Value = 1;
    } else if (Tok.is(tok::numeric_constant) &&
               PP.parseSimpleIntegerLiteral(Tok, Value)) {
      if (Value > 2) {
        PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_integer)
            << 0 << 2 << "vtordisp";
        return;
      }
    } else {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_action)
          << "vtordisp";
      return;
    }
  }

  // Finish the pragma: ')' $
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(VtorDispLoc, diag::warn_pragma_expected_rparen) << "vtordisp";
    return;
  }
  SourceLocation EndLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "vtordisp";
    return;
  }

  // Enter the annotation.
  Token AnnotTok;
  AnnotTok.startToken();
  AnnotTok.setKind(tok::annot_pragma_ms_vtordisp);
  AnnotTok.setLocation(VtorDispLoc);
  AnnotTok.setAnnotationEndLoc(EndLoc);
  AnnotTok.setAnnotationValue(reinterpret_cast<void *>(
      static_cast<uintptr_t>((Action << 16) | (Value & 0xFFFF))));
  PP.EnterToken(AnnotTok, /*IsReinject=*/false);
}



void Parser::initializePragmaHandlers() {
  AlignHandler = new PragmaAlignHandler();
  PP.AddPragmaHandler(AlignHandler);
  OptionsHandler = new PragmaOptionsHandler();
  PP.AddPragmaHandler(OptionsHandler);
  PackHandler = new PragmaPackHandler();
  PP.AddPragmaHandler(PackHandler);

  PCSectionHandler = new PragmaClangSectionHandler(Actions);
  PP.AddPragmaHandler("clang", PCSectionHandler);
  if (getLangOpts().MicrosoftExt ||
      getTargetInfo().getTriple().isOSBinFormatELF()) {
    MSCommentHandler = new PragmaCommentHandler(Actions);
    PP.AddPragmaHandler(MSCommentHandler);
  }

  FloatControlHandler = new PragmaFloatControlHandler(Actions);
  PP.AddPragmaHandler(FloatControlHandler);
  if (getLangOpts().MicrosoftExt) {
    MSDetectMismatchHandler = new PragmaDetectMismatchHandler(Actions);
    PP.AddPragmaHandler(MSDetectMismatchHandler);
    MSVtorDisp = new PragmaMSVtorDisp();
    PP.AddPragmaHandler(MSVtorDisp);
  }

  if (getLangOpts().CUDA) {
    CUDAForceHostDeviceHandler = new PragmaForceCUDAHostDeviceHandler(Actions);
    PP.AddPragmaHandler("clang", CUDAForceHostDeviceHandler);
  }

  OptimizeHandler = new PragmaOptimizeHandler(Actions);
  PP.AddPragmaHandler("clang", OptimizeHandler);

  AttributePragmaHandler =
      new PragmaAttributeHandler(AttrFactory);
  PP.AddPragmaHandler("clang", AttributePragmaHandler);

  if (getTargetInfo().getTriple().isRISCV()) {
    RISCVPragmaHandler = new PragmaRISCVHandler(Actions);
    PP.AddPragmaHandler("clang", RISCVPragmaHandler);
  }
}

void Parser::resetPragmaHandlers() {
  // Remove the pragma handlers we installed.
  PP.RemovePragmaHandler(AlignHandler);
  delete AlignHandler;
  // PP.RemovePragmaHandler("GCC", GCCVisibilityHandler.get());
  // GCCVisibilityHandler.reset();
  PP.RemovePragmaHandler(OptionsHandler);
  delete OptionsHandler;
  PP.RemovePragmaHandler(PackHandler);
  delete PackHandler;
  // PP.RemovePragmaHandler(MSStructHandler.get());
  // MSStructHandler.reset();
  // PP.RemovePragmaHandler(UnusedHandler.get());
  // UnusedHandler.reset();
  // PP.RemovePragmaHandler(WeakHandler.get());
  // WeakHandler.reset();
  // PP.RemovePragmaHandler(RedefineExtnameHandler.get());
  // RedefineExtnameHandler.reset();

  // if (getLangOpts().OpenCL) {
  //   PP.RemovePragmaHandler("OPENCL", OpenCLExtensionHandler.get());
  //   OpenCLExtensionHandler.reset();
  //   PP.RemovePragmaHandler("OPENCL", FPContractHandler.get());
  // }
  // PP.RemovePragmaHandler(OpenMPHandler.get());
  // OpenMPHandler.reset();

  // PP.RemovePragmaHandler(OpenACCHandler.get());
  // OpenACCHandler.reset();

  if (getLangOpts().MicrosoftExt ||
      getTargetInfo().getTriple().isOSBinFormatELF()) {
    PP.RemovePragmaHandler(MSCommentHandler);
    delete MSCommentHandler;
  }

  PP.RemovePragmaHandler("clang", PCSectionHandler);
  delete PCSectionHandler;

  PP.RemovePragmaHandler(FloatControlHandler);
  delete FloatControlHandler;
  if (getLangOpts().MicrosoftExt) {
    PP.RemovePragmaHandler(MSDetectMismatchHandler);
    delete MSDetectMismatchHandler;
  //   PP.RemovePragmaHandler(MSPointersToMembers.get());
  //   MSPointersToMembers.reset();
     PP.RemovePragmaHandler(MSVtorDisp);
     delete MSVtorDisp;
  //   PP.RemovePragmaHandler(MSInitSeg.get());
  //   MSInitSeg.reset();
  //   PP.RemovePragmaHandler(MSDataSeg.get());
  //   MSDataSeg.reset();
  //   PP.RemovePragmaHandler(MSBSSSeg.get());
  //   MSBSSSeg.reset();
  //   PP.RemovePragmaHandler(MSConstSeg.get());
  //   MSConstSeg.reset();
  //   PP.RemovePragmaHandler(MSCodeSeg.get());
  //   MSCodeSeg.reset();
  //   PP.RemovePragmaHandler(MSSection.get());
  //   MSSection.reset();
  //   PP.RemovePragmaHandler(MSStrictGuardStackCheck.get());
  //   MSStrictGuardStackCheck.reset();
  //   PP.RemovePragmaHandler(MSFunction.get());
  //   MSFunction.reset();
  //   PP.RemovePragmaHandler(MSAllocText.get());
  //   MSAllocText.reset();
  //   PP.RemovePragmaHandler(MSRuntimeChecks.get());
  //   MSRuntimeChecks.reset();
  //   PP.RemovePragmaHandler(MSIntrinsic.get());
  //   MSIntrinsic.reset();
  //   PP.RemovePragmaHandler(MSOptimize.get());
  //   MSOptimize.reset();
  //   PP.RemovePragmaHandler(MSFenvAccess.get());
  //   MSFenvAccess.reset();
  }

  if (getLangOpts().CUDA) {
    PP.RemovePragmaHandler("clang", CUDAForceHostDeviceHandler);
    delete CUDAForceHostDeviceHandler;
  }

  // PP.RemovePragmaHandler("STDC", FPContractHandler.get());
  // FPContractHandler.reset();

  // PP.RemovePragmaHandler("STDC", STDCFenvAccessHandler.get());
  // STDCFenvAccessHandler.reset();

  // PP.RemovePragmaHandler("STDC", STDCFenvRoundHandler.get());
  // STDCFenvRoundHandler.reset();

  // PP.RemovePragmaHandler("STDC", STDCCXLIMITHandler.get());
  // STDCCXLIMITHandler.reset();

  // PP.RemovePragmaHandler("STDC", STDCUnknownHandler.get());
  // STDCUnknownHandler.reset();

  PP.RemovePragmaHandler("clang", OptimizeHandler);
  delete OptimizeHandler;

  // PP.RemovePragmaHandler("clang", LoopHintHandler.get());
  // LoopHintHandler.reset();

  // PP.RemovePragmaHandler(UnrollHintHandler.get());
  // PP.RemovePragmaHandler("GCC", UnrollHintHandler.get());
  // UnrollHintHandler.reset();

  // PP.RemovePragmaHandler(NoUnrollHintHandler.get());
  // PP.RemovePragmaHandler("GCC", NoUnrollHintHandler.get());
  // NoUnrollHintHandler.reset();

  // PP.RemovePragmaHandler(UnrollAndJamHintHandler.get());
  // UnrollAndJamHintHandler.reset();

  // PP.RemovePragmaHandler(NoUnrollAndJamHintHandler.get());
  // NoUnrollAndJamHintHandler.reset();

  // PP.RemovePragmaHandler("clang", FPHandler.get());
  // FPHandler.reset();

  PP.RemovePragmaHandler("clang", AttributePragmaHandler);
  delete AttributePragmaHandler;

  // PP.RemovePragmaHandler("clang", MaxTokensHerePragmaHandler.get());
  // MaxTokensHerePragmaHandler.reset();

  // PP.RemovePragmaHandler("clang", MaxTokensTotalPragmaHandler.get());
  // MaxTokensTotalPragmaHandler.reset();

  if (getTargetInfo().getTriple().isRISCV()) {
    PP.RemovePragmaHandler("clang", RISCVPragmaHandler);
    delete RISCVPragmaHandler;
  }
}

/// Handle the annotation token produced for #pragma unused(...)
///
/// Each annot_pragma_unused is followed by the argument token so e.g.
/// "#pragma unused(x,y)" becomes:
/// annot_pragma_unused 'x' annot_pragma_unused 'y'
void Parser::HandlePragmaUnused() {
  assert(Tok.is(tok::annot_pragma_unused));
  SourceLocation UnusedLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaUnused(Tok, getCurScope(), UnusedLoc);
  ConsumeToken(); // The argument token.
}

void Parser::HandlePragmaVisibility() {
  assert(Tok.is(tok::annot_pragma_vis));
  const IdentifierInfo *VisType =
    static_cast<IdentifierInfo *>(Tok.getAnnotationValue());
  SourceLocation VisLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaVisibility(VisType, VisLoc);
}

void Parser::HandlePragmaPack() {
  assert(Tok.is(tok::annot_pragma_pack));
  Sema::PragmaPackInfo *Info =
      static_cast<Sema::PragmaPackInfo *>(Tok.getAnnotationValue());
  SourceLocation PragmaLoc = Tok.getLocation();
  ExprResult Alignment;
  if (Info->Alignment.is(tok::numeric_constant)) {
    Alignment = Actions.ActOnNumericConstant(Info->Alignment);
    if (Alignment.isInvalid()) {
      ConsumeAnnotationToken();
      return;
    }
  }
  Actions.ActOnPragmaPack(PragmaLoc, Info->Action, Info->SlotLabel,
                          Alignment.get());
  // Consume the token after processing the pragma to enable pragma-specific
  // #include warnings.
  ConsumeAnnotationToken();
}

void Parser::HandlePragmaMSStruct() {
  assert(Tok.is(tok::annot_pragma_msstruct));
  PragmaMSStructKind Kind = static_cast<PragmaMSStructKind>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));
  Actions.ActOnPragmaMSStruct(Kind);
  ConsumeAnnotationToken();
}

void Parser::HandlePragmaAlign() {
  assert(Tok.is(tok::annot_pragma_align));
  Sema::PragmaOptionsAlignKind Kind =
    static_cast<Sema::PragmaOptionsAlignKind>(
    reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));
  Actions.ActOnPragmaOptionsAlign(Kind, Tok.getLocation());
  // Consume the token after processing the pragma to enable pragma-specific
  // #include warnings.
  ConsumeAnnotationToken();
}

void Parser::HandlePragmaDump() {
  assert(Tok.is(tok::annot_pragma_dump));
  ConsumeAnnotationToken();
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok, diag::warn_pragma_debug_missing_argument) << "dump";
  } else if (NextToken().is(tok::eod)) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok, diag::warn_pragma_debug_unexpected_argument);
      ConsumeAnyToken();
      ExpectAndConsume(tok::eod);
      return;
    }
    IdentifierInfo *II = Tok.getIdentifierInfo();
    Actions.ActOnPragmaDump(getCurScope(), Tok.getLocation(), II);
    ConsumeToken();
  } else {
    SourceLocation StartLoc = Tok.getLocation();
    EnterExpressionEvaluationContext Ctx(
      Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    ExprResult E = ParseExpression();
    if (!E.isUsable() || E.get()->containsErrors()) {
      // Diagnostics were emitted during parsing. No action needed.
    } else if (E.get()->getDependence() != ExprDependence::None) {
      PP.Diag(StartLoc, diag::warn_pragma_debug_dependent_argument)
        << E.get()->isTypeDependent()
        << SourceRange(StartLoc, Tok.getLocation());
    } else {
      Actions.ActOnPragmaDump(E.get());
    }
    SkipUntil(tok::eod, StopBeforeMatch);
  }
  ExpectAndConsume(tok::eod);
}

void Parser::HandlePragmaWeak() {
  assert(Tok.is(tok::annot_pragma_weak));
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaWeakID(Tok.getIdentifierInfo(), PragmaLoc,
                            Tok.getLocation());
  ConsumeToken(); // The weak name.
}

void Parser::HandlePragmaWeakAlias() {
  assert(Tok.is(tok::annot_pragma_weakalias));
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  IdentifierInfo *WeakName = Tok.getIdentifierInfo();
  SourceLocation WeakNameLoc = Tok.getLocation();
  ConsumeToken();
  IdentifierInfo *AliasName = Tok.getIdentifierInfo();
  SourceLocation AliasNameLoc = Tok.getLocation();
  ConsumeToken();
  Actions.ActOnPragmaWeakAlias(WeakName, AliasName, PragmaLoc,
                               WeakNameLoc, AliasNameLoc);

}

void Parser::HandlePragmaRedefineExtname() {
  assert(Tok.is(tok::annot_pragma_redefine_extname));
  SourceLocation RedefLoc = ConsumeAnnotationToken();
  IdentifierInfo *RedefName = Tok.getIdentifierInfo();
  SourceLocation RedefNameLoc = Tok.getLocation();
  ConsumeToken();
  IdentifierInfo *AliasName = Tok.getIdentifierInfo();
  SourceLocation AliasNameLoc = Tok.getLocation();
  ConsumeToken();
  Actions.ActOnPragmaRedefineExtname(RedefName, AliasName, RedefLoc,
                                     RedefNameLoc, AliasNameLoc);
}

void Parser::HandlePragmaFPContract() {
  assert(Tok.is(tok::annot_pragma_fp_contract));
  tok::OnOffSwitch OOS =
    static_cast<tok::OnOffSwitch>(
    reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  LangOptions::FPModeKind FPC;
  switch (OOS) {
  case tok::OOS_ON:
    FPC = LangOptions::FPM_On;
    break;
  case tok::OOS_OFF:
    FPC = LangOptions::FPM_Off;
    break;
  case tok::OOS_DEFAULT:
    // According to ISO C99 standard chapter 7.3.4, the default value
    // for the pragma is ``off'. '-fcomplex-arithmetic=basic',
    // '-fcx-limited-range', '-fcx-fortran-rules' and
    // '-fcomplex-arithmetic=improved' control the default value of these
    // pragmas.
    FPC = getLangOpts().getDefaultFPContractMode();
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaFPContract(PragmaLoc, FPC);
}

void Parser::HandlePragmaFloatControl() {
  assert(Tok.is(tok::annot_pragma_float_control));

  // The value that is held on the PragmaFloatControlStack encodes
  // the PragmaFloatControl kind and the MSStackAction kind
  // into a single 32-bit word. The MsStackAction is the high 16 bits
  // and the FloatControl is the lower 16 bits. Use shift and bit-and
  // to decode the parts.
  uintptr_t Value = reinterpret_cast<uintptr_t>(Tok.getAnnotationValue());
  Sema::PragmaMsStackAction Action =
      static_cast<Sema::PragmaMsStackAction>((Value >> 16) & 0xFFFF);
  PragmaFloatControlKind Kind = PragmaFloatControlKind(Value & 0xFFFF);
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaFloatControl(PragmaLoc, Action, Kind);
}

void Parser::HandlePragmaFEnvAccess() {
  assert(Tok.is(tok::annot_pragma_fenv_access) ||
         Tok.is(tok::annot_pragma_fenv_access_ms));
  tok::OnOffSwitch OOS =
    static_cast<tok::OnOffSwitch>(
    reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  bool IsEnabled;
  switch (OOS) {
  case tok::OOS_ON:
    IsEnabled = true;
    break;
  case tok::OOS_OFF:
    IsEnabled = false;
    break;
  case tok::OOS_DEFAULT: // FIXME: Add this cli option when it makes sense.
    IsEnabled = false;
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaFEnvAccess(PragmaLoc, IsEnabled);
}

void Parser::HandlePragmaFEnvRound() {
  assert(Tok.is(tok::annot_pragma_fenv_round));
  auto RM = static_cast<llvm::RoundingMode>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaFEnvRound(PragmaLoc, RM);
}

void Parser::HandlePragmaCXLimitedRange() {
  assert(Tok.is(tok::annot_pragma_cx_limited_range));
  tok::OnOffSwitch OOS = static_cast<tok::OnOffSwitch>(
      reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));

  LangOptions::ComplexRangeKind Range;
  switch (OOS) {
  case tok::OOS_ON:
    Range = LangOptions::CX_Basic;
    break;
  case tok::OOS_OFF:
    Range = LangOptions::CX_Full;
    break;
  case tok::OOS_DEFAULT:
    // According to ISO C99 standard chapter 7.3.4, the default value
    // for the pragma is ``off'. -fcomplex-arithmetic controls the default value
    // of these pragmas.
    Range = getLangOpts().getComplexRange();
    break;
  }

  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaCXLimitedRange(PragmaLoc, Range);
}

StmtResult Parser::HandlePragmaCaptured()
{
  assert(Tok.is(tok::annot_pragma_captured));
  ConsumeAnnotationToken();

  if (Tok.isNot(tok::l_brace)) {
    PP.Diag(Tok, diag::err_expected) << tok::l_brace;
    return StmtError();
  }

  SourceLocation Loc = Tok.getLocation();

  ParseScope CapturedRegionScope(this, Scope::FnScope | Scope::DeclScope |
                                           Scope::CompoundStmtScope);
  Actions.ActOnCapturedRegionStart(Loc, getCurScope(), CR_Default,
                                   /*NumParams=*/1);

  StmtResult R = ParseCompoundStatement();
  CapturedRegionScope.Exit();

  if (R.isInvalid()) {
    Actions.ActOnCapturedRegionError();
    return StmtError();
  }

  return Actions.ActOnCapturedRegionEnd(R.get());
}

void Parser::HandlePragmaOpenCLExtension() {
  assert(Tok.is(tok::annot_pragma_opencl_extension));
  OpenCLExtData *Data = static_cast<OpenCLExtData*>(Tok.getAnnotationValue());
  auto State = Data->second;
  auto Ident = Data->first;
  SourceLocation NameLoc = Tok.getLocation();
  ConsumeAnnotationToken();

  auto &Opt = Actions.getOpenCLOptions();
  auto Name = Ident->getName();
  // OpenCL 1.1 9.1: "The all variant sets the behavior for all extensions,
  // overriding all previously issued extension directives, but only if the
  // behavior is set to disable."
  if (Name == "all") {
    if (State == Disable)
      Opt.disableAll();
    else
      PP.Diag(NameLoc, diag::warn_pragma_expected_predicate) << 1;
  } else if (State == Begin) {
    if (!Opt.isKnown(Name) || !Opt.isSupported(Name, getLangOpts())) {
      Opt.support(Name);
      // FIXME: Default behavior of the extension pragma is not defined.
      // Therefore, it should never be added by default.
      Opt.acceptsPragma(Name);
    }
  } else if (State == End) {
    // There is no behavior for this directive. We only accept this for
    // backward compatibility.
  } else if (!Opt.isKnown(Name) || !Opt.isWithPragma(Name))
    PP.Diag(NameLoc, diag::warn_pragma_unknown_extension) << Ident;
  else if (Opt.isSupportedExtension(Name, getLangOpts()))
    Opt.enable(Name, State == Enable);
  else if (Opt.isSupportedCoreOrOptionalCore(Name, getLangOpts()))
    PP.Diag(NameLoc, diag::warn_pragma_extension_is_core) << Ident;
  else
    PP.Diag(NameLoc, diag::warn_pragma_unsupported_extension) << Ident;
}

void Parser::HandlePragmaMSPointersToMembers() {
  assert(Tok.is(tok::annot_pragma_ms_pointers_to_members));
  LangOptions::PragmaMSPointersToMembersKind RepresentationMethod =
      static_cast<LangOptions::PragmaMSPointersToMembersKind>(
          reinterpret_cast<uintptr_t>(Tok.getAnnotationValue()));
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaMSPointersToMembers(RepresentationMethod, PragmaLoc);
}

void Parser::HandlePragmaMSVtorDisp() {
  assert(Tok.is(tok::annot_pragma_ms_vtordisp));
  uintptr_t Value = reinterpret_cast<uintptr_t>(Tok.getAnnotationValue());
  Sema::PragmaMsStackAction Action =
      static_cast<Sema::PragmaMsStackAction>((Value >> 16) & 0xFFFF);
  MSVtorDispMode Mode = MSVtorDispMode(Value & 0xFFFF);
  SourceLocation PragmaLoc = ConsumeAnnotationToken();
  Actions.ActOnPragmaMSVtorDisp(Action, PragmaLoc, Mode);
}

void Parser::HandlePragmaMSPragma() {
  assert(Tok.is(tok::annot_pragma_ms_pragma));
  // Grab the tokens out of the annotation and enter them into the stream.
  auto TheTokens =
      (std::pair<std::unique_ptr<Token[]>, size_t> *)Tok.getAnnotationValue();
  PP.EnterTokenStream(std::move(TheTokens->first), TheTokens->second, true,
                      /*IsReinject=*/true);
  SourceLocation PragmaLocation = ConsumeAnnotationToken();
  assert(Tok.isAnyIdentifier());
  StringRef PragmaName = Tok.getIdentifierInfo()->getName();
  PP.Lex(Tok); // pragma kind

  // Figure out which #pragma we're dealing with.  The switch has no default
  // because lex shouldn't emit the annotation token for unrecognized pragmas.
  typedef bool (Parser::*PragmaHandler)(StringRef, SourceLocation);
  PragmaHandler Handler =
      llvm::StringSwitch<PragmaHandler>(PragmaName)
          .Case("data_seg", &Parser::HandlePragmaMSSegment)
          .Case("bss_seg", &Parser::HandlePragmaMSSegment)
          .Case("const_seg", &Parser::HandlePragmaMSSegment)
          .Case("code_seg", &Parser::HandlePragmaMSSegment)
          .Case("section", &Parser::HandlePragmaMSSection)
          .Case("init_seg", &Parser::HandlePragmaMSInitSeg)
          .Case("strict_gs_check", &Parser::HandlePragmaMSStrictGuardStackCheck)
          .Case("function", &Parser::HandlePragmaMSFunction)
          .Case("alloc_text", &Parser::HandlePragmaMSAllocText)
          .Case("optimize", &Parser::HandlePragmaMSOptimize);

  if (!(this->*Handler)(PragmaName, PragmaLocation)) {
    // Pragma handling failed, and has been diagnosed.  Slurp up the tokens
    // until eof (really end of line) to prevent follow-on errors.
    while (Tok.isNot(tok::eof))
      PP.Lex(Tok);
    PP.Lex(Tok);
  }
}

bool Parser::HandlePragmaMSSection(StringRef PragmaName,
                                   SourceLocation PragmaLocation) {
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_lparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // (
  // Parsing code for pragma section
  if (Tok.isNot(tok::string_literal)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_section_name)
        << PragmaName;
    return false;
  }
  ExprResult StringResult = ParseStringLiteralExpression();
  if (StringResult.isInvalid())
    return false; // Already diagnosed.
  StringLiteral *SegmentName = cast<StringLiteral>(StringResult.get());
  if (SegmentName->getCharByteWidth() != 1) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
        << PragmaName;
    return false;
  }
  int SectionFlags = ASTContext::PSF_Read;
  bool SectionFlagsAreDefault = true;
  while (Tok.is(tok::comma)) {
    PP.Lex(Tok); // ,
    // Ignore "long" and "short".
    // They are undocumented, but widely used, section attributes which appear
    // to do nothing.
    if (Tok.is(tok::kw_long) || Tok.is(tok::kw_short)) {
      PP.Lex(Tok); // long/short
      continue;
    }

    if (!Tok.isAnyIdentifier()) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_action_or_r_paren)
          << PragmaName;
      return false;
    }
    ASTContext::PragmaSectionFlag Flag =
      llvm::StringSwitch<ASTContext::PragmaSectionFlag>(
      Tok.getIdentifierInfo()->getName())
      .Case("read", ASTContext::PSF_Read)
      .Case("write", ASTContext::PSF_Write)
      .Case("execute", ASTContext::PSF_Execute)
      .Case("shared", ASTContext::PSF_Invalid)
      .Case("nopage", ASTContext::PSF_Invalid)
      .Case("nocache", ASTContext::PSF_Invalid)
      .Case("discard", ASTContext::PSF_Invalid)
      .Case("remove", ASTContext::PSF_Invalid)
      .Default(ASTContext::PSF_None);
    if (Flag == ASTContext::PSF_None || Flag == ASTContext::PSF_Invalid) {
      PP.Diag(PragmaLocation, Flag == ASTContext::PSF_None
                                  ? diag::warn_pragma_invalid_specific_action
                                  : diag::warn_pragma_unsupported_action)
          << PragmaName << Tok.getIdentifierInfo()->getName();
      return false;
    }
    SectionFlags |= Flag;
    SectionFlagsAreDefault = false;
    PP.Lex(Tok); // Identifier
  }
  // If no section attributes are specified, the section will be marked as
  // read/write.
  if (SectionFlagsAreDefault)
    SectionFlags |= ASTContext::PSF_Write;
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_rparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // )
  if (Tok.isNot(tok::eof)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_extra_tokens_at_eol)
        << PragmaName;
    return false;
  }
  PP.Lex(Tok); // eof
  Actions.ActOnPragmaMSSection(PragmaLocation, SectionFlags, SegmentName);
  return true;
}

bool Parser::HandlePragmaMSSegment(StringRef PragmaName,
                                   SourceLocation PragmaLocation) {
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_lparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // (
  Sema::PragmaMsStackAction Action = Sema::PSK_Reset;
  StringRef SlotLabel;
  if (Tok.isAnyIdentifier()) {
    StringRef PushPop = Tok.getIdentifierInfo()->getName();
    if (PushPop == "push")
      Action = Sema::PSK_Push;
    else if (PushPop == "pop")
      Action = Sema::PSK_Pop;
    else {
      PP.Diag(PragmaLocation,
              diag::warn_pragma_expected_section_push_pop_or_name)
          << PragmaName;
      return false;
    }
    if (Action != Sema::PSK_Reset) {
      PP.Lex(Tok); // push | pop
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok); // ,
        // If we've got a comma, we either need a label or a string.
        if (Tok.isAnyIdentifier()) {
          SlotLabel = Tok.getIdentifierInfo()->getName();
          PP.Lex(Tok); // identifier
          if (Tok.is(tok::comma))
            PP.Lex(Tok);
          else if (Tok.isNot(tok::r_paren)) {
            PP.Diag(PragmaLocation, diag::warn_pragma_expected_punc)
                << PragmaName;
            return false;
          }
        }
      } else if (Tok.isNot(tok::r_paren)) {
        PP.Diag(PragmaLocation, diag::warn_pragma_expected_punc) << PragmaName;
        return false;
      }
    }
  }
  // Grab the string literal for our section name.
  StringLiteral *SegmentName = nullptr;
  if (Tok.isNot(tok::r_paren)) {
    if (Tok.isNot(tok::string_literal)) {
      unsigned DiagID = Action != Sema::PSK_Reset ? !SlotLabel.empty() ?
          diag::warn_pragma_expected_section_name :
          diag::warn_pragma_expected_section_label_or_name :
          diag::warn_pragma_expected_section_push_pop_or_name;
      PP.Diag(PragmaLocation, DiagID) << PragmaName;
      return false;
    }
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false; // Already diagnosed.
    SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
    // Setting section "" has no effect
    if (SegmentName->getLength())
      Action = (Sema::PragmaMsStackAction)(Action | Sema::PSK_Set);
  }
  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_rparen) << PragmaName;
    return false;
  }
  PP.Lex(Tok); // )
  if (Tok.isNot(tok::eof)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_extra_tokens_at_eol)
        << PragmaName;
    return false;
  }
  PP.Lex(Tok); // eof
  Actions.ActOnPragmaMSSeg(PragmaLocation, Action, SlotLabel,
                           SegmentName, PragmaName);
  return true;
}

// #pragma init_seg({ compiler | lib | user | "section-name" [, func-name]} )
bool Parser::HandlePragmaMSInitSeg(StringRef PragmaName,
                                   SourceLocation PragmaLocation) {
  if (getTargetInfo().getTriple().getEnvironment() != llvm::Triple::MSVC) {
    PP.Diag(PragmaLocation, diag::warn_pragma_init_seg_unsupported_target);
    return false;
  }

  if (ExpectAndConsume(tok::l_paren, diag::warn_pragma_expected_lparen,
                       PragmaName))
    return false;

  // Parse either the known section names or the string section name.
  StringLiteral *SegmentName = nullptr;
  if (Tok.isAnyIdentifier()) {
    auto *II = Tok.getIdentifierInfo();
    StringRef Section = llvm::StringSwitch<StringRef>(II->getName())
                            .Case("compiler", "\".CRT$XCC\"")
                            .Case("lib", "\".CRT$XCL\"")
                            .Case("user", "\".CRT$XCU\"")
                            .Default("");

    if (!Section.empty()) {
      // Pretend the user wrote the appropriate string literal here.
      Token Toks[1];
      Toks[0].startToken();
      Toks[0].setKind(tok::string_literal);
      Toks[0].setLocation(Tok.getLocation());
      Toks[0].setLiteralData(Section.data());
      Toks[0].setLength(Section.size());
      SegmentName =
          cast<StringLiteral>(Actions.ActOnStringLiteral(Toks, nullptr).get());
      PP.Lex(Tok);
    }
  } else if (Tok.is(tok::string_literal)) {
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false;
    SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
    // FIXME: Add support for the '[, func-name]' part of the pragma.
  }

  if (!SegmentName) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_init_seg) << PragmaName;
    return false;
  }

  if (ExpectAndConsume(tok::r_paren, diag::warn_pragma_expected_rparen,
                       PragmaName) ||
      ExpectAndConsume(tok::eof, diag::warn_pragma_extra_tokens_at_eol,
                       PragmaName))
    return false;

  Actions.ActOnPragmaMSInitSeg(PragmaLocation, SegmentName);
  return true;
}

// #pragma strict_gs_check(pop)
// #pragma strict_gs_check(push, "on" | "off")
// #pragma strict_gs_check("on" | "off")
bool Parser::HandlePragmaMSStrictGuardStackCheck(
    StringRef PragmaName, SourceLocation PragmaLocation) {
  if (ExpectAndConsume(tok::l_paren, diag::warn_pragma_expected_lparen,
                       PragmaName))
    return false;

  Sema::PragmaMsStackAction Action = Sema::PSK_Set;
  if (Tok.is(tok::identifier)) {
    StringRef PushPop = Tok.getIdentifierInfo()->getName();
    if (PushPop == "push") {
      PP.Lex(Tok);
      Action = Sema::PSK_Push;
      if (ExpectAndConsume(tok::comma, diag::warn_pragma_expected_punc,
                           PragmaName))
        return false;
    } else if (PushPop == "pop") {
      PP.Lex(Tok);
      Action = Sema::PSK_Pop;
    }
  }

  bool Value = false;
  if (Action & Sema::PSK_Push || Action & Sema::PSK_Set) {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II && II->isStr("off")) {
      PP.Lex(Tok);
      Value = false;
    } else if (II && II->isStr("on")) {
      PP.Lex(Tok);
      Value = true;
    } else {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_action)
          << PragmaName;
      return false;
    }
  }

  // Finish the pragma: ')' $
  if (ExpectAndConsume(tok::r_paren, diag::warn_pragma_expected_rparen,
                       PragmaName))
    return false;

  if (ExpectAndConsume(tok::eof, diag::warn_pragma_extra_tokens_at_eol,
                       PragmaName))
    return false;

  Actions.ActOnPragmaMSStrictGuardStackCheck(PragmaLocation, Action, Value);
  return true;
}

bool Parser::HandlePragmaMSAllocText(StringRef PragmaName,
                                     SourceLocation PragmaLocation) {
  Token FirstTok = Tok;
  if (ExpectAndConsume(tok::l_paren, diag::warn_pragma_expected_lparen,
                       PragmaName))
    return false;

  StringRef Section;
  if (Tok.is(tok::string_literal)) {
    ExprResult StringResult = ParseStringLiteralExpression();
    if (StringResult.isInvalid())
      return false; // Already diagnosed.
    StringLiteral *SegmentName = cast<StringLiteral>(StringResult.get());
    if (SegmentName->getCharByteWidth() != 1) {
      PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
          << PragmaName;
      return false;
    }
    Section = SegmentName->getString();
  } else if (Tok.is(tok::identifier)) {
    Section = Tok.getIdentifierInfo()->getName();
    PP.Lex(Tok);
  } else {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_section_name)
        << PragmaName;
    return false;
  }

  if (ExpectAndConsume(tok::comma, diag::warn_pragma_expected_comma,
                       PragmaName))
    return false;

  SmallVector<std::tuple<IdentifierInfo *, SourceLocation>> Functions;
  while (true) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::warn_pragma_expected_identifier)
          << PragmaName;
      return false;
    }

    IdentifierInfo *II = Tok.getIdentifierInfo();
    Functions.emplace_back(II, Tok.getLocation());

    PP.Lex(Tok);
    if (Tok.isNot(tok::comma))
      break;
    PP.Lex(Tok);
  }

  if (ExpectAndConsume(tok::r_paren, diag::warn_pragma_expected_rparen,
                       PragmaName) ||
      ExpectAndConsume(tok::eof, diag::warn_pragma_extra_tokens_at_eol,
                       PragmaName))
    return false;

  Actions.ActOnPragmaMSAllocText(FirstTok.getLocation(), Section, Functions);
  return true;
}

static std::string PragmaLoopHintString(Token PragmaName, Token Option) {
  StringRef Str = PragmaName.getIdentifierInfo()->getName();
  std::string ClangLoopStr("clang loop ");
  if (Str == "loop" && Option.getIdentifierInfo())
    ClangLoopStr += Option.getIdentifierInfo()->getName();
  return std::string(llvm::StringSwitch<StringRef>(Str)
                         .Case("loop", ClangLoopStr)
                         .Case("unroll_and_jam", Str)
                         .Case("unroll", Str)
                         .Default(""));
}

bool Parser::HandlePragmaLoopHint(LoopHint &Hint) {
  assert(Tok.is(tok::annot_pragma_loop_hint));
  PragmaLoopHintInfo *Info =
      static_cast<PragmaLoopHintInfo *>(Tok.getAnnotationValue());

  IdentifierInfo *PragmaNameInfo = Info->PragmaName.getIdentifierInfo();
  Hint.PragmaNameLoc = IdentifierLoc::create(
      Actions.Context, Info->PragmaName.getLocation(), PragmaNameInfo);

  // It is possible that the loop hint has no option identifier, such as
  // #pragma unroll(4).
  IdentifierInfo *OptionInfo = Info->Option.is(tok::identifier)
                                   ? Info->Option.getIdentifierInfo()
                                   : nullptr;
  Hint.OptionLoc = IdentifierLoc::create(
      Actions.Context, Info->Option.getLocation(), OptionInfo);

  llvm::ArrayRef<Token> Toks = Info->Toks;

  // Return a valid hint if pragma unroll or nounroll were specified
  // without an argument.
  auto IsLoopHint = llvm::StringSwitch<bool>(PragmaNameInfo->getName())
                        .Cases("unroll", "nounroll", "unroll_and_jam",
                               "nounroll_and_jam", true)
                        .Default(false);

  if (Toks.empty() && IsLoopHint) {
    ConsumeAnnotationToken();
    Hint.Range = Info->PragmaName.getLocation();
    return true;
  }

  // The constant expression is always followed by an eof token, which increases
  // the TokSize by 1.
  assert(!Toks.empty() &&
         "PragmaLoopHintInfo::Toks must contain at least one token.");

  // If no option is specified the argument is assumed to be a constant expr.
  bool OptionUnroll = false;
  bool OptionUnrollAndJam = false;
  bool OptionDistribute = false;
  bool OptionPipelineDisabled = false;
  bool StateOption = false;
  if (OptionInfo) { // Pragma Unroll does not specify an option.
    OptionUnroll = OptionInfo->isStr("unroll");
    OptionUnrollAndJam = OptionInfo->isStr("unroll_and_jam");
    OptionDistribute = OptionInfo->isStr("distribute");
    OptionPipelineDisabled = OptionInfo->isStr("pipeline");
    StateOption = llvm::StringSwitch<bool>(OptionInfo->getName())
                      .Case("vectorize", true)
                      .Case("interleave", true)
                      .Case("vectorize_predicate", true)
                      .Default(false) ||
                  OptionUnroll || OptionUnrollAndJam || OptionDistribute ||
                  OptionPipelineDisabled;
  }

  bool AssumeSafetyArg = !OptionUnroll && !OptionUnrollAndJam &&
                         !OptionDistribute && !OptionPipelineDisabled;
  // Verify loop hint has an argument.
  if (Toks[0].is(tok::eof)) {
    ConsumeAnnotationToken();
    Diag(Toks[0].getLocation(), diag::err_pragma_loop_missing_argument)
        << /*StateArgument=*/StateOption
        << /*FullKeyword=*/(OptionUnroll || OptionUnrollAndJam)
        << /*AssumeSafetyKeyword=*/AssumeSafetyArg;
    return false;
  }

  // Validate the argument.
  if (StateOption) {
    ConsumeAnnotationToken();
    SourceLocation StateLoc = Toks[0].getLocation();
    IdentifierInfo *StateInfo = Toks[0].getIdentifierInfo();

    bool Valid = StateInfo &&
                 llvm::StringSwitch<bool>(StateInfo->getName())
                     .Case("disable", true)
                     .Case("enable", !OptionPipelineDisabled)
                     .Case("full", OptionUnroll || OptionUnrollAndJam)
                     .Case("assume_safety", AssumeSafetyArg)
                     .Default(false);
    if (!Valid) {
      if (OptionPipelineDisabled) {
        Diag(Toks[0].getLocation(), diag::err_pragma_pipeline_invalid_keyword);
      } else {
        Diag(Toks[0].getLocation(), diag::err_pragma_invalid_keyword)
            << /*FullKeyword=*/(OptionUnroll || OptionUnrollAndJam)
            << /*AssumeSafetyKeyword=*/AssumeSafetyArg;
      }
      return false;
    }
    if (Toks.size() > 2)
      Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
          << PragmaLoopHintString(Info->PragmaName, Info->Option);
    Hint.StateLoc = IdentifierLoc::create(Actions.Context, StateLoc, StateInfo);
  } else if (OptionInfo && OptionInfo->getName() == "vectorize_width") {
    PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/false,
                        /*IsReinject=*/false);
    ConsumeAnnotationToken();

    SourceLocation StateLoc = Toks[0].getLocation();
    IdentifierInfo *StateInfo = Toks[0].getIdentifierInfo();
    StringRef IsScalableStr = StateInfo ? StateInfo->getName() : "";

    // Look for vectorize_width(fixed|scalable)
    if (IsScalableStr == "scalable" || IsScalableStr == "fixed") {
      PP.Lex(Tok); // Identifier

      if (Toks.size() > 2) {
        Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
            << PragmaLoopHintString(Info->PragmaName, Info->Option);
        while (Tok.isNot(tok::eof))
          ConsumeAnyToken();
      }

      Hint.StateLoc =
          IdentifierLoc::create(Actions.Context, StateLoc, StateInfo);

      ConsumeToken(); // Consume the constant expression eof terminator.
    } else {
      // Enter constant expression including eof terminator into token stream.
      ExprResult R = ParseConstantExpression();

      if (R.isInvalid() && !Tok.is(tok::comma))
        Diag(Toks[0].getLocation(),
             diag::note_pragma_loop_invalid_vectorize_option);

      bool Arg2Error = false;
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok); // ,

        StateInfo = Tok.getIdentifierInfo();
        IsScalableStr = StateInfo->getName();

        if (IsScalableStr != "scalable" && IsScalableStr != "fixed") {
          Diag(Tok.getLocation(),
               diag::err_pragma_loop_invalid_vectorize_option);
          Arg2Error = true;
        } else
          Hint.StateLoc =
              IdentifierLoc::create(Actions.Context, StateLoc, StateInfo);

        PP.Lex(Tok); // Identifier
      }

      // Tokens following an error in an ill-formed constant expression will
      // remain in the token stream and must be removed.
      if (Tok.isNot(tok::eof)) {
        Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
            << PragmaLoopHintString(Info->PragmaName, Info->Option);
        while (Tok.isNot(tok::eof))
          ConsumeAnyToken();
      }

      ConsumeToken(); // Consume the constant expression eof terminator.

      if (Arg2Error || R.isInvalid() ||
          Actions.CheckLoopHintExpr(R.get(), Toks[0].getLocation(),
                                    /*AllowZero=*/false))
        return false;

      // Argument is a constant expression with an integer type.
      Hint.ValueExpr = R.get();
    }
  } else {
    // Enter constant expression including eof terminator into token stream.
    PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/false,
                        /*IsReinject=*/false);
    ConsumeAnnotationToken();
    ExprResult R = ParseConstantExpression();

    // Tokens following an error in an ill-formed constant expression will
    // remain in the token stream and must be removed.
    if (Tok.isNot(tok::eof)) {
      Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
          << PragmaLoopHintString(Info->PragmaName, Info->Option);
      while (Tok.isNot(tok::eof))
        ConsumeAnyToken();
    }

    ConsumeToken(); // Consume the constant expression eof terminator.

    if (R.isInvalid() ||
        Actions.CheckLoopHintExpr(R.get(), Toks[0].getLocation(),
                                  /*AllowZero=*/true))
      return false;

    // Argument is a constant expression with an integer type.
    Hint.ValueExpr = R.get();
  }

  Hint.Range = SourceRange(Info->PragmaName.getLocation(),
                           Info->Toks.back().getLocation());
  return true;
}

namespace {
struct PragmaAttributeInfo {
  enum ActionType { Push, Pop, Attribute };
  ParsedAttributes &Attributes;
  ActionType Action;
  const IdentifierInfo *Namespace = nullptr;
  ArrayRef<Token> Tokens;

  PragmaAttributeInfo(ParsedAttributes &Attributes) : Attributes(Attributes) {}
};

#include "clang/Parse/AttrSubMatchRulesParserStringSwitches.inc"

} // end anonymous namespace

static StringRef getIdentifier(const Token &Tok) {
  if (Tok.is(tok::identifier))
    return Tok.getIdentifierInfo()->getName();
  const char *S = tok::getKeywordSpelling(Tok.getKind());
  if (!S)
    return "";
  return S;
}

static bool isAbstractAttrMatcherRule(attr::SubjectMatchRule Rule) {
  using namespace attr;
  switch (Rule) {
#define ATTR_MATCH_RULE(Value, Spelling, IsAbstract)                           \
  case Value:                                                                  \
    return IsAbstract;
#include "clang/Basic/AttrSubMatchRulesList.inc"
  }
  llvm_unreachable("Invalid attribute subject match rule");
  return false;
}

static void diagnoseExpectedAttributeSubjectSubRule(
    Parser &PRef, attr::SubjectMatchRule PrimaryRule, StringRef PrimaryRuleName,
    SourceLocation SubRuleLoc) {
  auto Diagnostic =
      PRef.Diag(SubRuleLoc,
                diag::err_pragma_attribute_expected_subject_sub_identifier)
      << PrimaryRuleName;
  if (const char *SubRules = validAttributeSubjectMatchSubRules(PrimaryRule))
    Diagnostic << /*SubRulesSupported=*/1 << SubRules;
  else
    Diagnostic << /*SubRulesSupported=*/0;
}

static void diagnoseUnknownAttributeSubjectSubRule(
    Parser &PRef, attr::SubjectMatchRule PrimaryRule, StringRef PrimaryRuleName,
    StringRef SubRuleName, SourceLocation SubRuleLoc) {

  auto Diagnostic =
      PRef.Diag(SubRuleLoc, diag::err_pragma_attribute_unknown_subject_sub_rule)
      << SubRuleName << PrimaryRuleName;
  if (const char *SubRules = validAttributeSubjectMatchSubRules(PrimaryRule))
    Diagnostic << /*SubRulesSupported=*/1 << SubRules;
  else
    Diagnostic << /*SubRulesSupported=*/0;
}

bool Parser::ParsePragmaAttributeSubjectMatchRuleSet(
    attr::ParsedSubjectMatchRuleSet &SubjectMatchRules, SourceLocation &AnyLoc,
    SourceLocation &LastMatchRuleEndLoc) {
  bool IsAny = false;
  BalancedDelimiterTracker AnyParens(*this, tok::l_paren);
  if (getIdentifier(Tok) == "any") {
    AnyLoc = ConsumeToken();
    IsAny = true;
    if (AnyParens.expectAndConsume())
      return true;
  }

  do {
    // Parse the subject matcher rule.
    StringRef Name = getIdentifier(Tok);
    if (Name.empty()) {
      Diag(Tok, diag::err_pragma_attribute_expected_subject_identifier);
      return true;
    }
    std::pair<std::optional<attr::SubjectMatchRule>,
              std::optional<attr::SubjectMatchRule> (*)(StringRef, bool)>
        Rule = isAttributeSubjectMatchRule(Name);
    if (!Rule.first) {
      Diag(Tok, diag::err_pragma_attribute_unknown_subject_rule) << Name;
      return true;
    }
    attr::SubjectMatchRule PrimaryRule = *Rule.first;
    SourceLocation RuleLoc = ConsumeToken();

    BalancedDelimiterTracker Parens(*this, tok::l_paren);
    if (isAbstractAttrMatcherRule(PrimaryRule)) {
      if (Parens.expectAndConsume())
        return true;
    } else if (Parens.consumeOpen()) {
      if (!SubjectMatchRules
               .insert(
                   std::make_pair(PrimaryRule, SourceRange(RuleLoc, RuleLoc)))
               .second)
        Diag(RuleLoc, diag::err_pragma_attribute_duplicate_subject)
            << Name
            << FixItHint::CreateRemoval(SourceRange(
                   RuleLoc, Tok.is(tok::comma) ? Tok.getLocation() : RuleLoc));
      LastMatchRuleEndLoc = RuleLoc;
      continue;
    }

    // Parse the sub-rules.
    StringRef SubRuleName = getIdentifier(Tok);
    if (SubRuleName.empty()) {
      diagnoseExpectedAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                              Tok.getLocation());
      return true;
    }
    attr::SubjectMatchRule SubRule;
    if (SubRuleName == "unless") {
      SourceLocation SubRuleLoc = ConsumeToken();
      BalancedDelimiterTracker Parens(*this, tok::l_paren);
      if (Parens.expectAndConsume())
        return true;
      SubRuleName = getIdentifier(Tok);
      if (SubRuleName.empty()) {
        diagnoseExpectedAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                                SubRuleLoc);
        return true;
      }
      auto SubRuleOrNone = Rule.second(SubRuleName, /*IsUnless=*/true);
      if (!SubRuleOrNone) {
        std::string SubRuleUnlessName = "unless(" + SubRuleName.str() + ")";
        diagnoseUnknownAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                               SubRuleUnlessName, SubRuleLoc);
        return true;
      }
      SubRule = *SubRuleOrNone;
      ConsumeToken();
      if (Parens.consumeClose())
        return true;
    } else {
      auto SubRuleOrNone = Rule.second(SubRuleName, /*IsUnless=*/false);
      if (!SubRuleOrNone) {
        diagnoseUnknownAttributeSubjectSubRule(*this, PrimaryRule, Name,
                                               SubRuleName, Tok.getLocation());
        return true;
      }
      SubRule = *SubRuleOrNone;
      ConsumeToken();
    }
    SourceLocation RuleEndLoc = Tok.getLocation();
    LastMatchRuleEndLoc = RuleEndLoc;
    if (Parens.consumeClose())
      return true;
    if (!SubjectMatchRules
             .insert(std::make_pair(SubRule, SourceRange(RuleLoc, RuleEndLoc)))
             .second) {
      Diag(RuleLoc, diag::err_pragma_attribute_duplicate_subject)
          << attr::getSubjectMatchRuleSpelling(SubRule)
          << FixItHint::CreateRemoval(SourceRange(
                 RuleLoc, Tok.is(tok::comma) ? Tok.getLocation() : RuleEndLoc));
      continue;
    }
  } while (IsAny && TryConsumeToken(tok::comma));

  if (IsAny)
    if (AnyParens.consumeClose())
      return true;

  return false;
}

namespace {

/// Describes the stage at which attribute subject rule parsing was interrupted.
enum class MissingAttributeSubjectRulesRecoveryPoint {
  Comma,
  ApplyTo,
  Equals,
  Any,
  None,
};

MissingAttributeSubjectRulesRecoveryPoint
getAttributeSubjectRulesRecoveryPointForToken(const Token &Tok) {
  if (const auto *II = Tok.getIdentifierInfo()) {
    if (II->isStr("apply_to"))
      return MissingAttributeSubjectRulesRecoveryPoint::ApplyTo;
    if (II->isStr("any"))
      return MissingAttributeSubjectRulesRecoveryPoint::Any;
  }
  if (Tok.is(tok::equal))
    return MissingAttributeSubjectRulesRecoveryPoint::Equals;
  return MissingAttributeSubjectRulesRecoveryPoint::None;
}

/// Creates a diagnostic for the attribute subject rule parsing diagnostic that
/// suggests the possible attribute subject rules in a fix-it together with
/// any other missing tokens.
DiagnosticBuilder createExpectedAttributeSubjectRulesTokenDiagnostic(
    unsigned DiagID, ParsedAttributes &Attrs,
    MissingAttributeSubjectRulesRecoveryPoint Point, Parser &PRef) {
  SourceLocation Loc = PRef.getEndOfPreviousToken();
  if (Loc.isInvalid())
    Loc = PRef.getCurToken().getLocation();
  auto Diagnostic = PRef.Diag(Loc, DiagID);
  std::string FixIt;
  MissingAttributeSubjectRulesRecoveryPoint EndPoint =
      getAttributeSubjectRulesRecoveryPointForToken(PRef.getCurToken());
  if (Point == MissingAttributeSubjectRulesRecoveryPoint::Comma)
    FixIt = ", ";
  if (Point <= MissingAttributeSubjectRulesRecoveryPoint::ApplyTo &&
      EndPoint > MissingAttributeSubjectRulesRecoveryPoint::ApplyTo)
    FixIt += "apply_to";
  if (Point <= MissingAttributeSubjectRulesRecoveryPoint::Equals &&
      EndPoint > MissingAttributeSubjectRulesRecoveryPoint::Equals)
    FixIt += " = ";
  SourceRange FixItRange(Loc);
  if (EndPoint == MissingAttributeSubjectRulesRecoveryPoint::None) {
    // Gather the subject match rules that are supported by the attribute.
    // Add all the possible rules initially.
    llvm::BitVector IsMatchRuleAvailable(attr::SubjectMatchRule_Last + 1, true);
    // Remove the ones that are not supported by any of the attributes.
    for (const ParsedAttr &Attribute : Attrs) {
      SmallVector<std::pair<attr::SubjectMatchRule, bool>, 4> MatchRules;
      Attribute.getMatchRules(PRef.getLangOpts(), MatchRules);
      llvm::BitVector IsSupported(attr::SubjectMatchRule_Last + 1);
      for (const auto &Rule : MatchRules) {
        // Ensure that the missing rule is reported in the fix-it only when it's
        // supported in the current language mode.
        if (!Rule.second)
          continue;
        IsSupported[Rule.first] = true;
      }
      IsMatchRuleAvailable &= IsSupported;
    }
    if (IsMatchRuleAvailable.count() == 0) {
      // FIXME: We can emit a "fix-it" with a subject list placeholder when
      // placeholders will be supported by the fix-its.
      return Diagnostic;
    }
    FixIt += "any(";
    bool NeedsComma = false;
    for (unsigned I = 0; I <= attr::SubjectMatchRule_Last; I++) {
      if (!IsMatchRuleAvailable[I])
        continue;
      if (NeedsComma)
        FixIt += ", ";
      else
        NeedsComma = true;
      FixIt += attr::getSubjectMatchRuleSpelling(
          static_cast<attr::SubjectMatchRule>(I));
    }
    FixIt += ")";
    // Check if we need to remove the range
    PRef.SkipUntil(tok::eof, Parser::StopBeforeMatch);
    FixItRange.setEnd(PRef.getCurToken().getLocation());
  }
  if (FixItRange.getBegin() == FixItRange.getEnd())
    Diagnostic << FixItHint::CreateInsertion(FixItRange.getBegin(), FixIt);
  else
    Diagnostic << FixItHint::CreateReplacement(
        CharSourceRange::getCharRange(FixItRange), FixIt);
  return Diagnostic;
}

} // end anonymous namespace

void Parser::HandlePragmaAttribute() {
  assert(Tok.is(tok::annot_pragma_attribute) &&
         "Expected #pragma attribute annotation token");
  SourceLocation PragmaLoc = Tok.getLocation();
  auto *Info = static_cast<PragmaAttributeInfo *>(Tok.getAnnotationValue());
  if (Info->Action == PragmaAttributeInfo::Pop) {
    ConsumeAnnotationToken();
    Actions.ActOnPragmaAttributePop(PragmaLoc, Info->Namespace);
    return;
  }
  // Parse the actual attribute with its arguments.
  assert((Info->Action == PragmaAttributeInfo::Push ||
          Info->Action == PragmaAttributeInfo::Attribute) &&
         "Unexpected #pragma attribute command");

  if (Info->Action == PragmaAttributeInfo::Push && Info->Tokens.empty()) {
    ConsumeAnnotationToken();
    Actions.ActOnPragmaAttributeEmptyPush(PragmaLoc, Info->Namespace);
    return;
  }

  PP.EnterTokenStream(Info->Tokens, /*DisableMacroExpansion=*/false,
                      /*IsReinject=*/false);
  ConsumeAnnotationToken();

  ParsedAttributes &Attrs = Info->Attributes;
  Attrs.clearListOnly();

  auto SkipToEnd = [this]() {
    SkipUntil(tok::eof, StopBeforeMatch);
    ConsumeToken();
  };

  if ((Tok.is(tok::l_square) && NextToken().is(tok::l_square)) ||
      Tok.isRegularKeywordAttribute()) {
    // Parse the CXX11 style attribute.
    ParseCXX11AttributeSpecifier(Attrs);
  } else if (Tok.is(tok::kw___attribute)) {
    ConsumeToken();
    if (ExpectAndConsume(tok::l_paren, diag::err_expected_lparen_after,
                         "attribute"))
      return SkipToEnd();
    if (ExpectAndConsume(tok::l_paren, diag::err_expected_lparen_after, "("))
      return SkipToEnd();

    // FIXME: The practical usefulness of completion here is limited because
    // we only get here if the line has balanced parens.
    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      // FIXME: suppress completion of unsupported attributes?
      Actions.CodeCompletion().CodeCompleteAttribute(
          AttributeCommonInfo::Syntax::AS_GNU);
      return SkipToEnd();
    }

    // Parse the comma-separated list of attributes.
    do {
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_pragma_attribute_expected_attribute_name);
        SkipToEnd();
        return;
      }
      IdentifierInfo *AttrName = Tok.getIdentifierInfo();
      SourceLocation AttrNameLoc = ConsumeToken();

      if (Tok.isNot(tok::l_paren))
        Attrs.addNew(AttrName, AttrNameLoc, nullptr, AttrNameLoc, nullptr, 0,
                     ParsedAttr::Form::GNU());
      else
        ParseGNUAttributeArgs(AttrName, AttrNameLoc, Attrs, /*EndLoc=*/nullptr,
                              /*ScopeName=*/nullptr,
                              /*ScopeLoc=*/SourceLocation(),
                              ParsedAttr::Form::GNU(),
                              /*Declarator=*/nullptr);
    } while (TryConsumeToken(tok::comma));

    if (ExpectAndConsume(tok::r_paren))
      return SkipToEnd();
    if (ExpectAndConsume(tok::r_paren))
      return SkipToEnd();
  } else if (Tok.is(tok::kw___declspec)) {
    ParseMicrosoftDeclSpecs(Attrs);
  } else {
    Diag(Tok, diag::err_pragma_attribute_expected_attribute_syntax);
    if (Tok.getIdentifierInfo()) {
      // If we suspect that this is an attribute suggest the use of
      // '__attribute__'.
      if (ParsedAttr::getParsedKind(
              Tok.getIdentifierInfo(), /*ScopeName=*/nullptr,
              ParsedAttr::AS_GNU) != ParsedAttr::UnknownAttribute) {
        SourceLocation InsertStartLoc = Tok.getLocation();
        ConsumeToken();
        if (Tok.is(tok::l_paren)) {
          ConsumeAnyToken();
          SkipUntil(tok::r_paren, StopBeforeMatch);
          if (Tok.isNot(tok::r_paren))
            return SkipToEnd();
        }
        Diag(Tok, diag::note_pragma_attribute_use_attribute_kw)
            << FixItHint::CreateInsertion(InsertStartLoc, "__attribute__((")
            << FixItHint::CreateInsertion(Tok.getEndLoc(), "))");
      }
    }
    SkipToEnd();
    return;
  }

  if (Attrs.empty() || Attrs.begin()->isInvalid()) {
    SkipToEnd();
    return;
  }

  for (const ParsedAttr &Attribute : Attrs) {
    if (!Attribute.isSupportedByPragmaAttribute()) {
      Diag(PragmaLoc, diag::err_pragma_attribute_unsupported_attribute)
          << Attribute;
      SkipToEnd();
      return;
    }
  }

  // Parse the subject-list.
  if (!TryConsumeToken(tok::comma)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_expected, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::Comma, *this)
        << tok::comma;
    SkipToEnd();
    return;
  }

  if (Tok.isNot(tok::identifier)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_pragma_attribute_invalid_subject_set_specifier, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::ApplyTo, *this);
    SkipToEnd();
    return;
  }
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  if (!II->isStr("apply_to")) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_pragma_attribute_invalid_subject_set_specifier, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::ApplyTo, *this);
    SkipToEnd();
    return;
  }
  ConsumeToken();

  if (!TryConsumeToken(tok::equal)) {
    createExpectedAttributeSubjectRulesTokenDiagnostic(
        diag::err_expected, Attrs,
        MissingAttributeSubjectRulesRecoveryPoint::Equals, *this)
        << tok::equal;
    SkipToEnd();
    return;
  }

  attr::ParsedSubjectMatchRuleSet SubjectMatchRules;
  SourceLocation AnyLoc, LastMatchRuleEndLoc;
  if (ParsePragmaAttributeSubjectMatchRuleSet(SubjectMatchRules, AnyLoc,
                                              LastMatchRuleEndLoc)) {
    SkipToEnd();
    return;
  }

  // Tokens following an ill-formed attribute will remain in the token stream
  // and must be removed.
  if (Tok.isNot(tok::eof)) {
    Diag(Tok, diag::err_pragma_attribute_extra_tokens_after_attribute);
    SkipToEnd();
    return;
  }

  // Consume the eof terminator token.
  ConsumeToken();

  // Handle a mixed push/attribute by desurging to a push, then an attribute.
  if (Info->Action == PragmaAttributeInfo::Push)
    Actions.ActOnPragmaAttributeEmptyPush(PragmaLoc, Info->Namespace);

  for (ParsedAttr &Attribute : Attrs) {
    Actions.ActOnPragmaAttributeAttribute(Attribute, PragmaLoc,
                                          SubjectMatchRules);
  }
}


// #pragma clang section bss="abc" data="" rodata="def" text="" relro=""
void PragmaClangSectionHandler::HandlePragma(Preprocessor &PP,
                                             PragmaIntroducer Introducer,
                                             Token &FirstToken) {

  Token Tok;
  auto SecKind = Sema::PragmaClangSectionKind::PCSK_Invalid;

  PP.Lex(Tok); // eat 'section'
  while (Tok.isNot(tok::eod)) {
    if (Tok.isNot(tok::identifier)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_expected_clang_section_name) << "clang section";
      return;
    }

    const IdentifierInfo *SecType = Tok.getIdentifierInfo();
    if (SecType->isStr("bss"))
      SecKind = Sema::PragmaClangSectionKind::PCSK_BSS;
    else if (SecType->isStr("data"))
      SecKind = Sema::PragmaClangSectionKind::PCSK_Data;
    else if (SecType->isStr("rodata"))
      SecKind = Sema::PragmaClangSectionKind::PCSK_Rodata;
    else if (SecType->isStr("relro"))
      SecKind = Sema::PragmaClangSectionKind::PCSK_Relro;
    else if (SecType->isStr("text"))
      SecKind = Sema::PragmaClangSectionKind::PCSK_Text;
    else {
      PP.Diag(Tok.getLocation(), diag::err_pragma_expected_clang_section_name) << "clang section";
      return;
    }

    SourceLocation PragmaLocation = Tok.getLocation();
    PP.Lex(Tok); // eat ['bss'|'data'|'rodata'|'text']
    if (Tok.isNot(tok::equal)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_clang_section_expected_equal) << SecKind;
      return;
    }

    std::string SecName;
    if (!PP.LexStringLiteral(Tok, SecName, "pragma clang section", false))
      return;

    Actions.ActOnPragmaClangSection(
        PragmaLocation,
        (SecName.size() ? Sema::PragmaClangSectionAction::PCSA_Set
                        : Sema::PragmaClangSectionAction::PCSA_Clear),
        SecKind, SecName);
  }
}

/// Handle the Microsoft \#pragma detect_mismatch extension.
///
/// The syntax is:
/// \code
///   #pragma detect_mismatch("name", "value")
/// \endcode
/// Where 'name' and 'value' are quoted strings.  The values are embedded in
/// the object file and passed along to the linker.  If the linker detects a
/// mismatch in the object file's values for the given name, a LNK2038 error
/// is emitted.  See MSDN for more details.
void PragmaDetectMismatchHandler::HandlePragma(Preprocessor &PP,
                                               PragmaIntroducer Introducer,
                                               Token &Tok) {
  SourceLocation DetectMismatchLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(DetectMismatchLoc, diag::err_expected) << tok::l_paren;
    return;
  }

  // Read the name to embed, which must be a string literal.
  std::string NameString;
  if (!PP.LexStringLiteral(Tok, NameString,
                           "pragma detect_mismatch",
                           /*AllowMacroExpansion=*/true))
    return;

  // Read the comma followed by a second string literal.
  std::string ValueString;
  if (Tok.isNot(tok::comma)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_detect_mismatch_malformed);
    return;
  }

  if (!PP.LexStringLiteral(Tok, ValueString, "pragma detect_mismatch",
                           /*AllowMacroExpansion=*/true))
    return;

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
    return;
  }
  PP.Lex(Tok);  // Eat the r_paren.

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_detect_mismatch_malformed);
    return;
  }

  // If the pragma is lexically sound, notify any interested PPCallbacks.
  if (PP.getPPCallbacks())
    PP.getPPCallbacks()->PragmaDetectMismatch(DetectMismatchLoc, NameString,
                                              ValueString);

  Actions.ActOnPragmaDetectMismatch(DetectMismatchLoc, NameString, ValueString);
}

/// Handle the microsoft \#pragma comment extension.
///
/// The syntax is:
/// \code
///   #pragma comment(linker, "foo")
/// \endcode
/// 'linker' is one of five identifiers: compiler, exestr, lib, linker, user.
/// "foo" is a string, which is fully macro expanded, and permits string
/// concatenation, embedded escape characters etc.  See MSDN for more details.
void PragmaCommentHandler::HandlePragma(Preprocessor &PP,
                                        PragmaIntroducer Introducer,
                                        Token &Tok) {
  SourceLocation CommentLoc = Tok.getLocation();
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(CommentLoc, diag::err_pragma_comment_malformed);
    return;
  }

  // Read the identifier.
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(CommentLoc, diag::err_pragma_comment_malformed);
    return;
  }

  // Verify that this is one of the 5 explicitly listed options.
  IdentifierInfo *II = Tok.getIdentifierInfo();
  PragmaMSCommentKind Kind =
    llvm::StringSwitch<PragmaMSCommentKind>(II->getName())
    .Case("linker",   PCK_Linker)
    .Case("lib",      PCK_Lib)
    .Case("compiler", PCK_Compiler)
    .Case("exestr",   PCK_ExeStr)
    .Case("user",     PCK_User)
    .Default(PCK_Unknown);
  if (Kind == PCK_Unknown) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_unknown_kind);
    return;
  }

  if (PP.getTargetInfo().getTriple().isOSBinFormatELF() && Kind != PCK_Lib) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_comment_ignored)
        << II->getName();
    return;
  }

  // Read the optional string if present.
  PP.Lex(Tok);
  std::string ArgumentString;
  if (Tok.is(tok::comma) && !PP.LexStringLiteral(Tok, ArgumentString,
                                                 "pragma comment",
                                                 /*AllowMacroExpansion=*/true))
    return;

  // FIXME: warn that 'exestr' is deprecated.
  // FIXME: If the kind is "compiler" warn if the string is present (it is
  // ignored).
  // The MSDN docs say that "lib" and "linker" require a string and have a short
  // list of linker options they support, but in practice MSVC doesn't
  // issue a diagnostic.  Therefore neither does clang.

  if (Tok.isNot(tok::r_paren)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_malformed);
    return;
  }
  PP.Lex(Tok);  // eat the r_paren.

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_comment_malformed);
    return;
  }

  // If the pragma is lexically sound, notify any interested PPCallbacks.
  if (PP.getPPCallbacks())
    PP.getPPCallbacks()->PragmaComment(CommentLoc, II, ArgumentString);

  Actions.ActOnPragmaMSComment(CommentLoc, Kind, ArgumentString);
}

/// Handle the \#pragma float_control extension.
///
/// The syntax is:
/// \code
///   #pragma float_control(keyword[, setting] [,push])
/// \endcode
/// Where 'keyword' and 'setting' are identifiers.
// 'keyword' can be: precise, except, push, pop
// 'setting' can be: on, off
/// The optional arguments 'setting' and 'push' are supported only
/// when the keyword is 'precise' or 'except'.
void PragmaFloatControlHandler::HandlePragma(Preprocessor &PP,
                                             PragmaIntroducer Introducer,
                                             Token &Tok) {
  Sema::PragmaMsStackAction Action = Sema::PSK_Set;
  SourceLocation FloatControlLoc = Tok.getLocation();
  Token PragmaName = Tok;
  if (!PP.getTargetInfo().hasStrictFP() && !PP.getLangOpts().ExpStrictFP) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_fp_ignored)
        << PragmaName.getIdentifierInfo()->getName();
    return;
  }
  PP.Lex(Tok);
  if (Tok.isNot(tok::l_paren)) {
    PP.Diag(FloatControlLoc, diag::err_expected) << tok::l_paren;
    return;
  }

  // Read the identifier.
  PP.Lex(Tok);
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
    return;
  }

  // Verify that this is one of the float control options.
  IdentifierInfo *II = Tok.getIdentifierInfo();
  PragmaFloatControlKind Kind =
      llvm::StringSwitch<PragmaFloatControlKind>(II->getName())
          .Case("precise", PFC_Precise)
          .Case("except", PFC_Except)
          .Case("push", PFC_Push)
          .Case("pop", PFC_Pop)
          .Default(PFC_Unknown);
  PP.Lex(Tok); // the identifier
  if (Kind == PFC_Unknown) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
    return;
  } else if (Kind == PFC_Push || Kind == PFC_Pop) {
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
      return;
    }
    PP.Lex(Tok); // Eat the r_paren
    Action = (Kind == PFC_Pop) ? Sema::PSK_Pop : Sema::PSK_Push;
  } else {
    if (Tok.is(tok::r_paren))
      // Selecting Precise or Except
      PP.Lex(Tok); // the r_paren
    else if (Tok.isNot(tok::comma)) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
      return;
    } else {
      PP.Lex(Tok); // ,
      if (!Tok.isAnyIdentifier()) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      StringRef PushOnOff = Tok.getIdentifierInfo()->getName();
      if (PushOnOff == "on")
        // Kind is set correctly
        ;
      else if (PushOnOff == "off") {
        if (Kind == PFC_Precise)
          Kind = PFC_NoPrecise;
        if (Kind == PFC_Except)
          Kind = PFC_NoExcept;
      } else if (PushOnOff == "push") {
        Action = Sema::PSK_Push_Set;
      } else {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      PP.Lex(Tok); // the identifier
      if (Tok.is(tok::comma)) {
        PP.Lex(Tok); // ,
        if (!Tok.isAnyIdentifier()) {
          PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
          return;
        }
        StringRef ExpectedPush = Tok.getIdentifierInfo()->getName();
        if (ExpectedPush == "push") {
          Action = Sema::PSK_Push_Set;
        } else {
          PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
          return;
        }
        PP.Lex(Tok); // the push identifier
      }
      if (Tok.isNot(tok::r_paren)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_float_control_malformed);
        return;
      }
      PP.Lex(Tok); // the r_paren
    }
  }
  SourceLocation EndLoc = Tok.getLocation();
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "float_control";
    return;
  }

  // Note: there is no accomodation for PP callback for this pragma.

  // Enter the annotation.
  auto TokenArray = std::make_unique<Token[]>(1);
  TokenArray[0].startToken();
  TokenArray[0].setKind(tok::annot_pragma_float_control);
  TokenArray[0].setLocation(FloatControlLoc);
  TokenArray[0].setAnnotationEndLoc(EndLoc);
  // Create an encoding of Action and Value by shifting the Action into
  // the high 16 bits then union with the Kind.
  TokenArray[0].setAnnotationValue(reinterpret_cast<void *>(
      static_cast<uintptr_t>((Action << 16) | (Kind & 0xFFFF))));
  PP.EnterTokenStream(std::move(TokenArray), 1,
                      /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
}

// #pragma clang optimize off
// #pragma clang optimize on
void PragmaOptimizeHandler::HandlePragma(Preprocessor &PP,
                                         PragmaIntroducer Introducer,
                                         Token &FirstToken) {
  Token Tok;
  PP.Lex(Tok);
  if (Tok.is(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_missing_argument)
        << "clang optimize" << /*Expected=*/true << "'on' or 'off'";
    return;
  }
  if (Tok.isNot(tok::identifier)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_invalid_argument)
      << PP.getSpelling(Tok);
    return;
  }
  const IdentifierInfo *II = Tok.getIdentifierInfo();
  // The only accepted values are 'on' or 'off'.
  bool IsOn = false;
  if (II->isStr("on")) {
    IsOn = true;
  } else if (!II->isStr("off")) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_invalid_argument)
      << PP.getSpelling(Tok);
    return;
  }
  PP.Lex(Tok);

  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::err_pragma_optimize_extra_argument)
      << PP.getSpelling(Tok);
    return;
  }

  Actions.ActOnPragmaOptimize(IsOn, FirstToken.getLocation());
}

void Parser::HandlePragmaFP() {
  assert(Tok.is(tok::annot_pragma_fp));
  auto *AnnotValue =
      reinterpret_cast<TokFPAnnotValue *>(Tok.getAnnotationValue());

  if (AnnotValue->ReassociateValue)
    Actions.ActOnPragmaFPValueChangingOption(
        Tok.getLocation(), PFK_Reassociate,
        *AnnotValue->ReassociateValue == LangOptions::FPModeKind::FPM_On);

  if (AnnotValue->ReciprocalValue)
    Actions.ActOnPragmaFPValueChangingOption(
        Tok.getLocation(), PFK_Reciprocal,
        *AnnotValue->ReciprocalValue == LangOptions::FPModeKind::FPM_On);

  if (AnnotValue->ContractValue)
    Actions.ActOnPragmaFPContract(Tok.getLocation(),
                                  *AnnotValue->ContractValue);
  if (AnnotValue->ExceptionsValue)
    Actions.ActOnPragmaFPExceptions(Tok.getLocation(),
                                    *AnnotValue->ExceptionsValue);
  if (AnnotValue->EvalMethodValue)
    Actions.ActOnPragmaFPEvalMethod(Tok.getLocation(),
                                    *AnnotValue->EvalMethodValue);
  ConsumeAnnotationToken();
}


bool Parser::HandlePragmaMSFunction(StringRef PragmaName,
                                    SourceLocation PragmaLocation) {
  Token FirstTok = Tok;

  if (ExpectAndConsume(tok::l_paren, diag::warn_pragma_expected_lparen,
                       PragmaName))
    return false;

  bool SuggestIntrinH = !PP.isMacroDefined("__INTRIN_H");

  llvm::SmallVector<StringRef> NoBuiltins;
  while (Tok.is(tok::identifier)) {
    IdentifierInfo *II = Tok.getIdentifierInfo();
    if (!II->getBuiltinID())
      PP.Diag(Tok.getLocation(), diag::warn_pragma_intrinsic_builtin)
          << II << SuggestIntrinH;
    else
      NoBuiltins.emplace_back(II->getName());

    PP.Lex(Tok);
    if (Tok.isNot(tok::comma))
      break;
    PP.Lex(Tok); // ,
  }

  if (ExpectAndConsume(tok::r_paren, diag::warn_pragma_expected_rparen,
                       PragmaName) ||
      ExpectAndConsume(tok::eof, diag::warn_pragma_extra_tokens_at_eol,
                       PragmaName))
    return false;

  Actions.ActOnPragmaMSFunction(FirstTok.getLocation(), NoBuiltins);
  return true;
}

// #pragma optimize("gsty", on|off)
bool Parser::HandlePragmaMSOptimize(StringRef PragmaName,
                                    SourceLocation PragmaLocation) {
  Token FirstTok = Tok;
  if (ExpectAndConsume(tok::l_paren, diag::warn_pragma_expected_lparen,
                       PragmaName))
    return false;

  if (Tok.isNot(tok::string_literal)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_string) << PragmaName;
    return false;
  }
  ExprResult StringResult = ParseStringLiteralExpression();
  if (StringResult.isInvalid())
    return false; // Already diagnosed.
  StringLiteral *OptimizationList = cast<StringLiteral>(StringResult.get());
  if (OptimizationList->getCharByteWidth() != 1) {
    PP.Diag(PragmaLocation, diag::warn_pragma_expected_non_wide_string)
        << PragmaName;
    return false;
  }

  if (ExpectAndConsume(tok::comma, diag::warn_pragma_expected_comma,
                       PragmaName))
    return false;

  if (Tok.is(tok::eof) || Tok.is(tok::r_paren)) {
    PP.Diag(PragmaLocation, diag::warn_pragma_missing_argument)
        << PragmaName << /*Expected=*/true << "'on' or 'off'";
    return false;
  }
  IdentifierInfo *II = Tok.getIdentifierInfo();
  if (!II || (!II->isStr("on") && !II->isStr("off"))) {
    PP.Diag(PragmaLocation, diag::warn_pragma_invalid_argument)
        << PP.getSpelling(Tok) << PragmaName << /*Expected=*/true
        << "'on' or 'off'";
    return false;
  }
  bool IsOn = II->isStr("on");
  PP.Lex(Tok);

  if (ExpectAndConsume(tok::r_paren, diag::warn_pragma_expected_rparen,
                       PragmaName))
    return false;

  // TODO: Add support for "sgty"
  if (!OptimizationList->getString().empty()) {
    PP.Diag(PragmaLocation, diag::warn_pragma_invalid_argument)
        << OptimizationList->getString() << PragmaName << /*Expected=*/true
        << "\"\"";
    return false;
  }

  if (ExpectAndConsume(tok::eof, diag::warn_pragma_extra_tokens_at_eol,
                       PragmaName))
    return false;

  Actions.ActOnPragmaMSOptimize(FirstTok.getLocation(), IsOn);
  return true;
}

void PragmaForceCUDAHostDeviceHandler::HandlePragma(
    Preprocessor &PP, PragmaIntroducer Introducer, Token &Tok) {
  Token FirstTok = Tok;

  PP.Lex(Tok);
  IdentifierInfo *Info = Tok.getIdentifierInfo();
  if (!Info || (!Info->isStr("begin") && !Info->isStr("end"))) {
    PP.Diag(FirstTok.getLocation(),
            diag::warn_pragma_force_cuda_host_device_bad_arg);
    return;
  }

  if (Info->isStr("begin"))
    Actions.CUDA().PushForceHostDevice();
  else if (!Actions.CUDA().PopForceHostDevice())
    PP.Diag(FirstTok.getLocation(),
            diag::err_pragma_cannot_end_force_cuda_host_device);

  PP.Lex(Tok);
  if (!Tok.is(tok::eod))
    PP.Diag(FirstTok.getLocation(),
            diag::warn_pragma_force_cuda_host_device_bad_arg);
}

/// Handle the #pragma clang attribute directive.
///
/// The syntax is:
/// \code
///  #pragma clang attribute push (attribute, subject-set)
///  #pragma clang attribute push
///  #pragma clang attribute (attribute, subject-set)
///  #pragma clang attribute pop
/// \endcode
///
/// There are also 'namespace' variants of push and pop directives. The bare
/// '#pragma clang attribute (attribute, subject-set)' version doesn't require a
/// namespace, since it always applies attributes to the most recently pushed
/// group, regardless of namespace.
/// \code
///  #pragma clang attribute namespace.push (attribute, subject-set)
///  #pragma clang attribute namespace.push
///  #pragma clang attribute namespace.pop
/// \endcode
///
/// The subject-set clause defines the set of declarations which receive the
/// attribute. Its exact syntax is described in the LanguageExtensions document
/// in Clang's documentation.
///
/// This directive instructs the compiler to begin/finish applying the specified
/// attribute to the set of attribute-specific declarations in the active range
/// of the pragma.
void PragmaAttributeHandler::HandlePragma(Preprocessor &PP,
                                          PragmaIntroducer Introducer,
                                          Token &FirstToken) {
  Token Tok;
  PP.Lex(Tok);
  auto *Info = new (PP.getPreprocessorAllocator())
      PragmaAttributeInfo(AttributesForPragmaAttribute);

  // Parse the optional namespace followed by a period.
  if (Tok.is(tok::identifier)) {
    IdentifierInfo *II = Tok.getIdentifierInfo();
    if (!II->isStr("push") && !II->isStr("pop")) {
      Info->Namespace = II;
      PP.Lex(Tok);

      if (!Tok.is(tok::period)) {
        PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_expected_period)
            << II;
        return;
      }
      PP.Lex(Tok);
    }
  }

  if (!Tok.isOneOf(tok::identifier, tok::l_paren)) {
    PP.Diag(Tok.getLocation(),
            diag::err_pragma_attribute_expected_push_pop_paren);
    return;
  }

  // Determine what action this pragma clang attribute represents.
  if (Tok.is(tok::l_paren)) {
    if (Info->Namespace) {
      PP.Diag(Tok.getLocation(),
              diag::err_pragma_attribute_namespace_on_attribute);
      PP.Diag(Tok.getLocation(),
              diag::note_pragma_attribute_namespace_on_attribute);
      return;
    }
    Info->Action = PragmaAttributeInfo::Attribute;
  } else {
    const IdentifierInfo *II = Tok.getIdentifierInfo();
    if (II->isStr("push"))
      Info->Action = PragmaAttributeInfo::Push;
    else if (II->isStr("pop"))
      Info->Action = PragmaAttributeInfo::Pop;
    else {
      PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_invalid_argument)
          << PP.getSpelling(Tok);
      return;
    }

    PP.Lex(Tok);
  }

  // Parse the actual attribute.
  if ((Info->Action == PragmaAttributeInfo::Push && Tok.isNot(tok::eod)) ||
      Info->Action == PragmaAttributeInfo::Attribute) {
    if (Tok.isNot(tok::l_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::l_paren;
      return;
    }
    PP.Lex(Tok);

    // Lex the attribute tokens.
    SmallVector<Token, 16> AttributeTokens;
    int OpenParens = 1;
    while (Tok.isNot(tok::eod)) {
      if (Tok.is(tok::l_paren))
        OpenParens++;
      else if (Tok.is(tok::r_paren)) {
        OpenParens--;
        if (OpenParens == 0)
          break;
      }

      AttributeTokens.push_back(Tok);
      PP.Lex(Tok);
    }

    if (AttributeTokens.empty()) {
      PP.Diag(Tok.getLocation(), diag::err_pragma_attribute_expected_attribute);
      return;
    }
    if (Tok.isNot(tok::r_paren)) {
      PP.Diag(Tok.getLocation(), diag::err_expected) << tok::r_paren;
      return;
    }
    SourceLocation EndLoc = Tok.getLocation();
    PP.Lex(Tok);

    // Terminate the attribute for parsing.
    Token EOFTok;
    EOFTok.startToken();
    EOFTok.setKind(tok::eof);
    EOFTok.setLocation(EndLoc);
    AttributeTokens.push_back(EOFTok);

    markAsReinjectedForRelexing(AttributeTokens);
    Info->Tokens =
        llvm::ArrayRef(AttributeTokens).copy(PP.getPreprocessorAllocator());
  }

  if (Tok.isNot(tok::eod))
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "clang attribute";

  // Generate the annotated pragma token.
  auto TokenArray = std::make_unique<Token[]>(1);
  TokenArray[0].startToken();
  TokenArray[0].setKind(tok::annot_pragma_attribute);
  TokenArray[0].setLocation(FirstToken.getLocation());
  TokenArray[0].setAnnotationEndLoc(FirstToken.getLocation());
  TokenArray[0].setAnnotationValue(static_cast<void *>(Info));
  PP.EnterTokenStream(std::move(TokenArray), 1,
                      /*DisableMacroExpansion=*/false, /*IsReinject=*/false);
}

// Handle '#pragma clang riscv intrinsic vector'.
//        '#pragma clang riscv intrinsic sifive_vector'.
void PragmaRISCVHandler::HandlePragma(Preprocessor &PP,
                                      PragmaIntroducer Introducer,
                                      Token &FirstToken) {
  Token Tok;
  PP.Lex(Tok);
  IdentifierInfo *II = Tok.getIdentifierInfo();

  if (!II || !II->isStr("intrinsic")) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_argument)
        << PP.getSpelling(Tok) << "riscv" << /*Expected=*/true << "'intrinsic'";
    return;
  }

  PP.Lex(Tok);
  II = Tok.getIdentifierInfo();
  if (!II || !(II->isStr("vector") || II->isStr("sifive_vector"))) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_invalid_argument)
        << PP.getSpelling(Tok) << "riscv" << /*Expected=*/true
        << "'vector' or 'sifive_vector'";
    return;
  }

  PP.Lex(Tok);
  if (Tok.isNot(tok::eod)) {
    PP.Diag(Tok.getLocation(), diag::warn_pragma_extra_tokens_at_eol)
        << "clang riscv intrinsic";
    return;
  }

  if (II->isStr("vector"))
    Actions.RISCV().DeclareRVVBuiltins = true;
  else if (II->isStr("sifive_vector"))
    Actions.RISCV().DeclareSiFiveVectorBuiltins = true;
}
