/* Publish lower-screen GLES frames for the independent Wayland mirror. */
#include "mirror_frame.h"
#include <fcntl.h>
#include <sys/mman.h>
static void *mirror_sdl;
static GooMirrorFrame *mirror_pixels;
static int mirror_failed;
static uint32_t mirror_deadline;
static int notify_fd=-1;
static uint32_t (*ticks)(void);
static void (*read_pixels)(int,int,int,int,unsigned,unsigned,void *);

static void mirror_frame(void *lower)
{
    if (mirror_failed || !window_display ||
        window_display(lower)!=1 || !drawable_size || !sdl_proc || !mirror_sdl) return;
    if (!ticks) ticks=lookup(mirror_sdl,"SDL_GetTicks");
    if (!read_pixels) read_pixels=sdl_proc("glReadPixels");
    if (!ticks || !read_pixels) return;
    uint32_t now=ticks();
    if (mirror_pixels && (int32_t)(now-mirror_deadline)<0) return;
    // Keep the 30 fps sampling phase even when the game's swap times jitter.
    if (!mirror_pixels || now-mirror_deadline>33) mirror_deadline=now;
    mirror_deadline+=33;
    int w=0,h=0;
    drawable_size(lower,&w,&h);
    if (w!=GOO_MIRROR_W || h!=GOO_MIRROR_H) return;
    if (!mirror_pixels) {
        const char *path=getenv("GOO_MIRROR_FRAME");
        if (!path) return;
        int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC,0600);
        if (fd<0 || ftruncate(fd,sizeof(GooMirrorFrame))<0) {
            if (fd>=0) close(fd);
            mirror_failed=1;
            return;
        }
        void *map=mmap(NULL,sizeof(GooMirrorFrame),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
        close(fd);
        if (map==MAP_FAILED) { mirror_failed=1; return; }
        mirror_pixels=map;
        const char *notify=getenv("GOO_MIRROR_NOTIFY");
        if (notify) notify_fd=open(notify,O_RDWR|O_NONBLOCK|O_CLOEXEC);
        int bounds[4]={0};
        int (*get_bounds)(int,void *)=lookup(mirror_sdl,"SDL_GetDisplayBounds");
        const char *(*name)(int)=lookup(mirror_sdl,"SDL_GetDisplayName");
        if (get_bounds && get_bounds(0,bounds)==0) {
            mirror_pixels->output_x=bounds[0];
            mirror_pixels->output_y=bounds[1];
        }
        const char *label=name?name(0):NULL;
        if (label) snprintf(mirror_pixels->output_name,sizeof(mirror_pixels->output_name),"%s",label);
        fprintf(stderr,"GOO_MIRROR: publishing lower frames for output %s (%d,%d)\n",
                mirror_pixels->output_name,bounds[0],bounds[1]);
    }
    __atomic_add_fetch(&mirror_pixels->sequence,1,__ATOMIC_SEQ_CST);
    read_pixels(0,0,w,h,0x1908,0x1401,mirror_pixels->rgba);
    __atomic_add_fetch(&mirror_pixels->sequence,1,__ATOMIC_SEQ_CST);
    if (notify_fd>=0) (void)write(notify_fd,"F",1);
}
