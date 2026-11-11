#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "mirror_frame.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    struct wl_output *handle;
    int x,y,geometry;
    char name[96];
} Output;
typedef struct {
    struct wl_buffer *handle;
    uint32_t *pixels;
    int busy;
} Buffer;
static Output outputs[8];
static unsigned output_count;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *shell;
static int configured, closed, rgba_direct, frame_pending;
static int entered_upper, frame_seen, ready_written;
static struct wl_output *upper_output;
static const char *ready_path;

static void report_ready(void)
{
    if (!entered_upper || !frame_seen || ready_written) return;
    int fd=open(ready_path,O_WRONLY|O_CREAT|O_CLOEXEC,0600);
    if (fd<0) return;
    close(fd);
    ready_written=1;
    fprintf(stderr,"Upper presenter: first frame acknowledged on target output\n");
}

static void surface_enter(void *data,struct wl_surface *surface,struct wl_output *output)
{
    if (output!=upper_output) return;
    entered_upper=1;
    report_ready();
}
static void surface_leave(void *data,struct wl_surface *surface,struct wl_output *output)
{
    if (output!=upper_output) return;
    entered_upper=frame_seen=ready_written=0;
    unlink(ready_path);
}
static const struct wl_surface_listener output_tracking={
    .enter=surface_enter,.leave=surface_leave
};
static void shm_format(void *data,struct wl_shm *handle,uint32_t format)
{
    if (format==WL_SHM_FORMAT_XBGR8888) rgba_direct=1;
}
static const struct wl_shm_listener shm_listener={shm_format};
static void frame_done(void *data,struct wl_callback *callback,uint32_t time)
{
    wl_callback_destroy(callback);
    frame_pending=0;
    frame_seen=1;
    report_ready();
}
static const struct wl_callback_listener frame_listener={frame_done};
static void geometry(void *data,struct wl_output *o,int x,int y,int pw,int ph,
                     int sub,const char *make,const char *model,int transform)
{
    Output *out=data;
    out->x=x;out->y=y;out->geometry=1;
}
static void mode(void *d,struct wl_output *o,uint32_t f,int w,int h,int refresh){}
static void done(void *d,struct wl_output *o){}
static void scale(void *d,struct wl_output *o,int factor){}
static void name(void *d,struct wl_output *o,const char *n)
{
    Output *out=d;
    snprintf(out->name,sizeof(out->name),"%s",n);
}
static void description(void *d,struct wl_output *o,const char *n){}
static const struct wl_output_listener output_listener={geometry,mode,done,scale,name,description};
static void global(void *d,struct wl_registry *registry,uint32_t id,const char *interface,uint32_t version)
{
    if (!strcmp(interface,"wl_compositor"))
        compositor=wl_registry_bind(registry,id,&wl_compositor_interface,1);
    else if (!strcmp(interface,"wl_shm")) {
        shm=wl_registry_bind(registry,id,&wl_shm_interface,1);
        wl_shm_add_listener(shm,&shm_listener,NULL);
    }
    else if (!strcmp(interface,"xdg_wm_base"))
        shell=wl_registry_bind(registry,id,&xdg_wm_base_interface,1);
    else if (!strcmp(interface,"wl_output") && output_count<8) {
        Output *out=&outputs[output_count++];
        out->handle=wl_registry_bind(registry,id,&wl_output_interface,version>4?4:version);
        wl_output_add_listener(out->handle,&output_listener,out);
    }
}
static void removed(void *d,struct wl_registry *r,uint32_t id){}
static const struct wl_registry_listener registry_listener={global,removed};
static void ping(void *d,struct xdg_wm_base *wm,uint32_t serial) {xdg_wm_base_pong(wm,serial);}
static const struct xdg_wm_base_listener shell_listener={ping};
static void configure(void *d,struct xdg_surface *surface,uint32_t serial)
{
    xdg_surface_ack_configure(surface,serial);
    configured=1;
}
static const struct xdg_surface_listener surface_listener={configure};
static void toplevel_configure(void *d,struct xdg_toplevel *t,int w,int h,struct wl_array *states){}
static void toplevel_close(void *d,struct xdg_toplevel *t) {closed=1;}
static const struct xdg_toplevel_listener toplevel_listener={.configure=toplevel_configure,.close=toplevel_close};
static void release(void *d,struct wl_buffer *buffer) {((Buffer *)d)->busy=0;}
static const struct wl_buffer_listener buffer_listener={release};

static Output *target_output(const GooMirrorFrame *frame)
{
    Output *selected=NULL;
    int best=0, tied=0;
    for (unsigned i=0;i<output_count;i++) {
        Output *out=&outputs[i];
        int score=0;
        if (out->name[0] && !strcmp(frame->output_name,out->name)) score+=2;
        if (out->geometry && frame->output_x==out->x && frame->output_y==out->y) score++;
        if (score>best) {selected=out;best=score;tied=0;}
        else if (score && score==best) tied=1;
    }
    return tied?NULL:selected;
}

static int copy_frame(const GooMirrorFrame *frame,Buffer *buffer,uint32_t *last)
{
    uint32_t sequence=__atomic_load_n(&frame->sequence,__ATOMIC_ACQUIRE);
    if (!sequence || sequence==*last || sequence&1) return 0;
    for (int y=0;y<GOO_MIRROR_H;y++) {
        const unsigned char *src=frame->rgba+(GOO_MIRROR_H-1-y)*GOO_MIRROR_W*4;
        uint32_t *dst=buffer->pixels+y*GOO_MIRROR_W;
        if (rgba_direct) memcpy(dst,src,GOO_MIRROR_W*4);
        else for (int x=0;x<GOO_MIRROR_W;x++,src+=4)
            dst[x]=0xff000000u | (uint32_t)src[0]<<16 | (uint32_t)src[1]<<8 | src[2];
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (__atomic_load_n(&frame->sequence,__ATOMIC_ACQUIRE)!=sequence) return 0;
    *last=sequence;
    return 1;
}

int main(int argc,char **argv)
{
    if (argc!=3) return 2;
    ready_path=argv[2];
    int source=open(argv[1],O_RDONLY|O_CLOEXEC);
    struct stat st;
    if (source<0 || fstat(source,&st)<0 || st.st_size!=sizeof(GooMirrorFrame)) return 2;
    const GooMirrorFrame *frame=mmap(NULL,sizeof(*frame),PROT_READ,MAP_SHARED,source,0);
    close(source);
    if (frame==MAP_FAILED) return 2;
    for (int attempt=0;attempt<100;attempt++) {
        uint32_t sequence=__atomic_load_n(&frame->sequence,__ATOMIC_ACQUIRE);
        if (sequence && !(sequence&1)) break;
        usleep(10000);
    }
    if (!__atomic_load_n(&frame->sequence,__ATOMIC_ACQUIRE)) return 2;
    struct wl_display *display=wl_display_connect(NULL);
    if (!display) {fprintf(stderr,"Mirror: could not connect to Wayland\n");return 3;}
    struct wl_registry *registry=wl_display_get_registry(display);
    wl_registry_add_listener(registry,&registry_listener,NULL);
    if (wl_display_roundtrip(display)<0 || wl_display_roundtrip(display)<0 ||
        !compositor || !shm || !shell || output_count<2) return 3;
    Output *output=target_output(frame);
    if (!output) {
        fprintf(stderr,"Mirror: could not identify upper output %s (%d,%d)\n",
                frame->output_name,frame->output_x,frame->output_y);
        return 3;
    }
    fprintf(stderr,"Mirror: explicit Wayland upper output %s (%d,%d)\n",output->name,output->x,output->y);
    xdg_wm_base_add_listener(shell,&shell_listener,NULL);
    upper_output=output->handle;
    struct wl_surface *surface=wl_compositor_create_surface(compositor);
    wl_surface_add_listener(surface,&output_tracking,NULL);
    struct xdg_surface *xdg=xdg_wm_base_get_xdg_surface(shell,surface);
    xdg_surface_add_listener(xdg,&surface_listener,NULL);
    struct xdg_toplevel *toplevel=xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(toplevel,&toplevel_listener,NULL);
    xdg_toplevel_set_title(toplevel,"World of Goo - upper mirror");
    xdg_toplevel_set_fullscreen(toplevel,output->handle);
    wl_surface_commit(surface);
    int fd=memfd_create("worldofgoo-mirror",MFD_CLOEXEC);
    if (fd<0 || ftruncate(fd,2*GOO_MIRROR_BYTES)<0) return 4;
    unsigned char *pixels=mmap(NULL,2*GOO_MIRROR_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if (pixels==MAP_FAILED) return 4;
    struct wl_shm_pool *pool=wl_shm_create_pool(shm,fd,2*GOO_MIRROR_BYTES);
    close(fd);
    Buffer buffers[2]={0};
    for (int i=0;i<2;i++) {
        buffers[i].pixels=(void *)(pixels+i*GOO_MIRROR_BYTES);
        buffers[i].handle=wl_shm_pool_create_buffer(pool,i*GOO_MIRROR_BYTES,
            GOO_MIRROR_W,GOO_MIRROR_H,GOO_MIRROR_W*4,rgba_direct?WL_SHM_FORMAT_XBGR8888:WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(buffers[i].handle,&buffer_listener,&buffers[i]);
    }
    wl_shm_pool_destroy(pool);
    const char *notify=getenv("GOO_MIRROR_NOTIFY");
    int notify_fd=notify?open(notify,O_RDWR|O_NONBLOCK|O_CLOEXEC):-1;
    struct pollfd fds[2]={{wl_display_get_fd(display),POLLIN,0},{notify_fd,POLLIN,0}};
    uint32_t last=0;
    while (!closed) {
        if (wl_display_dispatch_pending(display)<0) break;
        if (configured && !frame_pending) for(int i=0;i<2;i++) {
            if (buffers[i].busy || !copy_frame(frame,&buffers[i],&last)) continue;
            buffers[i].busy=1;
            wl_surface_attach(surface,buffers[i].handle,0,0);
            wl_surface_damage(surface,0,0,GOO_MIRROR_W,GOO_MIRROR_H);
            struct wl_callback *callback=wl_surface_frame(surface);
            if (callback) {
                wl_callback_add_listener(callback,&frame_listener,NULL);
                frame_pending=1;
            }
            wl_surface_commit(surface);
            break;
        }
        fds[0].events=POLLIN;
        if (wl_display_flush(display)<0) {
            if (errno!=EAGAIN) break;
            fds[0].events|=POLLOUT;
        }
        int ready=poll(fds,2,notify_fd>=0?-1:8);
        if (ready<0 && errno==EINTR) continue;
        if (ready<0 || (fds[0].revents&(POLLERR|POLLHUP|POLLNVAL))) break;
        if (fds[1].revents&POLLIN) {
            char signals[256];
            while (read(notify_fd,signals,sizeof(signals))>0) {}
        }
        if ((fds[0].revents&POLLIN) && wl_display_dispatch(display)<0) break;
    }
    unlink(ready_path);
    wl_display_disconnect(display);
    return 0;
}
