#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "oled_ui.h"

typedef uint8_t frame_t[8][128];
static struct { uint32_t before; frame_t frame; uint32_t after; } guarded;
static easycon_script_status_t state;
static hid_display_snapshot_t input = {.report={.HAT=8,.LX=128,.LY=128,.RX=128,.RY=128}};
static void render(void) {
    guarded.before=0x12345678; guarded.after=0x98765432;
    oled_ui_render(guarded.frame,&state,&input,false);
    assert(guarded.before==0x12345678 && guarded.after==0x98765432);
}
static bool pixel(const frame_t f,int x,int y) { return (f[y/8][x] >> (y%8)) & 1; }
static void check_changes(const frame_t a,int x,int y,int w,int h) {
    unsigned changes=0;
    for(int yy=0;yy<64;yy++) for(int xx=0;xx<128;xx++)
        if(pixel(a,xx,yy)!=pixel(guarded.frame,xx,yy)) {
            assert(xx>=x && xx<x+w && yy>=y && yy<y+h);
            ++changes;
        }
    assert(changes>0);
}
static void preview(const char *path) {
    FILE *file=fopen(path,"wb"); assert(file);
    fprintf(file,"P5\n384 128\n255\n");
    frame_t frames[6];
    for(unsigned i=0;i<6;i++) {
        state=(easycon_script_status_t){0}; input.buttons=0; input.directions=0;
        if(i==1 || i==2) {state.pc_seen=true;state.pc_age_ms=i==1?100:3000;}
        if(i>=3) {state.ecx=true;state.elapsed_ms=12500;state.state=EASYCON_SCRIPT_RUNNING;}
        if(i==1) {input.buttons=SWITCH_BUTTON_A|SWITCH_BUTTON_ZL; input.directions=3;}
        if(i==4) {state.state=EASYCON_SCRIPT_WAITING;state.wait_ms=1500;}
        if(i==5) {state.state=EASYCON_SCRIPT_ERROR;state.error_code=8;}
        oled_ui_render(frames[i],&state,&input,i!=0);
    }
    for(int y=0;y<128;y++) for(int x=0;x<384;x++)
        fputc(pixel(frames[(y/64)*3+x/128],x%128,y%64)?255:0,file);
    fclose(file);
}
int main(int argc,char **argv) {
    render();
    frame_t neutral; memcpy(neutral,guarded.frame,sizeof(neutral));
    // Region checks protect all 14 mappings, without mirroring font bitmaps.
    const int regions[14][4]={{105,50,7,7},{113,57,7,7},{121,50,7,7},{113,43,7,7},
        {19,36,7,7},{98,36,7,7},{0,36,13,7},{110,36,13,7},
        {39,36,7,7},{50,36,7,7},{36,45,19,19},{66,45,19,19},
        {63,36,7,7},{76,36,7,7}};
    for(unsigned b=0;b<14;b++) {
        input.buttons=1u<<b; render();
        check_changes(neutral,regions[b][0],regions[b][1],regions[b][2],regions[b][3]);
    }
    input.buttons=0;
    const int arrow_regions[4][2]={{10,44},{18,52},{10,57},{2,52}};
    for(unsigned i=0;i<4;i++) {
        input.directions=1u<<i; render();
        check_changes(neutral,arrow_regions[i][0],arrow_regions[i][1],7,7);
    }
    input.directions=0;
    input.report.LX=0;input.report.LY=0;input.report.RX=255;input.report.RY=255;
    render(); assert(pixel(guarded.frame,40,49) && pixel(guarded.frame,80,59));
    input.report.LX=input.report.LY=input.report.RX=input.report.RY=128;
    render(); assert(pixel(guarded.frame,45,54) && pixel(guarded.frame,75,54));
    frame_t usb; oled_ui_render(usb,&state,&input,true);
    assert(pixel(usb,123,3) && !pixel(guarded.frame,123,3));
    state.pc_seen=true;state.pc_age_ms=1999;render();
    frame_t active; memcpy(active,guarded.frame,sizeof(active));
    state.pc_age_ms=2000;render(); assert(memcmp(active,guarded.frame,sizeof(active)));
    state=(easycon_script_status_t){.state=EASYCON_SCRIPT_RUNNING,.ecx=true,.pc_seen=true};
    render(); frame_t running;memcpy(running,guarded.frame,sizeof(running));
    state.pc_seen=false;render();assert(!memcmp(running,guarded.frame,sizeof(running)));
    for(int s=0;s<=EASYCON_SCRIPT_ERROR;s++) {
        state.state=s; state.elapsed_ms=UINT32_MAX; state.wait_ms=UINT32_MAX;
        for(int e=0;e<=14;e++) {state.error_code=e;render();}
    }
    state.ecx=false;state.state=EASYCON_SCRIPT_FINISHED;state.end_address=0;render();
    frame_t empty;memcpy(empty,guarded.frame,sizeof(empty));
    state.ecx=true;render();assert(memcmp(empty,guarded.frame,sizeof(empty)));
    state.state=EASYCON_SCRIPT_ERROR;state.error_code=INT_MIN;render();
    state.state=(easycon_script_state_t)-1;render();
    if(argc==3 && !strcmp(argv[1],"--preview")) preview(argv[2]);
    puts("OLED layout: state modes, input regions, axes, USB and bounds passed");
}
