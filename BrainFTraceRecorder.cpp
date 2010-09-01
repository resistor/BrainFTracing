//===-- BrainFTraceRecorder.cpp - BrainF trace recorder ------------------==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
// 
// This class observes the execution trace of the interpreter, identifying
// hot traces and eventually compiling them to native code.
//
// The operation of the recorder can be divided into four parts:
//   1) Interation Counting - To identify hot traces, we track the execution
//      counts of all loop headers ('[' instructions).  We use a fixed-size
//      array of counters for this, since lack of precision does not affect
//      correctness.
//
//   2) Trace Buffering - Once a header has passed a hotness threshold, we 
//      begin buffering the execution trace beginning from that header the
//      next time it is executed.  This buffer is of a fixed length, though
//      that choice can be tuned for performance.  If the end of the buffer
//      is reached without execution returning to the header, we throw out
//      the trace.
//
//   3) Trace Commit - If the buffered trace returns to the header before 
//      the buffer limit is reached, that trace is commited to form a trace
//      tree.  This tree aggregates all execution traces that have been 
//      observed originating from the header since it passed the hotness
//      threshold.  The buffer is then cleared to allow a new trace to be
//      recorded.
//
//   4) Trace Compilation - Once a secondary hotness threshold is reached,
//      trace recording is terminated and the set of observed traces encoded
//      in the trace tree are compiled to native code, and a function pointer
//      to that trace is installed into the bytecode array in place of one of
//      the normal opcode functions.  Details of this compilation are in
//      BrainFCodeGen.cpp
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "BrainFVM.h"
#include "llvm/Support/raw_ostream.h"

#define ITERATION_BUF_SIZE  1024
#define TRACE_BUF_SIZE       128
#define TRACE_THRESHOLD      100
#define BACKEDGE_THRESHOLD     5

void BrainFTraceRecorder::BrainFTraceNode::dump(unsigned lvl) {
  for (unsigned i = 0; i < lvl; ++i)
    outs() << '.';
  outs() << opcode << " : " << pc << "\n";
  if (left && left != (BrainFTraceNode*)~0ULL) left->dump(lvl+1);
  if (right && right != (BrainFTraceNode*)~0ULL) right->dump(lvl+1);
}

BrainFTraceRecorder::BrainFTraceRecorder()
  : mode(MODE_PROFILING), iteration_count(new uint8_t[ITERATION_BUF_SIZE]),
    trace_begin(new std::pair<uint8_t, size_t>[TRACE_BUF_SIZE]),
    trace_end(trace_begin + TRACE_BUF_SIZE),
    trace_tail(trace_begin),
    module(new Module("BrainF", getGlobalContext())) {
  memset(iteration_count, 0, ITERATION_BUF_SIZE);
  memset(trace_begin, 0, sizeof(std::pair<uint8_t, size_t>) * TRACE_BUF_SIZE);
  
  initialize_module();
}

BrainFTraceRecorder::~BrainFTraceRecorder() {
  delete[] iteration_count;
  delete[] trace_begin;
  delete FPM;
  delete EE;
}

void BrainFTraceRecorder::commit() {
  BrainFTraceNode *&Head = trace_map[trace_begin->second];
  if (!Head)
    Head = new BrainFTraceNode(trace_begin->first, trace_begin->second, 0);
  
  BrainFTraceNode *Parent = Head;
  std::pair<uint8_t, size_t> *trace_iter = trace_begin+1;
  unsigned depth = 0;
  while (trace_iter != trace_tail) {
    ++depth;
    BrainFTraceNode *Child = 0;
    
    if (trace_iter->second == Parent->pc+1) {
      if (Parent->left) Child = Parent->left;
      else Child = Parent->left =
        new BrainFTraceNode(trace_iter->first, trace_iter->second, depth);
    } else {
      if (Parent->right) Child = Parent->right;
      else Child = Parent->right =
        new BrainFTraceNode(trace_iter->first, trace_iter->second, depth);
    }
    
    Parent = Child;
    ++trace_iter;
  }
  
  if (Parent->pc+1 == Head->pc)
    Parent->left = (BrainFTraceNode*)~0ULL;
  else
    Parent->right = (BrainFTraceNode*)~0ULL;
}

void BrainFTraceRecorder::commit_extension() {
  BrainFTraceNode *Parent = extension_leaf;
  unsigned depth = extension_leaf->depth;
  std::pair<uint8_t, size_t> *trace_iter = trace_begin;
  while (trace_iter != trace_tail) {
    ++depth;
    BrainFTraceNode *Child = 0;
    
    if (trace_iter->second == Parent->pc+1) {
      if (Parent->left) Child = Parent->left;
      else Child = Parent->left =
        new BrainFTraceNode(trace_iter->first, trace_iter->second, depth);
    } else {
      if (Parent->right) Child = Parent->right;
      else Child = Parent->right =
        new BrainFTraceNode(trace_iter->first, trace_iter->second, depth);
    }
    
    Parent = Child;
    ++trace_iter;
  }
  
  if (Parent->pc+1 == extension_root->pc)
    Parent->left = (BrainFTraceNode*)~0ULL;
  else
    Parent->right = (BrainFTraceNode*)~0ULL;
}

void
BrainFTraceRecorder::record_simple(size_t pc, uint8_t opcode, size_t next_pc) {
  if (mode == MODE_RECORDING) {
    if (opcode == ']' && next_pc != trace_begin->second) {
      ++backedge_count;
      if (backedge_count > BACKEDGE_THRESHOLD) {
        backedge_count = 0;
        mode = MODE_PROFILING;
        return;
      }
    }
    
    if (trace_tail == trace_end) {
      mode = MODE_PROFILING;
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
      
      if (next_pc == trace_begin->second) {
        commit();
        compile(trace_map[next_pc]);
        mode = MODE_PROFILING;
      }
    }
  } else if (mode == MODE_EXTENSION_BEGIN) {
    if (blacklist.count(pc)) {
      mode = MODE_PROFILING;
    } else {
      trace_tail = trace_begin;
      backedge_count = 0;
      mode = MODE_EXTENSION;
      record_simple(pc, opcode, next_pc);
    }
  } else if (mode == MODE_EXTENSION) {
    if (opcode == ']' && next_pc != extension_root->pc) {
      ++backedge_count;
      if (backedge_count > BACKEDGE_THRESHOLD) {
        blacklist.insert(trace_begin->second);
        backedge_count = 0;
        mode = MODE_PROFILING;
        return;
      }
    }
    
    if (trace_tail + extension_leaf->depth >= trace_end) {
      mode = MODE_PROFILING;
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
      
      if (next_pc == extension_root->pc) {
        commit_extension();
        compile(extension_root);
        mode = MODE_PROFILING;
      }
    }
  }
}

void BrainFTraceRecorder::record(size_t pc, uint8_t opcode, size_t next_pc) {
  if (mode == MODE_RECORDING) {
    if (trace_tail == trace_end) {
      mode = MODE_PROFILING;
      record(pc, opcode, next_pc);
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
      
      if (next_pc == trace_begin->second) {
        commit();
        compile(trace_map[next_pc]);
        mode = MODE_PROFILING;
      }
    }
  } else if (mode == MODE_PROFILING){
    size_t hash = pc % ITERATION_BUF_SIZE;
    if (iteration_count[hash] == 255) iteration_count[hash] = 254;
    if (++iteration_count[hash] > TRACE_THRESHOLD) {
      trace_begin->first = opcode;
      trace_begin->second = pc;
      trace_tail = trace_begin+1;
      backedge_count = 0;
      mode = MODE_RECORDING;
    }
  } else if (mode == MODE_EXTENSION_BEGIN) {
    trace_tail = trace_begin;
    mode = MODE_EXTENSION;
    record(pc, opcode, next_pc);
  } else if (mode == MODE_EXTENSION) {
    if (trace_tail + extension_leaf->depth >= trace_end) {
      mode = MODE_PROFILING;
      record(pc, opcode, next_pc);
    } else {
      trace_tail->first = opcode;
      trace_tail->second = pc;
      ++trace_tail;
      
      if (next_pc == extension_root->pc) {
        commit_extension();
        compile(extension_root);
        mode = MODE_PROFILING;
      }
    }
  }
}