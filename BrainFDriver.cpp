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
#include "BrainFVM.h"
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
  MemoryBuffer *ParsedCode =
    MemoryBuffer::getNewMemBuffer(sizeof(opcode_func_t) * 
                                  (Code->getBufferSize()+1));
  const uint8_t *CodeBegin = (const uint8_t*)(Code->getBufferStart());
  BytecodeArray = (opcode_func_t*)(ParsedCode->getBufferStart());
  size_t BytecodeOffset = 0;
  
  JumpMap = new size_t[Code->getBufferSize()];
  memset(JumpMap, 0, sizeof(size_t) * Code->getBufferSize());
  std::vector<size_t> Stack;
  
  for (size_t i = 0; i < Code->getBufferSize(); ++i) {
    uint8_t opcode = CodeBegin[i];
    switch (opcode) {
      case '>':
        BytecodeArray[BytecodeOffset++] = &op_right;
        break;
      case '<':
        BytecodeArray[BytecodeOffset++] = &op_left;
        break;
      case '+':
        BytecodeArray[BytecodeOffset++] = &op_plus;
        break;
      case '-':
        BytecodeArray[BytecodeOffset++] = &op_minus;
        break;
      case '.':
        BytecodeArray[BytecodeOffset++] = &op_put;
        break;
      case ',':
        BytecodeArray[BytecodeOffset++] = &op_get;
        break;
      case '[':
        Stack.push_back(BytecodeOffset);
        BytecodeArray[BytecodeOffset++] = &op_if;
        break;
      case ']':
        JumpMap[Stack.back()] = BytecodeOffset;
        JumpMap[BytecodeOffset] = Stack.back();
        Stack.pop_back();
        BytecodeArray[BytecodeOffset++] = &op_back;
        break;
      default:
        continue;
    }
  }
  
  while (BytecodeOffset < Code->getBufferSize()+1) {
    BytecodeArray[BytecodeOffset++] = &op_end;
  }
  
  // Setup the array
  uint8_t *BrainFArray = new uint8_t[32768];
  memset(BrainFArray, 0, 32768);
  
  // Main interpreter loop
  uint8_t* data = BrainFArray;
  BrainFTraceRecorder tracer;
  
  BytecodeArray[0](0, data);
  
  //Clean up
  delete Code;
  delete ParsedCode;
  delete[] BrainFArray;
  delete[] JumpMap;

  return 0;
}
