//===-- BrainFDriver.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/PassManager.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/StringExtras.h"

void BrainFTraceRecorder::compile(BrainFTraceNode* trace) {
  LLVMContext &Context = module->getContext();
  
  const Type *int_type = sizeof(int) == 4 ? IntegerType::getInt32Ty(Context)
                                       : IntegerType::getInt64Ty(Context);
  pchar = module->getOrInsertFunction("putchar", int_type, int_type, NULL);
  gchar = module->getOrInsertFunction("getchar", int_type, NULL);
  
  const Type *pc_type = sizeof(size_t) == 4 ? 
                  IntegerType::getInt32Ty(Context) : 
                  IntegerType::getInt64Ty(Context);
  const Type *data_type = PointerType::getUnqual(
    PointerType::getUnqual(IntegerType::getInt8Ty(Context)));
  Function *curr_func = cast<Function>(module->
    getOrInsertFunction("trace_"+utostr(trace->pc),
                        pc_type, data_type, NULL));
  
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", curr_func);
  Header = BasicBlock::Create(Context, utostr(trace->pc), curr_func);
  
  IRBuilder<> builder(Entry);
  Data = curr_func->arg_begin();
  DataPtr = builder.CreateLoad(Data);
  builder.CreateBr(Header);
  
  builder.SetInsertPoint(Header);
  HeaderPHI = builder.CreatePHI(DataPtr->getType());
  HeaderPHI->addIncoming(DataPtr, Entry);
  DataPtr = HeaderPHI;
  compile_opcode(trace, builder);
  
  FunctionPassManager OurFPM(module);
  createStandardFunctionPasses(&OurFPM, 3);

  OurFPM.run(*curr_func);
  
  compile_map[trace->pc] = curr_func;
  code_map[trace->pc] =
    (trace_func_t)(intptr_t)(EE->getPointerToFunction(curr_func));
}

void BrainFTraceRecorder::compile_plus(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  Value *CellValue = builder.CreateLoad(DataPtr);
  Constant *One =
    ConstantInt::get(IntegerType::getInt8Ty(Header->getContext()), 1);
  Value *UpdatedValue = builder.CreateAdd(CellValue, One);
  builder.CreateStore(UpdatedValue, DataPtr);
  
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                       
void BrainFTraceRecorder::compile_minus(BrainFTraceNode *node,
                                        IRBuilder<>& builder) {
  Value *CellValue = builder.CreateLoad(DataPtr);
  Constant *One =
    ConstantInt::get(IntegerType::getInt8Ty(Header->getContext()), 1);
  Value *UpdatedValue = builder.CreateSub(CellValue, One);
  builder.CreateStore(UpdatedValue, DataPtr);
  
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                                                            
void BrainFTraceRecorder::compile_left(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  Instruction *OldPtr = DataPtr;
  DataPtr = cast<Instruction>(builder.CreateConstInBoundsGEP1_32(DataPtr, -1));
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
  DataPtr = OldPtr;
}
                                                                                                                   
void BrainFTraceRecorder::compile_right(BrainFTraceNode *node,
                                        IRBuilder<>& builder) {
  Instruction *OldPtr = DataPtr;
  DataPtr = cast<Instruction>(builder.CreateConstInBoundsGEP1_32(DataPtr, 1));
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
  DataPtr = OldPtr;
}
                                                                                                                                                          
void BrainFTraceRecorder::compile_put(BrainFTraceNode *node,
                                      IRBuilder<>& builder) {
  Value *Loaded = builder.CreateLoad(DataPtr);
  Value *Print =
    builder.CreateSExt(Loaded, IntegerType::get(Loaded->getContext(), 32));
  builder.CreateCall(pchar, Print);
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                                                                                                                                                                                 
void BrainFTraceRecorder::compile_get(BrainFTraceNode *node,
                                      IRBuilder<>& builder) {
  Value *Ret = builder.CreateCall(gchar);
  Value *Trunc =
    builder.CreateTrunc(Ret, IntegerType::get(Ret->getContext(), 8));
  builder.CreateStore(Ret, Trunc);
  if (node->left)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                                                                                                                                                                                                                        
void BrainFTraceRecorder::compile_if(BrainFTraceNode *node,
                                     IRBuilder<>& builder) {
  BasicBlock *ZeroChild = 0;
  BasicBlock *NonZeroChild = 0;
  
  IRBuilder<> oldBuilder = builder;
  
  LLVMContext &Context = Header->getContext();
  if (node->left == 0 && node->right == 0) {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
    return;
  }
  
  if (node->left != 0) {
    NonZeroChild = BasicBlock::Create(Context, 
                                      utostr(node->left->pc), 
                                      Header->getParent());
    builder.SetInsertPoint(NonZeroChild);
    compile_opcode(node->left, builder);
  } else {
    NonZeroChild = BasicBlock::Create(Context,
                                   "exit_left_"+utostr(node->pc),
                                   Header->getParent());
    builder.SetInsertPoint(NonZeroChild);
    const Type *pc_type = sizeof(size_t) == 32 ? 
                    IntegerType::getInt32Ty(Context) : 
                    IntegerType::getInt64Ty(Context);
    builder.CreateStore(DataPtr, Data);
    builder.CreateRet(ConstantInt::get(pc_type, node->pc));
  }
  
  if (node->right != 0) {
    ZeroChild = BasicBlock::Create(Context, 
                                   utostr(node->right->pc), 
                                   Header->getParent());
    builder.SetInsertPoint(ZeroChild);
    compile_opcode(node->right, builder);
  } else {
    ZeroChild = BasicBlock::Create(Context,
                                   "exit_right_"+utostr(node->pc),
                                   Header->getParent());
    builder.SetInsertPoint(ZeroChild);
    const Type *pc_type = sizeof(size_t) == 32 ? 
                    IntegerType::getInt32Ty(Context) : 
                    IntegerType::getInt64Ty(Context);
    builder.CreateStore(DataPtr, Data);
    builder.CreateRet(ConstantInt::get(pc_type, node->pc));
  }
  
  Value *Loaded = oldBuilder.CreateLoad(DataPtr);
  Value *Cmp = oldBuilder.CreateICmpEQ(Loaded, 
                                       ConstantInt::get(Loaded->getType(), 0));
  oldBuilder.CreateCondBr(Cmp, ZeroChild, NonZeroChild);
}
                                                                                                                                                                                                                                                                               
void BrainFTraceRecorder::compile_back(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  if (node->right)
    compile_opcode(node->right, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}

void BrainFTraceRecorder::compile_opcode(BrainFTraceNode *node,
                                         IRBuilder<>& builder) {
  switch (node->opcode) {
    case '+':
      compile_plus(node, builder);
      break;
    case '-':
      compile_minus(node, builder);
      break;
    case '<':
      compile_left(node, builder);
      break;
    case '>':
      compile_right(node, builder);
      break;
    case '.':
      compile_put(node, builder);
      break;
    case ',':
      compile_get(node, builder);
      break;
    case '[':
      compile_if(node, builder);
      break;
    case ']':
      compile_back(node, builder);
      break;
  }
}
