#ifndef IRGEN_H
#define IRGEN_H

#include <llvm-c/Core.h>

LLVMModuleRef build_app_module(LLVMContextRef context);

#endif
