#include "../firmware/src/history.h"
#include "../firmware/src/raster.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>

static uint16_t pixel(const uint8_t *row, int x) { return row[2*x]*256+row[2*x+1]; }
int main() {
    TraceHistory h;
    int p[3] = {0, 50, 100}; bool known[3] = {true, false, true};
    assert(h.append(0xFFFFFFF0, p, known));
    assert(h.values[0][23]==0 && h.values[1][23]==255 && h.values[2][23]==100);
    assert(!h.append(0xFFFFFFF0, p, known));
    assert(h.values[0][22]==255);
    p[0]=100;
    assert(h.append(10, p, known)); // clock rollover remains a new sample
    assert(h.values[0][22]==0 && h.values[0][23]==100);
    for(int i=0;i<30;++i) { p[0]=i; h.append(20+i,p,known); }
    assert(h.values[0][0]==6 && h.values[0][23]==29);
    uint8_t row[480];
    uint8_t endpoints[]={0,100};
    rasterTraceRow(row,101,25,24,endpoints,2,1,2,3);
    assert(pixel(row,0)==1 && pixel(row,100)==2);
    rasterTraceRow(row,101,25,0,endpoints,2,1,2,3);
    assert(pixel(row,100)==1 && pixel(row,0)==2);
    uint8_t gap[]={255,100};
    rasterTraceRow(row,101,25,0,gap,2,1,2,3);
    for(int x=0;x<100;++x) assert(pixel(row,x)==2);
    assert(pixel(row,100)==1);
    // Exactly three rows at midscale and both limits; never bridge a gap.
    for (uint8_t level : {0, 50, 100}) {
        uint8_t flat[]={level,level};
        int lit=0;
        for (int y=0;y<25;++y) {
            rasterTraceRow(row,101,25,y,flat,2,1,2,3);
            if (pixel(row,50)==1) ++lit;
            rasterTraceRow(row,101,25,y,gap,2,1,2,3);
            assert(pixel(row,50)!=1);
        }
        assert(lit==3);
    }
    puts("Trace history deduplication, missing samples, rollover, retention and raster passed");
}
