//===-- BrainFDriver.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#define ITERATION_BUF_SIZE  1024
#define TRACE_BUF_SIZE       256
#define TRACE_THRESHOLD      50
#define COMPILE_THRESHOLD    200

void BrainFTraceRecorder::BrainFTraceNode::dump(unsigned lvl) {
  for (unsigned i = 0; i < lvl; ++i)
    outs() << '.';
  outs() << opcode << " : " << pc << "\n";
  if (left && left != (BrainFTraceNode*)~0ULL) left->dump(lvl+1);
  if (right && right != (BrainFTraceNode*)~0ULL) right->dump(lvl+1);
}

BrainFTraceRecorder::BrainFTraceRecorder()
  : prev_opcode('+'), iteration_count(new uint8_t[ITERATION_BUF_SIZE]),
    trace_begin(new std::pair<uint8_t, size_t>[TRACE_BUF_SIZE]),
    trace_end(trace_begin + TRACE_BUF_SIZE),
    trace_tail(trace_begin),
    module(new Module("BrainF", getGlobalContext())) {
  memset(iteration_count, 0, ITERATION_BUF_SIZE);
  memset(trace_begin, 0, sizeof(std::pair<uint8_t, size_t>) * TRACE_BUF_SIZE);
  InitializeNativeTarget();
  EE = EngineBuilder(module).create();
}

BrainFTraceRecorder::~BrainFTraceRecorder() {
#if 0
  for (DenseMap<size_t, BrainFTraceNode*>::iterator I = trace_map.begin(), 
       E = trace_map.end(); I != E; ++I) {
    outs() << "Recorded Trace:\n";
    I->second->dump(1);
    outs() << "\n";
  }
  
#endif
  
  //module->dump();
  delete[] iteration_count;
  delete[] trace_begin;
  delete EE;
}

void BrainFTraceRecorder::commit() {
#if 0
  outs() << "Committing trace: ";
  for (BrainFTraceNode *begin = trace_begin, *end = trace_tail;
       begin != end; ++begin) {
    outs() << begin->opcode;
  }
  
  outs() << "\n";
#endif

  BrainFTraceNode *&Head = trace_map[trace_begin->second];
  if (!Head)
    Head = new BrainFTraceNode(trace_begin->first, trace_begin->second);
  
  BrainFTraceNode *Parent = Head;
  std::pair<uint8_t, size_t> *trace_iter = trace_begin+1;
  while (trace_iter != trace_tail) {
    BrainFTraceNode *Child = 0;
    
    if (trace_iter->second == Parent->pc+1) {
      if (Parent->left) Child = Parent->left;
      else Child = Parent->left =
        new BrainFTraceNode(trace_iter->first, trace_iter->second);
    } else {
      if (Parent->right) Child = Parent->right;
      else Child = Parent->right =
        new BrainFTraceNode(trace_iter->first, trace_iter->second);
    }
    
    Parent = Child;
    ++trace_iter;
  }
  
  if (Parent->pc+1 == Head->pc)
    Parent->left = (BrainFTraceNode*)~0ULL;
  else
    Parent->right = (BrainFTraceNode*)~0ULL;
}

void BrainFTraceRecorder::record_simple(size_t pc, uint8_t opcode) {
  if (trace_tail != trace_begin) {
    if (trace_tail == trace_end) {
      trace_tail = trace_begin;
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
    }
  }
  prev_opcode = opcode;
}

bool BrainFTraceRecorder::record(size_t &pc, uint8_t opcode, uint8_t** data) {
  if (code_map.count(pc)) {
    size_t old_pc = pc;
    pc = code_map[pc](data);
    trace_tail = trace_begin;
    return old_pc != pc;
  }
  
  if (trace_tail != trace_begin) {
    if (pc == trace_begin->second) {
      commit();
      trace_tail = trace_begin;
    } else if (trace_tail == trace_end) {
      trace_tail = trace_begin;
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
    }
  } else if (opcode == '[' && prev_opcode == ']'){
    size_t hash = pc % ITERATION_BUF_SIZE;
    if (iteration_count[hash] == 255) iteration_count[hash] = 254;
    if (++iteration_count[hash] > COMPILE_THRESHOLD && trace_map.count(pc)) {
      compile(trace_map[pc]);
    } else if (++iteration_count[hash] > TRACE_THRESHOLD) {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
    }
  }
  
  prev_opcode = opcode;
  return false;
}