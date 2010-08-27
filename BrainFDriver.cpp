//===-- BrainFDriver.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// This program converts the BrainF language into LLVM assembly,
// which it can then run using the JIT or output as BitCode.
//
// This implementation has a tape of 65536 bytes,
// with the head starting in the middle.
// Range checking is off by default, so be careful.
// It can be enabled with -abc.
//
// Use:
// ./BrainF -jit      prog.bf          #Run program now
// ./BrainF -jit -abc prog.bf          #Run program now safely
// ./BrainF           prog.bf          #Write as BitCode
//
// lli prog.bf.bc                      #Run generated BitCode
// llvm-ld -native -o=prog prog.bf.bc  #Compile BitCode into native executable
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
using namespace llvm;

//Command line options

static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input brainf>"));

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, " BrainF compiler\n");

  if (InputFilename == "") {
    errs() << "Error: You must specify the filename of the program to "
    "be compiled.  Use --help to see the options.\n";
    abort();
  }

  //Get the input stream
  MemoryBuffer *Code = MemoryBuffer::getFileOrSTDIN(InputFilename);
  
  // Setup the array
  uint8_t *BrainFArray = new uint8_t[32768];
  memset(BrainFArray, 0, 32768);
  
  // Main interpreter loop
  size_t pc = 0;
  uint8_t* data = BrainFArray;
  const uint8_t *CodeBegin = (const uint8_t*)(Code->getBufferStart());
  BrainFTraceRecorder tracer;
  
  uint8_t opcode = CodeBegin[pc];
  while (CodeBegin[pc] != '\0') {
    switch (opcode) {
      default:
        break;
      case '>':
        tracer.record_simple(pc, opcode);
        ++data;
        break;
      case '<':
        tracer.record_simple(pc, opcode);
        --data;
        break;
      case '+':
        tracer.record_simple(pc, opcode);
        *data += 1;
        break;
      case '-':
        tracer.record_simple(pc, opcode);
        *data -= 1;
        break;
      case '.':
        tracer.record_simple(pc, opcode);
        putchar(*data);
        break;
      case ',':
        tracer.record_simple(pc, opcode);
        *data = getchar();
        break;
      case '[':
        if (tracer.record(pc, opcode, &data)) continue;
        if (!*data) {
          unsigned count = 1;
          opcode = CodeBegin[++pc];
          while (count > 0) {
            if (opcode == '[')
              ++count;
            else if (opcode == ']')
              --count;
            opcode = CodeBegin[++pc];
          }
          continue;
        }
        break;
      case ']':
        tracer.record_simple(pc, CodeBegin[pc]);
        unsigned count = 1;
        opcode = CodeBegin[--pc];
        while (count > 0) {
          if (CodeBegin[pc] == '[')
            --count;
          else if (CodeBegin[pc] == ']')
            ++count;
          opcode = CodeBegin[--pc];
        }
        opcode = CodeBegin[++pc];
        continue;
    }
    opcode = CodeBegin[++pc];
  }

  //Clean up
  delete Code;
  delete[] BrainFArray;

  return 0;
}
