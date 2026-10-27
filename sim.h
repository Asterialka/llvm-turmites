#ifndef SIM_H
#define SIM_H

#define SIM_X_SIZE 1536
#define SIM_Y_SIZE 768

int simInit(int headless);
void simExit(void);
void simPutPixel(int x, int y, int argb);
int simFlush(void);

#endif
