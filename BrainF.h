//===-- BrainF.h - BrainF compiler class ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//

#ifndef BRAINF_H
#define BRAINF_H

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/PassManager.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"


using namespace llvm;

class BrainFTraceRecorder {
  struct BrainFTraceNode {
    uint8_t opcode;
    size_t pc;
    size_t depth;
    BrainFTraceNode(uint8_t o, size_t p, size_t d)
      : opcode(o), pc(p), depth(d), left(0), right(0)  { }
    void dump(unsigned level);
    
    // On an if, left is the x != 0 edge.
    // A value of 0 indicates an un-traced edge.
    // A value of ~0ULL indicates an edge to the trace head.
    BrainFTraceNode *left, *right;
  };
  
  static const uint8_t MODE_PROFILING = 0;
  static const uint8_t MODE_RECORDING = 1;
  static const uint8_t MODE_EXTENSION_BEGIN = 2;
  static const uint8_t MODE_EXTENSION = 3;

  size_t backedge_count;

  uint8_t mode;
  BrainFTraceNode *extension_root, *extension_leaf;
  
  uint8_t *iteration_count;
  std::pair<uint8_t, size_t> *trace_begin, *trace_end, *trace_tail;
  DenseMap<size_t, BrainFTraceNode*> trace_map;
  DenseSet<size_t> blacklist;
  Module *module;
  BasicBlock *Header;
  Value *DataPtr;
  PHINode *HeaderPHI;
  ExecutionEngine *EE;

  const IntegerType *int_type;
  const FunctionType *op_type;
  GlobalValue *bytecode_array, *mode_flag, *ext_root, *ext_leaf;
  Value *getchar_func, *putchar_func;
  FunctionPassManager *FPM;
  
  
  void commit();
  void commit_extension();
  void initialize_module();
  void compile(BrainFTraceNode* trace);
  void compile_opcode(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_plus(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_minus(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_left(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_right(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_put(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_get(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_if(BrainFTraceNode *node, IRBuilder<>& builder);
  void compile_back(BrainFTraceNode *node, IRBuilder<>& builder);  
  void compile_set_zero(BrainFTraceNode *node, IRBuilder<>& builder);                                        
  
public:
  BrainFTraceRecorder();
  ~BrainFTraceRecorder();
  
  void record(size_t pc, uint8_t opcode, size_t next_pc);
  void record_simple(size_t pc, uint8_t opcode, size_t next_pc);
};

#endif
