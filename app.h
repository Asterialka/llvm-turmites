#ifndef APP_H
#define APP_H

#include "sim.h"

enum {
    CELL_SIZE = 4,
    GRID_WIDTH = SIM_X_SIZE / CELL_SIZE,
    GRID_HEIGHT = SIM_Y_SIZE / CELL_SIZE,
    ANT_COUNT = 32,
    STEPS_PER_FRAME = 128,
    COLOURS = 6
};

void app(unsigned frames);

#endif
