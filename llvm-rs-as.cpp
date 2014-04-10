//===--- llvm-as.cpp - The low-level LLVM assembler -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This utility may be invoked in the following manner:
//   llvm-as --help         - Output information about command line switches
//   llvm-as [options]      - Read LLVM asm from stdin, write bitcode to stdout
//   llvm-as [options] x.ll - Read LLVM asm from the x.ll file, write bitcode
//                            to the x.bc file.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/OwningPtr.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"

#include "BitWriter_3_2/ReaderWriter_3_2.h"
#include "BitWriter_2_9/ReaderWriter_2_9.h"
#include "BitWriter_2_9_func/ReaderWriter_2_9_func.h"

#include <memory>
using namespace llvm;

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input .llvm file>"), cl::init("-"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
DisableOutput("disable-output", cl::desc("Disable output"), cl::init(false));

static cl::opt<bool>
DumpAsm("d", cl::desc("Print assembly as parsed"), cl::Hidden);

static cl::opt<bool>
DisableVerify("disable-verify", cl::Hidden,
              cl::desc("Do not run verifier on input LLVM (dangerous!)"));

enum BCVersion {
  BC29, BC29Func, BC32, BCHEAD
};

cl::opt<BCVersion> BitcodeVersion("bitcode-version",
  cl::desc("Set the bitcode version to be written:"),
  cl::values(
    clEnumValN(BC29, "BC29", "Version 2.9"),
     clEnumVal(BC29Func,     "Version 2.9 func"),
     clEnumVal(BC32,         "Version 3.2"),
     clEnumVal(BCHEAD,       "Most current version"),
    clEnumValEnd), cl::init(BC32));

static void WriteOutputFile(const Module *M) {
  // Infer the output filename if needed.
  if (OutputFilename.empty()) {
    if (InputFilename == "-") {
      OutputFilename = "-";
    } else {
      std::string IFN = InputFilename;
      int Len = IFN.length();
      if (IFN[Len-3] == '.' && IFN[Len-2] == 'l' && IFN[Len-1] == 'l') {
        // Source ends in .ll
        OutputFilename = std::string(IFN.begin(), IFN.end()-3);
      } else {
        OutputFilename = IFN;   // Append a .bc to it
      }
      OutputFilename += ".bc";
    }
  }

  std::string ErrorInfo;
  OwningPtr<tool_output_file> Out
  (new tool_output_file(OutputFilename.c_str(), ErrorInfo,
                        llvm::sys::fs::F_None));
  if (!ErrorInfo.empty()) {
    errs() << ErrorInfo << '\n';
    exit(1);
  }

  if (Force || !CheckBitcodeOutputToConsole(Out->os(), true)) {
    switch(BitcodeVersion) {
      case BC29:
        llvm_2_9::WriteBitcodeToFile(M, Out->os());
        break;
      case BC29Func:
        llvm_2_9_func::WriteBitcodeToFile(M, Out->os());
        break;
      case BC32:
        llvm_3_2::WriteBitcodeToFile(M, Out->os());
        break;
      case BCHEAD:
        llvm::WriteBitcodeToFile(M, Out->os());
        break;
    }
  }

  // Declare success.
  Out->keep();
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);
  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm .ll -> .bc assembler\n");

  // Parse the file now...
  SMDiagnostic Err;
  OwningPtr<Module> M(ParseAssemblyFile(InputFilename, Err, Context));
  if (M.get() == 0) {
    Err.print(argv[0], errs());
    return 1;
  }

  if (!DisableVerify) {
    std::string Err;
    raw_string_ostream stream(Err);
    if (verifyModule(*M.get(), &stream)) {
      errs() << argv[0]
             << ": assembly parsed, but does not verify as correct!\n";
      errs() << Err;
      return 1;
    }
  }

  if (DumpAsm) errs() << "Here's the assembly:\n" << *M.get();

  if (!DisableOutput)
    WriteOutputFile(M.get());

  return 0;
}
