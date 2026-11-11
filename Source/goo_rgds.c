/* World of Goo RGDS+ integration. MIT license, 2026. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
static void*sdl; static volatile uint32_t*state;
static void*fn(const char*n){if(!sdl)sdl=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);void*p=sdl?dlsym(sdl,n):NULL;if(!p){fprintf(stderr,"GOO missing SDL %s\n",n);_exit(4);}return p;}
static void init(void){static int tried;if(tried)return;tried=1;const char*p=getenv("GOO_INPUT_STATE");if(p){int f=open(p,O_RDWR);if(f>=0){void*m=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_SHARED,f,0);close(f);if(m!=MAP_FAILED)state=m;}}}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int(*mixvol)(int,int);static int desired[257];static int mixinit;
/* SDL_Event is 56 bytes on the Linux x86_64 target. */
typedef union{uint32_t type;uint8_t b[56];struct{uint32_t type,time,window,which,buttons;int32_t x,y,dx,dy;} motion;struct{uint32_t type,time,window,which;uint8_t button,state,clicks,pad;int32_t x,y;} button;struct{uint32_t type,time,window;uint8_t state,repeat,pad[2];int32_t scan,key;uint16_t mod;uint32_t unused;} key;} Event;
/* Use guest-side events so synthetic motion never crosses the Box64 SDL event
 * wrapper. The verified GOG binary drains SDL_PeepEvents one event at a time. */
static Event queued[272];
static unsigned queued_at, queued_count;
static float cursor_x, cursor_y;
static int cursor_ready, pointer_down;
static uint32_t game_window_id;

static void enqueue(Event event)
{
    if (queued_count == sizeof(queued)/sizeof(queued[0])) return;
    queued[(queued_at + queued_count++) % (sizeof(queued)/sizeof(queued[0]))] = event;
}

static void pointer_motion(uint32_t window, float x, float y, int width, int height)
{
    int previous_x = cursor_x, previous_y = cursor_y;
    cursor_x = x < 0 ? 0 : x > width-1 ? width-1 : x;
    cursor_y = y < 0 ? 0 : y > height-1 ? height-1 : y;
    Event event = {0};
    event.motion.type = 0x400;
    event.motion.window = window;
    event.motion.buttons = pointer_down;
    event.motion.x = cursor_x;
    event.motion.y = cursor_y;
    event.motion.dx = event.motion.x - previous_x;
    event.motion.dy = event.motion.y - previous_y;
    enqueue(event);
}

static void pointer_button(uint32_t window, int down)
{
    if (pointer_down == down) return;
    pointer_down = down;
    Event event = {0};
    event.button.type = down ? 0x401 : 0x402;
    event.button.window = window;
    event.button.button = 1;
    event.button.state = down;
    event.button.clicks = 1;
    event.button.x = cursor_x;
    event.button.y = cursor_y;
    enqueue(event);
}

#include "name_keys.c"
static void inject(void)
{
    static double previous;
    static uint32_t old;
    static int quit;
    init();
    if (!state) return;
    double time = now(), dt = previous ? time-previous : .016;
    if (dt < .008 || queued_count) return;
    previous = time;
    if (dt > .05) dt = .05;
    void *(*current_window)(void) = fn("SDL_GL_GetCurrentWindow");
    void *window = current_window();
    if (!window) return;
    uint32_t (*window_id)(void *) = fn("SDL_GetWindowID");
    void (*size)(void *, int *, int *) = fn("SDL_GetWindowSize");
    uint32_t id = window_id(window), bits = state[0];
    game_window_id = id;
    int width = 1024, height = 768;
    size(window, &width, &height);
    if (width < 2 || height < 2) return;
    if (!cursor_ready) {
        cursor_x = width/2;
        cursor_y = height/2;
        cursor_ready = 1;
    }
    const char *exitpath = getenv("GOO_EXIT_REQUEST");
    if (!quit && exitpath && access(exitpath, F_OK)==0) {
        enqueue((Event){.type=0x100});
        quit = 1;
    }

    if (name_keyboard(id,bits,old)) {
        old=bits;
        return;
    }
    int button_a = (bits >> 16)&1;
    float ax, ay;
    uint32_t ux = state[2], uy = state[3];
    memcpy(&ax, &ux, 4);
    memcpy(&ay, &uy, 4);
    if (!(ax >= -1 && ax <= 1)) ax = 0;
    if (!(ay >= -1 && ay <= 1)) ay = 0;
    if (bits & (3u<<20)) ax = (float)!!(bits&(1u<<21)) - !!(bits&(1u<<20));
    if (bits & (3u<<22)) ay = (float)!!(bits&(1u<<23)) - !!(bits&(1u<<22));
    if (ax != 0 || ay != 0) {
        float speed = bits&(1u<<24) ? 150 : 600;
        if (ax != 0 && ay != 0) speed *= .70710678f;
        pointer_motion(id,cursor_x+ax*speed*dt,cursor_y+ay*speed*dt,width,height);
    }
    if (button_a && !pointer_down)
        pointer_motion(id,cursor_x,cursor_y,width,height);
    pointer_button(id,button_a);

    const uint32_t start=1u<<27;
    if ((old^bits)&start) {
        Event event={0};
        event.key.type=bits&start?0x300:0x301;
        event.key.window=id;
        event.key.state=(bits&start)!=0;
        event.key.scan=41;
        event.key.key=27;
        enqueue(event);
    }
    old = bits;
}

int SDL_GetRelativeMouseMode(void)
{
    return 0;
}

int SDL_SetRelativeMouseMode(int enabled)
{
    (void)enabled;
    int (*real)(int) = fn("SDL_SetRelativeMouseMode");
    return real(0);
}

void SDL_PumpEvents(void)
{
    void (*real)(void) = fn("SDL_PumpEvents");
    real();
    inject();
}

static int duplicate_input(const Event *event)
{
    // The separate upper presenter can take Wayland keyboard focus from the
    // render window. This game's focus handlers pause and resume its mixer.
    if (event->type == 0x200 && (event->b[12] == 12 || event->b[12] == 13)) {
        const char *frames=getenv("GOO_MIRROR_FRAME");
        if (frames && access(frames,F_OK)==0) return 1;
    }
    if (game_window_id && event->motion.window && event->motion.window != game_window_id &&
        (event->type == 0x200 || (event->type >= 0x300 && event->type <= 0x303) ||
         (event->type >= 0x400 && event->type <= 0x403))) return 1;
    if (event->type >= 0x600 && event->type <= 0x65f) return 1;
    if (event->type >= 0x700 && event->type <= 0x702) return 1;
    if (event->type >= 0x400 && event->type <= 0x402)
        return 1;
    if (event->type == 0x300 || event->type == 0x301)
        return event->key.scan >= 79 && event->key.scan <= 82;
    return 0;
}

int SDL_PeepEvents(void *events, int n, int action, uint32_t min, uint32_t max)
{
    int (*real)(void *, int, int, uint32_t, uint32_t) = fn("SDL_PeepEvents");
    if (!events || n <= 0 || action != 2 || min != 0 || max < 0xffff)
        return real(events,n,action,min,max);
    inject();
    Event *output = events;
    int written = 0;
    while (written < n && queued_count) {
        output[written++] = queued[queued_at];
        queued_at = (queued_at+1) % (sizeof(queued)/sizeof(queued[0]));
        --queued_count;
    }
    if (written == n) return written;
    int count = real(output+written,n-written,action,min,max);
    while (count > 0) {
        int kept = 0;
        for (int i=0; i<count; ++i) {
            Event *event = &output[written+i];
            if (!state || !duplicate_input(event)) output[written+kept++] = *event;
        }
        if (written || kept) return written+kept;
        count = real(output,n,action,min,max);
    }
    return written ? written : count;
}
static void*owned[32];
char*SDL_GetPrefPath(const char*org,const char*app){(void)org;(void)app;const char*p=getenv("GOO_PREF_PATH");if(!p)return NULL;size_t n=strlen(p);char*r=malloc(n+2);if(!r)return NULL;memcpy(r,p,n);r[n]='/';r[n+1]=0;for(int i=0;i<32;i++)if(!owned[i]){owned[i]=r;return r;}free(r);return NULL;}
void SDL_free(void*p){for(int i=0;i<32;i++)if(p&&owned[i]==p){owned[i]=0;free(p);return;}void(*real)(void*)=fn("SDL_free");real(p);}
char*getenv(const char*n){static char*(*real)(const char*);if(!real)real=dlsym(RTLD_NEXT,"getenv");if(!real)return NULL;if(!strcmp(n,"HOME")){char*p=real("GOO_SAVED_HOME");if(p)return p;}return real(n);}
