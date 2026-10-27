#include "irgen.h"
#include "app.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTypeRef i32, void_type, pixel_type, flush_type, paint_type;
    LLVMValueRef pixel, flush, paint;
} Generator;

typedef struct {
    LLVMValueRef index;
    LLVMBasicBlockRef header, latch, exit;
} Loop;

static LLVMValueRef integer(Generator *g, int64_t n)
{
    return LLVMConstInt(g->i32, (uint64_t)n, 0);
}

static LLVMBasicBlockRef block(Generator *g, LLVMValueRef fn, const char *name)
{
    return LLVMAppendBasicBlockInContext(g->context, fn, name);
}

static Loop begin_loop(Generator *g, LLVMValueRef fn, int limit, const char *name)
{
    LLVMBasicBlockRef preheader = LLVMGetInsertBlock(g->builder);
    Loop loop = {0};
    loop.header = block(g, fn, name);
    LLVMBasicBlockRef body = block(g, fn, "loop.body");
    loop.latch = block(g, fn, "loop.latch");
    loop.exit = block(g, fn, "loop.exit");
    LLVMBuildBr(g->builder, loop.header);
    LLVMPositionBuilderAtEnd(g->builder, loop.header);
    loop.index = LLVMBuildPhi(g->builder, g->i32, "i");
    LLVMValueRef zero = integer(g, 0);
    LLVMAddIncoming(loop.index, &zero, &preheader, 1);
    LLVMValueRef condition = LLVMBuildICmp(g->builder, LLVMIntSLT, loop.index,
                                          integer(g, limit), "in.range");
    LLVMBuildCondBr(g->builder, condition, body, loop.exit);
    LLVMPositionBuilderAtEnd(g->builder, body);
    return loop;
}

static void end_loop(Generator *g, Loop loop)
{
    LLVMBuildBr(g->builder, loop.latch);
    LLVMPositionBuilderAtEnd(g->builder, loop.latch);
    LLVMValueRef next = LLVMBuildAdd(g->builder, loop.index, integer(g, 1), "next");
    LLVMAddIncoming(loop.index, &next, &loop.latch, 1);
    LLVMBuildBr(g->builder, loop.header);
    LLVMPositionBuilderAtEnd(g->builder, loop.exit);
}

static LLVMValueRef element(Generator *g, LLVMValueRef array, LLVMValueRef index)
{
    return LLVMBuildGEP2(g->builder, g->i32, array, &index, 1, "element");
}

static LLVMValueRef load(Generator *g, LLVMValueRef array, LLVMValueRef index)
{
    return LLVMBuildLoad2(g->builder, g->i32, element(g, array, index), "value");
}

static void store(Generator *g, LLVMValueRef array, LLVMValueRef index,
                  LLVMValueRef value)
{
    LLVMBuildStore(g->builder, value, element(g, array, index));
}

static LLVMValueRef array(Generator *g, int length, const char *name)
{
    return LLVMBuildArrayAlloca(g->builder, g->i32, integer(g, length), name);
}

static LLVMValueRef constant_array(Generator *g, const char *name,
                                    const int64_t values[COLOURS])
{
    LLVMValueRef constants[COLOURS];
    for (unsigned i = 0; i < COLOURS; ++i)
        constants[i] = integer(g, values[i]);
    LLVMValueRef init = LLVMConstArray(g->i32, constants, COLOURS);
    LLVMValueRef global = LLVMAddGlobal(g->module, LLVMTypeOf(init), name);
    LLVMSetInitializer(global, init);
    LLVMSetGlobalConstant(global, 1);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    return global;
}

static void put_pixel(Generator *g, LLVMValueRef x, LLVMValueRef y,
                       LLVMValueRef colour)
{
    LLVMValueRef args[] = {x, y, colour};
    LLVMBuildCall2(g->builder, g->pixel_type, g->pixel, args, 3, "");
}

static void build_paint_cell(Generator *g)
{
    LLVMSetLinkage(g->paint, LLVMInternalLinkage);
    LLVMPositionBuilderAtEnd(g->builder, block(g, g->paint, "entry"));
    LLVMValueRef x = LLVMBuildMul(g->builder, LLVMGetParam(g->paint, 0),
                                  integer(g, CELL_SIZE), "base.x");
    LLVMValueRef y = LLVMBuildMul(g->builder, LLVMGetParam(g->paint, 1),
                                  integer(g, CELL_SIZE), "base.y");
    Loop dy = begin_loop(g, g->paint, CELL_SIZE, "dy");
    Loop dx = begin_loop(g, g->paint, CELL_SIZE, "dx");
    put_pixel(g, LLVMBuildAdd(g->builder, x, dx.index, "pixel.x"),
              LLVMBuildAdd(g->builder, y, dy.index, "pixel.y"),
              LLVMGetParam(g->paint, 2));
    end_loop(g, dx);
    end_loop(g, dy);
    LLVMBuildRetVoid(g->builder);
}

static LLVMValueRef move_coordinate(Generator *g, LLVMValueRef coordinate,
                                    LLVMValueRef direction, int positive,
                                    int negative, int size)
{
    LLVMValueRef plus = LLVMBuildICmp(g->builder, LLVMIntEQ, direction,
                                      integer(g, positive), "positive");
    LLVMValueRef minus = LLVMBuildICmp(g->builder, LLVMIntEQ, direction,
                                       integer(g, negative), "negative");
    plus = LLVMBuildZExt(g->builder, plus, g->i32, "plus");
    minus = LLVMBuildZExt(g->builder, minus, g->i32, "minus");
    LLVMValueRef delta = LLVMBuildSub(g->builder, plus, minus, "delta");
    LLVMValueRef moved = LLVMBuildAdd(g->builder, coordinate, delta, "moved");
    LLVMValueRef below = LLVMBuildICmp(g->builder, LLVMIntSLT, moved,
                                       integer(g, 0), "below.zero");
    moved = LLVMBuildSelect(g->builder, below, integer(g, size - 1), moved,
                            "wrap.negative");
    LLVMValueRef above = LLVMBuildICmp(g->builder, LLVMIntEQ, moved,
                                       integer(g, size), "at.boundary");
    return LLVMBuildSelect(g->builder, above, integer(g, 0), moved, "wrapped");
}

static void build_app(Generator *g)
{
    const int64_t turns[COLOURS] = {1, -1, -1, 1, 1, -1};
    const int64_t colours[COLOURS] = {
        0xFF000000, 0xFF293C83, 0xFF398BB6,
        0xFF78D4BE, 0xFFF3CA7D, 0xFFEF7185
    };
    LLVMValueRef turn = constant_array(g, "turn", turns);
    LLVMValueRef palette = constant_array(g, "palette", colours);
    LLVMTypeRef type = LLVMFunctionType(g->void_type, &g->i32, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->module, "app", type);
    LLVMValueRef frames = LLVMGetParam(fn, 0);
    LLVMSetValueName2(frames, "frames", 6);
    LLVMPositionBuilderAtEnd(g->builder, block(g, fn, "entry"));

    LLVMValueRef cells = array(g, GRID_WIDTH * GRID_HEIGHT, "cells");
    LLVMValueRef ant_x = array(g, ANT_COUNT, "ant.x");
    LLVMValueRef ant_y = array(g, ANT_COUNT, "ant.y");
    LLVMValueRef direction = array(g, ANT_COUNT, "direction");
    LLVMValueRef phase = array(g, ANT_COUNT, "phase");
    Loop clear = begin_loop(g, fn, GRID_WIDTH * GRID_HEIGHT, "clear.cells");
    store(g, cells, clear.index, integer(g, 0));
    end_loop(g, clear);

    Loop init = begin_loop(g, fn, ANT_COUNT, "init.ants");
    LLVMValueRef i = init.index;
    LLVMValueRef x = LLVMBuildMul(g->builder, i, integer(g, 101), "x.mul");
    x = LLVMBuildAdd(g->builder, x, integer(g, 37), "x.add");
    x = LLVMBuildSRem(g->builder, x, integer(g, GRID_WIDTH), "x.initial");
    store(g, ant_x, i, x);
    LLVMValueRef y = LLVMBuildMul(g->builder, i, integer(g, 73), "y.mul");
    y = LLVMBuildAdd(g->builder, y, integer(g, 19), "y.add");
    y = LLVMBuildSRem(g->builder, y, integer(g, GRID_HEIGHT), "y.initial");
    store(g, ant_y, i, y);
    LLVMValueRef dir = LLVMBuildMul(g->builder, i, integer(g, 7), "dir.mul");
    dir = LLVMBuildAnd(g->builder, dir, integer(g, 3), "dir.initial");
    store(g, direction, i, dir);
    store(g, phase, i, LLVMBuildSRem(g->builder, i, integer(g, COLOURS), "phase.initial"));
    end_loop(g, init);

    LLVMBasicBlockRef before_frames = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef frame_header = block(g, fn, "frame");
    LLVMBasicBlockRef finish = block(g, fn, "finish");
    LLVMBuildBr(g->builder, frame_header);
    LLVMPositionBuilderAtEnd(g->builder, frame_header);
    LLVMValueRef frame = LLVMBuildPhi(g->builder, g->i32, "frame.number");
    LLVMValueRef zero = integer(g, 0);
    LLVMAddIncoming(frame, &zero, &before_frames, 1);

    Loop step = begin_loop(g, fn, STEPS_PER_FRAME, "step");
    Loop ant = begin_loop(g, fn, ANT_COUNT, "ant");
    i = ant.index;
    x = load(g, ant_x, i);
    y = load(g, ant_y, i);
    LLVMValueRef offset = LLVMBuildMul(g->builder, y, integer(g, GRID_WIDTH), "row");
    offset = LLVMBuildAdd(g->builder, offset, x, "cell.offset");
    LLVMValueRef old = load(g, cells, offset);
    LLVMValueRef colour = LLVMBuildAdd(g->builder, old, integer(g, 1), "colour.next");
    LLVMValueRef at_end = LLVMBuildICmp(g->builder, LLVMIntEQ, colour,
                                       integer(g, COLOURS), "colour.at.end");
    colour = LLVMBuildSelect(g->builder, at_end, zero, colour, "colour");
    LLVMValueRef rule = LLVMBuildAdd(g->builder, old, load(g, phase, i), "rule.sum");
    LLVMValueRef wrap_rule = LLVMBuildICmp(g->builder, LLVMIntSGE, rule,
                                          integer(g, COLOURS), "rule.at.end");
    LLVMValueRef reduced = LLVMBuildSub(g->builder, rule, integer(g, COLOURS), "rule.reduced");
    rule = LLVMBuildSelect(g->builder, wrap_rule, reduced, rule, "rule");
    dir = LLVMBuildAdd(g->builder, load(g, direction, i), load(g, turn, rule), "dir.turn");
    dir = LLVMBuildAdd(g->builder, dir, integer(g, 4), "dir.positive");
    dir = LLVMBuildAnd(g->builder, dir, integer(g, 3), "dir");
    store(g, direction, i, dir);
    store(g, cells, offset, colour);
    LLVMValueRef paint_args[] = {x, y, load(g, palette, colour)};
    LLVMBuildCall2(g->builder, g->paint_type, g->paint, paint_args, 3, "");
    store(g, ant_x, i, move_coordinate(g, x, dir, 1, 3, GRID_WIDTH));
    store(g, ant_y, i, move_coordinate(g, y, dir, 2, 0, GRID_HEIGHT));
    end_loop(g, ant);
    end_loop(g, step);

    Loop markers = begin_loop(g, fn, ANT_COUNT, "markers");
    x = LLVMBuildMul(g->builder, load(g, ant_x, markers.index), integer(g, CELL_SIZE), "marker.x.base");
    y = LLVMBuildMul(g->builder, load(g, ant_y, markers.index), integer(g, CELL_SIZE), "marker.y.base");
    x = LLVMBuildAdd(g->builder, x, integer(g, 1), "marker.x");
    y = LLVMBuildAdd(g->builder, y, integer(g, 1), "marker.y");
    LLVMValueRef x1 = LLVMBuildAdd(g->builder, x, integer(g, 1), "marker.x1");
    LLVMValueRef y1 = LLVMBuildAdd(g->builder, y, integer(g, 1), "marker.y1");
    put_pixel(g, x, y, integer(g, -1));
    put_pixel(g, x1, y, integer(g, -1));
    put_pixel(g, x, y1, integer(g, -1));
    put_pixel(g, x1, y1, integer(g, -1));
    end_loop(g, markers);

    LLVMValueRef open = LLVMBuildCall2(g->builder, g->flush_type, g->flush, NULL, 0, "window.open");
    open = LLVMBuildICmp(g->builder, LLVMIntNE, open, zero, "keep.running");
    LLVMValueRef next_frame = LLVMBuildAdd(g->builder, frame, integer(g, 1), "next.frame");
    LLVMValueRef unlimited = LLVMBuildICmp(g->builder, LLVMIntEQ, frames, zero, "unlimited");
    LLVMValueRef remaining = LLVMBuildICmp(g->builder, LLVMIntULT, next_frame, frames, "remaining");
    remaining = LLVMBuildOr(g->builder, unlimited, remaining, "more.frames");
    LLVMValueRef again = LLVMBuildAnd(g->builder, open, remaining, "continue");
    LLVMBasicBlockRef frame_end = LLVMGetInsertBlock(g->builder);
    LLVMAddIncoming(frame, &next_frame, &frame_end, 1);
    LLVMBuildCondBr(g->builder, again, frame_header, finish);
    LLVMPositionBuilderAtEnd(g->builder, finish);
    LLVMBuildRetVoid(g->builder);
}

LLVMModuleRef build_app_module(LLVMContextRef context)
{
    Generator g = {0};
    g.context = context;
    g.module = LLVMModuleCreateWithNameInContext("turmite_colony", context);
    g.builder = LLVMCreateBuilderInContext(context);
    g.i32 = LLVMInt32TypeInContext(context);
    g.void_type = LLVMVoidTypeInContext(context);
    LLVMTypeRef pixel_params[] = {g.i32, g.i32, g.i32};
    g.pixel_type = LLVMFunctionType(g.void_type, pixel_params, 3, 0);
    g.paint_type = g.pixel_type;
    g.flush_type = LLVMFunctionType(g.i32, NULL, 0, 0);
    g.pixel = LLVMAddFunction(g.module, "simPutPixel", g.pixel_type);
    g.flush = LLVMAddFunction(g.module, "simFlush", g.flush_type);
    g.paint = LLVMAddFunction(g.module, "paint_cell", g.paint_type);
    build_paint_cell(&g);
    build_app(&g);
    LLVMDisposeBuilder(g.builder);
    return g.module;
}
