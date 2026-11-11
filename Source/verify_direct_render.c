#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void SDL_GL_SwapWindow(void *window) {
    static unsigned count;
    void (*real)(void *) = dlsym(RTLD_NEXT,"SDL_GL_SwapWindow");
    real(window);
    if (++count == 60) { fprintf(stderr,"PASS: rendered 60 frames\n"); exit(0); }
}
int SDL_ShowSimpleMessageBox(unsigned flags,const char *title,const char *message,void *window) {
    fprintf(stderr,"GAME DIALOG: %s: %s\n",title,message);
    return 0;
}
static void forbidden_fbo(int n, unsigned *ids) {
    fprintf(stderr,"FAIL: game attempted to allocate an FBO\n");
    exit(9);
}
void *SDL_GL_GetProcAddress(const char *name) {
    void *(*real)(const char *) = dlsym(RTLD_NEXT,"SDL_GL_GetProcAddress");
    if (!strcmp(name,"glGenFramebuffers")) return forbidden_fbo;
    return real(name);
}
