/* GLES overlay for name entry. Draw after the game, before presenting either screen. */
#include <fcntl.h>
#include <sys/mman.h>
static const unsigned char font[][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
};
static void (*kb_scissor)(int,int,int,int);
static void (*kb_clear)(unsigned);
static int kb_height;
static void kb_rect(int x,int y,int w,int h)
{
    kb_scissor(x,kb_height-y-h,w,h);
    kb_clear(0x4000);
}
static void kb_text(const char *label,int x,int y,int scale)
{
    for (;*label;label++,x+=6*scale) {
        if (*label<'A'||*label>'Z') continue;
        const unsigned char *glyph=font[*label-'A'];
        for (int row=0;row<7;row++) {
            for (int col=0;col<5;col++) {
                if (glyph[row]&(16>>col)) kb_rect(x+col*scale,y+row*scale,scale,scale);
            }
        }
    }
}
static void name_overlay(void *window)
{
    static const volatile uint32_t *mailbox;
    if (!mailbox) {
        const char *path=getenv("GOO_INPUT_STATE");
        int fd=path?open(path,O_RDONLY):-1;
        if (fd<0) return;
        void *mapped=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0);
        close(fd);
        if (mapped==MAP_FAILED) return;
        mailbox=mapped;
    }
    unsigned selected=mailbox[520];
    if (!selected || selected>28 || !sdl_proc || !drawable_size) return;
    int w=0,h=0;
    drawable_size(window,&w,&h);
    if (w<700 || h<460) return;
    kb_height=h;
    kb_scissor=sdl_proc("glScissor");
    kb_clear=sdl_proc("glClear");
    void (*color)(float,float,float,float)=sdl_proc("glClearColor");
    void (*get_int)(unsigned,int *)=sdl_proc("glGetIntegerv");
    void (*get_float)(unsigned,float *)=sdl_proc("glGetFloatv");
    void (*get_bool)(unsigned,unsigned char *)=sdl_proc("glGetBooleanv");
    unsigned char (*enabled)(unsigned)=sdl_proc("glIsEnabled");
    void (*enable)(unsigned)=sdl_proc("glEnable");
    void (*disable)(unsigned)=sdl_proc("glDisable");
    void (*mask)(unsigned char,unsigned char,unsigned char,unsigned char)=sdl_proc("glColorMask");
    if (!kb_scissor||!kb_clear||!color||!get_int||!get_float||!get_bool||!enabled||!enable||!disable||!mask) return;
    int box[4];
    float saved_color[4];
    unsigned char saved_mask[4];
    unsigned char scissor_on=enabled(0x0c11);
    get_int(0x0c10,box);
    get_float(0x0c22,saved_color);
    get_bool(0x0c23,saved_mask);
    enable(0x0c11);
    mask(1,1,1,1);
    int left=(w-676)/2, top=h-326;
    color(.04f,.05f,.09f,1);
    kb_rect(left,top,676,314);
    color(1,1,1,1);
    kb_text("ENTER NAME",left+22,top+12,3);
    for (int i=0;i<28;i++) {
        int x=left+16+(i%7)*92,y=top+45+(i/7)*52;
        if (i==(int)selected-1) color(.18f,.48f,.83f,1);
        else color(.15f,.18f,.26f,1);
        kb_rect(x,y,86,46);
        color(1,1,1,1);
        char single[2]={'A'+i,0};
        kb_text(i==26?"SPACE":i==27?"DEL":single,x+(i>=26?10:36),y+13,i>=26?2:3);
    }
    kb_text("A TYPE   B DELETE   START DONE   Y CLOSE",left+22,top+271,2);
    color(saved_color[0],saved_color[1],saved_color[2],saved_color[3]);
    mask(saved_mask[0],saved_mask[1],saved_mask[2],saved_mask[3]);
    kb_scissor(box[0],box[1],box[2],box[3]);
    if (!scissor_on) disable(0x0c11);
}
