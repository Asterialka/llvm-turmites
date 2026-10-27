#include "app.h"

static void paint_cell(int x, int y, int argb)
{
    int base_x = x * CELL_SIZE;
    int base_y = y * CELL_SIZE;
    for (int dy = 0; dy < CELL_SIZE; ++dy)
        for (int dx = 0; dx < CELL_SIZE; ++dx)
            simPutPixel(base_x + dx, base_y + dy, argb);
}

void app(unsigned frames)
{
    int cells[GRID_HEIGHT][GRID_WIDTH] = {{0}};
    int ant_x[ANT_COUNT], ant_y[ANT_COUNT];
    int direction[ANT_COUNT], phase[ANT_COUNT];
    const int turn[COLOURS] = {1, -1, -1, 1, 1, -1};
    const int palette[COLOURS] = {
        (int)0xFF000000u, (int)0xFF293C83u, (int)0xFF398BB6u,
        (int)0xFF78D4BEu, (int)0xFFF3CA7Du, (int)0xFFEF7185u
    };

    for (int i = 0; i < ANT_COUNT; ++i) {
        ant_x[i] = (i * 101 + 37) % GRID_WIDTH;
        ant_y[i] = (i * 73 + 19) % GRID_HEIGHT;
        direction[i] = (i * 7) & 3;
        phase[i] = i % COLOURS;
    }

    unsigned frame = 0;
    do {
        for (int step = 0; step < STEPS_PER_FRAME; ++step) {
            for (int i = 0; i < ANT_COUNT; ++i) {
                int x = ant_x[i], y = ant_y[i];
                int old_colour = cells[y][x];
                int new_colour = old_colour + 1;
                int rule_index = old_colour + phase[i];
                if (new_colour == COLOURS)
                    new_colour = 0;
                if (rule_index >= COLOURS)
                    rule_index -= COLOURS;

                direction[i] = (direction[i] + turn[rule_index] + 4) & 3;
                cells[y][x] = new_colour;
                paint_cell(x, y, palette[new_colour]);

                ant_x[i] += (direction[i] == 1) - (direction[i] == 3);
                ant_y[i] += (direction[i] == 2) - (direction[i] == 0);
                if (ant_x[i] < 0)
                    ant_x[i] = GRID_WIDTH - 1;
                if (ant_x[i] == GRID_WIDTH)
                    ant_x[i] = 0;
                if (ant_y[i] < 0)
                    ant_y[i] = GRID_HEIGHT - 1;
                if (ant_y[i] == GRID_HEIGHT)
                    ant_y[i] = 0;
            }
        }

        for (int i = 0; i < ANT_COUNT; ++i) {
            int px = ant_x[i] * CELL_SIZE + 1;
            int py = ant_y[i] * CELL_SIZE + 1;
            simPutPixel(px, py, (int)0xFFFFFFFFu);
            simPutPixel(px + 1, py, (int)0xFFFFFFFFu);
            simPutPixel(px, py + 1, (int)0xFFFFFFFFu);
            simPutPixel(px + 1, py + 1, (int)0xFFFFFFFFu);
        }
        if (!simFlush())
            break;
        ++frame;
    } while (frames == 0 || frame < frames);
}
