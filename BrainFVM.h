//===-- BrainF.h - BrainF compiler class ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// This class stores the data for the BrainF compiler so it doesn't have
// to pass all of it around.  The main method is parse.
//
//===--------------------------------------------------------------------===//

#ifndef BRAINF_VM_H
#define BRAINF_VM_H

#include "stdint.h"
#include <cstring>

typedef void(*opcode_func_t)(size_t pc, uint8_t* data);
opcode_func_t *BytecodeArray;

size_t *JumpMap;

void op_plus(size_t, uint8_t*);
void op_minus(size_t, uint8_t*);
void op_left(size_t, uint8_t*);
void op_right(size_t, uint8_t*);
void op_put(size_t, uint8_t*);
void op_get(size_t, uint8_t*);
void op_if(size_t, uint8_t*);
void op_back(size_t, uint8_t*);


#endif