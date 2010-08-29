//===-- BrainFDriver.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//

#include "BrainFVM.h"
#include <cstdio>

void op_plus(size_t pc, uint8_t *data) {
  *data += 1;
  BytecodeArray[pc+1](pc+1, data);
}

void op_minus(size_t pc, uint8_t *data) {
  *data -= 1;
  BytecodeArray[pc+1](pc+1, data);
}

void op_left(size_t pc, uint8_t *data) {
  BytecodeArray[pc+1](pc+1, data-1);
}

void op_right(size_t pc, uint8_t *data) {
  BytecodeArray[pc+1](pc+1, data+1);
}

void op_put(size_t pc, uint8_t *data) {
  putchar(*data);
  BytecodeArray[pc+1](pc+1, data);
}

void op_get(size_t pc, uint8_t *data) {
  *data = getchar();
  BytecodeArray[pc+1](pc+1, data);
}

void op_if(size_t pc, uint8_t *data) {
  if (!*data) {
    size_t new_pc = JumpMap[pc];
    BytecodeArray[new_pc](new_pc, data);
  } else {
    BytecodeArray[pc+1](pc+1, data);
  }
}

void op_back(size_t pc, uint8_t *data) {
  size_t new_pc = JumpMap[pc];
  BytecodeArray[new_pc](new_pc, data);
}