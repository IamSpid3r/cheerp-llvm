//===-- CheerpBackend.cpp - Backend wrapper for CheerpWriter---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// Copyright 2011-2015 Leaning Technologies
//
//===----------------------------------------------------------------------===//

#include "CheerpTargetMachine.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/PassManager.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Type.h"
#include "llvm/Cheerp/Writer.h"
#include "llvm/Cheerp/AllocaMerging.h"
#include "llvm/Cheerp/PointerPasses.h"
#include "llvm/Cheerp/Registerize.h"
#include "llvm/Cheerp/ResolveAliases.h"
#include "llvm/Cheerp/SourceMaps.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<std::string> SourceMap("cheerp-sourcemap", cl::Optional,
  cl::desc("If specified, the file name of the source map"), cl::value_desc("filename"));

static cl::opt<std::string> SourceMapPrefix("cheerp-sourcemap-prefix", cl::Optional,
  cl::desc("If specified, this prefix will be removed from source map file paths"), cl::value_desc("path"));

static cl::opt<bool> PrettyCode("cheerp-pretty-code", cl::desc("Generate human-readable JS") );

static cl::opt<bool> MakeModule("cheerp-make-module", cl::desc("Create a closure around JS to avoid global namespace pollution") );

static cl::opt<bool> NoRegisterize("cheerp-no-registerize", cl::desc("Disable registerize pass") );

static cl::opt<bool> NoNativeJavaScriptMath("cheerp-no-native-math", cl::desc("Disable native JavaScript math functions") );

static cl::opt<bool> NoJavaScriptMathImul("cheerp-no-math-imul", cl::desc("Disable JavaScript Math.imul") );

static cl::opt<bool> NoCredits("cheerp-no-credits", cl::desc("Disable Cheerp credits in JS") );

static cl::opt<bool> MeasureTimeToMain("cheerp-measure-time-to-main", cl::desc("Print time elapsed until the first line of main() is executed") );

static cl::list<std::string> ReservedNames("cheerp-reserved-names", cl::value_desc("list"), cl::desc("A list of JS identifiers that should not be used by Cheerp"), cl::CommaSeparated);

extern "C" void LLVMInitializeCheerpBackendTarget() {
  // Register the target.
  RegisterTargetMachine<CheerpTargetMachine> X(TheCheerpBackendTarget);
}

namespace {
  class CheerpWritePass : public ModulePass {
  private:
    formatted_raw_ostream &Out;
    static char ID;
    void getAnalysisUsage(AnalysisUsage& AU) const;
  public:
    explicit CheerpWritePass(formatted_raw_ostream &o) :
      ModulePass(ID), Out(o) { }
    bool runOnModule(Module &M);
    const char *getPassName() const {
	return "CheerpWritePass";
    }
  };
} // end anonymous namespace.

bool CheerpWritePass::runOnModule(Module& M)
{
  cheerp::PointerAnalyzer &PA = getAnalysis<cheerp::PointerAnalyzer>();
  cheerp::GlobalDepsAnalyzer &GDA = getAnalysis<cheerp::GlobalDepsAnalyzer>();
  cheerp::Registerize &registerize = getAnalysis<cheerp::Registerize>();
  cheerp::SourceMapGenerator* sourceMapGenerator = NULL;
  if (!SourceMap.empty())
  {
    std::error_code ErrorCode;
    sourceMapGenerator = new cheerp::SourceMapGenerator(SourceMap, SourceMapPrefix, M.getContext(), ErrorCode);
    if (ErrorCode)
    {
       // An error occurred opening the source map file, bail out
       delete sourceMapGenerator;
       llvm::report_fatal_error(ErrorCode.message(), false);
       return false;
    }
  }
  PA.fullResolve();
  PA.computeConstantOffsets(M);
  registerize.assignRegisters(M, PA);
  // Build the ordered list of reserved names
  std::vector<std::string> reservedNames(ReservedNames.begin(), ReservedNames.end());
  std::sort(reservedNames.begin(), reservedNames.end());
  cheerp::CheerpWriter writer(M, Out, PA, registerize, GDA, sourceMapGenerator, reservedNames,
          PrettyCode, MakeModule, NoRegisterize, !NoNativeJavaScriptMath,
          !NoJavaScriptMathImul, !NoCredits, MeasureTimeToMain);
  writer.makeJS();
  delete sourceMapGenerator;
  return false;
}

void CheerpWritePass::getAnalysisUsage(AnalysisUsage& AU) const
{
  AU.addRequired<cheerp::GlobalDepsAnalyzer>();
  AU.addRequired<cheerp::PointerAnalyzer>();
  AU.addRequired<cheerp::Registerize>();
}

char CheerpWritePass::ID = 0;

//===----------------------------------------------------------------------===//
//                       External Interface declaration
//===----------------------------------------------------------------------===//

bool CheerpTargetMachine::addPassesToEmitFile(PassManagerBase &PM,
                                           formatted_raw_ostream &o,
                                           CodeGenFileType FileType,
                                           bool DisableVerify,
                                           AnalysisID StartAfter,
                                           AnalysisID StopAfter) {
  if (FileType != TargetMachine::CGFT_AssemblyFile) return true;
  PM.add(createResolveAliasesPass());
  PM.add(createFreeAndDeleteRemovalPass());
  PM.add(cheerp::createGlobalDepsAnalyzerPass());
  PM.add(createPointerArithmeticToArrayIndexingPass());
  PM.add(createPointerToImmutablePHIRemovalPass());
  PM.add(cheerp::createRegisterizePass(NoRegisterize));
  PM.add(cheerp::createPointerAnalyzerPass());
  PM.add(cheerp::createAllocaMergingPass());
  PM.add(createIndirectCallOptimizerPass());
  PM.add(createAllocaArraysPass());
  PM.add(cheerp::createAllocaArraysMergingPass());
  PM.add(createDelayAllocasPass());
  PM.add(new CheerpWritePass(o));
  return false;
}
