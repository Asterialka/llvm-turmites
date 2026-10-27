#include "irgen.h"
#include "sim.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/ExecutionEngine.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    printf("Usage: %s [--frames N] [--headless] [--emit FILE] [--no-run]\n"
           "  --frames N  stop after N frames; 0 means until window closes\n"
           "  --headless  print frame and drawing hashes without SDL video\n"
           "  --emit FILE save the generated LLVM IR\n"
           "  --no-run    only generate and verify IR\n", program);
}

static int parse_frames(const char *text, unsigned *frames)
{
    if (*text < '0' || *text > '9')
        return 0;
    errno = 0;
    char *end;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > UINT32_MAX)
        return 0;
    *frames = (unsigned)n;
    return 1;
}

int main(int argc, char **argv)
{
    unsigned frames = 0;
    int headless = 0, no_run = 0;
    const char *output = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--no-run") == 0) {
            no_run = 1;
        } else if (strcmp(argv[i], "--emit") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (!parse_frames(argv[++i], &frames)) {
                fputs("Invalid frame count\n", stderr);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }
    if (headless && frames == 0 && !no_run) {
        fputs("--headless requires a positive --frames count\n", stderr);
        return EXIT_FAILURE;
    }

    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = build_app_module(context);
    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(module, triple);
    LLVMDisposeMessage(triple);
    char *error = NULL;
    int status = EXIT_FAILURE;
    if (LLVMVerifyModule(module, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "Invalid generated IR: %s\n", error);
        LLVMDisposeMessage(error);
        goto dispose_module;
    }
    LLVMDisposeMessage(error);
    error = NULL;
    if (output && LLVMPrintModuleToFile(module, output, &error)) {
        fprintf(stderr, "Cannot write IR: %s\n", error);
        LLVMDisposeMessage(error);
        goto dispose_module;
    }
    if (no_run) {
        status = EXIT_SUCCESS;
        goto dispose_module;
    }

    LLVMLinkInInterpreter();
    LLVMExecutionEngineRef engine = NULL;
    LLVMValueRef entry = LLVMGetNamedFunction(module, "app");
    LLVMValueRef pixel = LLVMGetNamedFunction(module, "simPutPixel");
    LLVMValueRef flush = LLVMGetNamedFunction(module, "simFlush");
    if (LLVMCreateInterpreterForModule(&engine, module, &error)) {
        fprintf(stderr, "Cannot create LLVM interpreter: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMContextDispose(context);
        return EXIT_FAILURE;
    }

    LLVMAddGlobalMapping(engine, pixel, (void *)(uintptr_t)simPutPixel);
    LLVMAddGlobalMapping(engine, flush, (void *)(uintptr_t)simFlush);
    if (simInit(headless)) {
        LLVMGenericValueRef arg = LLVMCreateGenericValueOfInt(
            LLVMInt32TypeInContext(context), frames, 0);
        LLVMGenericValueRef result = LLVMRunFunction(engine, entry, 1, &arg);
        LLVMDisposeGenericValue(result);
        LLVMDisposeGenericValue(arg);
        simExit();
        status = EXIT_SUCCESS;
    }
    LLVMDisposeExecutionEngine(engine);
    LLVMContextDispose(context);
    return status;

dispose_module:
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    return status;
}
