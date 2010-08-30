//===-- BrainFCodeGen.cpp - BrainF compiler driver -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "BrainFVM.h"
#include "llvm/Attributes.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/StringExtras.h"

/// initialize_module - perform setup of the LLVM code generation system.
void BrainFTraceRecorder::initialize_module() {
  LLVMContext &Context = module->getContext();
  
  // Initialize the code generator, and enable aggressive code generation.
  InitializeNativeTarget();
  EngineBuilder builder(module);
  builder.setOptLevel(CodeGenOpt::Aggressive);
  EE = builder.create();
  
  // Create a FunctionPassManager to handle running optimization passes
  // on our generated code.  Setup a basic suite of optimizations for it.
  FPM = new llvm::FunctionPassManager(module);
  FPM->add(createInstructionCombiningPass());
  FPM->add(createCFGSimplificationPass());
  FPM->add(createScalarReplAggregatesPass());
  FPM->add(createSimplifyLibCallsPass());
  FPM->add(createInstructionCombiningPass());
  FPM->add(createJumpThreadingPass());
  FPM->add(createCFGSimplificationPass());
  FPM->add(createInstructionCombiningPass());
  FPM->add(createCFGSimplificationPass());
  FPM->add(createReassociatePass());
  FPM->add(createLoopRotatePass());
  FPM->add(createLICMPass());
  FPM->add(createLoopUnswitchPass(false));
  FPM->add(createInstructionCombiningPass());  
  FPM->add(createIndVarSimplifyPass());
  FPM->add(createLoopDeletionPass());
  FPM->add(createLoopUnrollPass());
  FPM->add(createInstructionCombiningPass());
  FPM->add(createGVNPass());
  FPM->add(createSCCPPass());
  FPM->add(createInstructionCombiningPass());
  FPM->add(createJumpThreadingPass());
  FPM->add(createDeadStoreEliminationPass());
  FPM->add(createAggressiveDCEPass());
  FPM->add(createCFGSimplificationPass());
  
  // Cache the LLVM type signature of an opcode function
  int_type = sizeof(size_t) == 4 ? 
                  IntegerType::getInt32Ty(Context) : 
                  IntegerType::getInt64Ty(Context);
  const Type *data_type =
    PointerType::getUnqual(IntegerType::getInt8Ty(Context));
  std::vector<const Type*> args;
  args.push_back(int_type);
  args.push_back(data_type);
  op_type =
    FunctionType::get(Type::getVoidTy(Context), args, false);
  
  // Setup a global variable in the LLVM module to represent the bytecode
  // array.  Bind it to the actual bytecode array at JIT time.
  const Type *bytecode_type = PointerType::getUnqual(op_type);
  bytecode_array = cast<GlobalValue>(module->
    getOrInsertGlobal("BytecodeArray", bytecode_type));
  EE->addGlobalMapping(bytecode_array, BytecodeArray);
  
  // Setup a similar mapping for the global executed flag.
  const IntegerType *flag_type = IntegerType::get(Context, 8);
  executed_flag =
    cast<GlobalValue>(module->getOrInsertGlobal("executed", flag_type));
  EE->addGlobalMapping(executed_flag, &executed);

  // Cache LLVM declarations for putchar() and getchar().
  const Type *int_type = sizeof(int) == 4 ? IntegerType::getInt32Ty(Context)
                                       : IntegerType::getInt64Ty(Context);
  putchar_func =
    module->getOrInsertFunction("putchar", int_type, int_type, NULL);
  getchar_func = module->getOrInsertFunction("getchar", int_type, NULL);
}

void BrainFTraceRecorder::compile(BrainFTraceNode* trace) {
  LLVMContext &Context = module->getContext();
  Function *curr_func = cast<Function>(module->
    getOrInsertFunction("trace_"+utostr(trace->pc), op_type));
  
  BasicBlock *Entry = BasicBlock::Create(Context, "entry", curr_func);
  Header = BasicBlock::Create(Context, utostr(trace->pc), curr_func);
  
  IRBuilder<> builder(Entry);
  Argument *Arg1 = ++curr_func->arg_begin();
  Arg1->addAttr(Attribute::NoAlias);
  DataPtr = Arg1;
  
  const IntegerType *flag_type = IntegerType::get(Context, 8);
  ConstantInt *True = ConstantInt::get(flag_type, 1);
  builder.CreateStore(True, executed_flag);
  builder.CreateBr(Header);
  
  builder.SetInsertPoint(Header);
  HeaderPHI = builder.CreatePHI(DataPtr->getType());
  HeaderPHI->addIncoming(DataPtr, Entry);
  DataPtr = HeaderPHI;
  compile_opcode(trace, builder);

  FPM->run(*curr_func);
  
  void *code = EE->getPointerToFunction(curr_func);
  BytecodeArray[trace->pc] =
    (opcode_func_t)(intptr_t)code;
}

void BrainFTraceRecorder::compile_plus(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  Value *CellValue = builder.CreateLoad(DataPtr);
  Constant *One =
    ConstantInt::get(IntegerType::getInt8Ty(Header->getContext()), 1);
  Value *UpdatedValue = builder.CreateAdd(CellValue, One);
  builder.CreateStore(UpdatedValue, DataPtr);
  
  if (node->left != (BrainFTraceNode*)~0ULL)
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
  
  if (node->left != (BrainFTraceNode*)~0ULL)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                                                            
void BrainFTraceRecorder::compile_left(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  Value *OldPtr = DataPtr;
  DataPtr = builder.CreateConstInBoundsGEP1_32(DataPtr, -1);
  if (node->left != (BrainFTraceNode*)~0ULL)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
  DataPtr = OldPtr;
}
                                                                                                                   
void BrainFTraceRecorder::compile_right(BrainFTraceNode *node,
                                        IRBuilder<>& builder) {
  Value *OldPtr = DataPtr;
  DataPtr = builder.CreateConstInBoundsGEP1_32(DataPtr, 1);
  if (node->left != (BrainFTraceNode*)~0ULL)
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
  builder.CreateCall(putchar_func, Print);
  if (node->left != (BrainFTraceNode*)~0ULL)
    compile_opcode(node->left, builder);
  else {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
  }
}
                                                                                                                                                                                                 
void BrainFTraceRecorder::compile_get(BrainFTraceNode *node,
                                      IRBuilder<>& builder) {
  Value *Ret = builder.CreateCall(getchar_func);
  Value *Trunc =
    builder.CreateTrunc(Ret, IntegerType::get(Ret->getContext(), 8));
  builder.CreateStore(Ret, Trunc);
  if (node->left != (BrainFTraceNode*)~0ULL)
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
  if (node->left == (BrainFTraceNode*)~0ULL &&
      node->right == (BrainFTraceNode*)~0ULL) {
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
    builder.CreateBr(Header);
    return;
  }
  
  if (node->left == (BrainFTraceNode*)~0ULL) {
    NonZeroChild = Header;
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
  } else if (node->left == 0) {
    NonZeroChild = BasicBlock::Create(Context,
                                   "exit_left_"+utostr(node->pc),
                                   Header->getParent());
    builder.SetInsertPoint(NonZeroChild);
    ConstantInt *NewPc = ConstantInt::get(int_type, node->pc+1);
    Value *BytecodeIndex =
      builder.CreateConstInBoundsGEP1_32(bytecode_array, node->pc+1);
    Value *Target = builder.CreateLoad(BytecodeIndex);
    CallInst *Call =cast<CallInst>(builder.CreateCall2(Target, NewPc, DataPtr));
    Call->setTailCall();
    builder.CreateRetVoid();
  } else {
    NonZeroChild = BasicBlock::Create(Context, 
                                      utostr(node->left->pc), 
                                      Header->getParent());
    builder.SetInsertPoint(NonZeroChild);
    compile_opcode(node->left, builder);
  }
  
  if (node->right == (BrainFTraceNode*)~0ULL) {
    ZeroChild = Header;
    HeaderPHI->addIncoming(DataPtr, builder.GetInsertBlock());
  } else if (node->right == 0) {
    ZeroChild = BasicBlock::Create(Context,
                                   "exit_right_"+utostr(node->pc),
                                   Header->getParent());
    builder.SetInsertPoint(ZeroChild);
    ConstantInt *NewPc = ConstantInt::get(int_type, JumpMap[node->pc]+1);
    Value *BytecodeIndex =
      builder.CreateConstInBoundsGEP1_32(bytecode_array, JumpMap[node->pc]+1);
    Value *Target = builder.CreateLoad(BytecodeIndex);
    CallInst *Call =cast<CallInst>(builder.CreateCall2(Target, NewPc, DataPtr));
    Call->setTailCall();
    builder.CreateRetVoid();
  } else {
    ZeroChild = BasicBlock::Create(Context, 
                                      utostr(node->right->pc), 
                                      Header->getParent());
    builder.SetInsertPoint(ZeroChild);
    compile_opcode(node->right, builder);
  }
  
  Value *Loaded = oldBuilder.CreateLoad(DataPtr);
  Value *Cmp = oldBuilder.CreateICmpEQ(Loaded, 
                                       ConstantInt::get(Loaded->getType(), 0));
  oldBuilder.CreateCondBr(Cmp, ZeroChild, NonZeroChild);
}
                                                                                                                                                                                                                                                                               
void BrainFTraceRecorder::compile_back(BrainFTraceNode *node,
                                       IRBuilder<>& builder) {
  if (node->right != (BrainFTraceNode*)~0ULL)
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
