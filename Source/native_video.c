/* RGDS+ Box64 SDL and direct GLES bridge, MIT 2026 PolluxPT. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <link.h>
#include <elf.h>
static void*(*lookup)(void*,const char*);
static void*(*open_library)(const char*,int);
static void*bridge_dlsym(void*,const char*);
static int active=-1;
static int (*display_count)(void);
static int (*window_display)(void *);
static int (*message_box)(uint32_t, const char *, const char *, void *);

static int log_message_box(uint32_t flags, const char *title, const char *message, void *window)
{
    fprintf(stderr, "GOO_DIALOG: %s: %s\n", title ? title : "", message ? message : "");
    return message_box(flags, title, message, window);
}
static int(*set_attribute)(int,int);
static void*(*create_context)(void*);
static void*(*create_window)(const char*,int,int,int,int,uint32_t);
static void(*swap_window)(void*);
static void*(*sdl_proc)(const char*);
static void(*show_window)(void*);
static void(*drawable_size)(void*,int*,int*);
/* Resolve libc/libdl directly; the executable exports its own dlsym/dlopen. */
static int loader_symbols(struct dl_phdr_info*info,size_t size,void*unused){
 (void)size;(void)unused;const char*name=info->dlpi_name;
 if(!name||(!strstr(name,"libc.so")&&!strstr(name,"libdl.so")))return 0;
 ElfW(Dyn)*dyn=NULL;for(int i=0;i<info->dlpi_phnum;i++)if(info->dlpi_phdr[i].p_type==PT_DYNAMIC)dyn=(void*)(info->dlpi_addr+info->dlpi_phdr[i].p_vaddr);
 if(!dyn)return 0;ElfW(Sym)*symbols=NULL;const char*strings=NULL;uint32_t*hash=NULL,*gnu_hash=NULL;
 for(;dyn->d_tag!=DT_NULL;dyn++){
  uintptr_t p=dyn->d_un.d_ptr;if(p<info->dlpi_addr)p+=info->dlpi_addr;
  if(dyn->d_tag==DT_SYMTAB)symbols=(void*)p;
  else if(dyn->d_tag==DT_STRTAB)strings=(void*)p;
  else if(dyn->d_tag==DT_HASH)hash=(void*)p;
  else if(dyn->d_tag==DT_GNU_HASH)gnu_hash=(void*)p;
 }
 if(!symbols||!strings||(!hash&&!gnu_hash))return 0;
 unsigned count=hash?hash[1]:0;
 if(!count&&gnu_hash){
  uint32_t*buckets=(void*)((ElfW(Addr)*)(gnu_hash+4)+gnu_hash[2]);
  uint32_t*chains=buckets+gnu_hash[0];unsigned highest=0;
  for(unsigned i=0;i<gnu_hash[0];i++)if(buckets[i]>highest)highest=buckets[i];
  if(!highest)count=gnu_hash[1];else{while(!(chains[highest-gnu_hash[1]]&1))highest++;count=highest+1;}
 }
 for(unsigned i=0;i<count;i++){
  if(symbols[i].st_shndx==SHN_UNDEF)continue;
  const char*n=strings+symbols[i].st_name;void*p=(void*)(info->dlpi_addr+symbols[i].st_value);
  if(!strcmp(n,"dlsym"))lookup=p;
  else if(!strcmp(n,"dlopen"))open_library=p;
 }
 return lookup&&open_library;
}
static void resolve(void){
 if(!lookup||!open_library){dl_iterate_phdr(loader_symbols,NULL);if(!lookup||!open_library)_exit(8);}
 if(active<0){const char*p=getenv("GOO_NATIVE_GLES");active=p&&!strcmp(p,"1");}
}
/* This core has its own native dlsym dispatcher. Install the forwarding
 * callback in its pinned writable cache, after checking the mapped executable
 * and dispatcher bytes. The launcher also verifies the complete core hash. */
static int core_cache(struct dl_phdr_info*info,size_t size,void*unused){
 (void)size;(void)unused;if(info->dlpi_name&&*info->dlpi_name)return 0;
 const uintptr_t entry=0x35379050u,cache=0x36032ef8u;int code=0,data=0;
 for(int i=0;i<info->dlpi_phnum;i++){
  const ElfW(Phdr)*p=info->dlpi_phdr+i;uintptr_t lo=info->dlpi_addr+p->p_vaddr,hi=lo+p->p_memsz;
  if(p->p_type!=PT_LOAD)continue;
  if(entry>=lo&&entry+16<=hi&&(p->p_flags&PF_X))code=1;
  if(cache>=lo&&cache+sizeof(void*)<=hi&&(p->p_flags&PF_W))data=1;
 }
 if(!code||!data)return 0;
 static const unsigned char expected[16]={0xfd,0x7b,0xbd,0xa9,0xf5,0x0b,0x00,0xf9,0xf4,0x4f,0x02,0xa9,0xfd,0x03,0x00,0x91};
 if(memcmp((void*)entry,expected,16)){fprintf(stderr,"GOO_VIDEO64: unsupported translator dispatcher\n");_exit(8);}
 *(void**)(uintptr_t)cache=bridge_dlsym;
 fprintf(stderr,"GOO_VIDEO64: pinned native SDL lookup bridge installed\n");return 1;
}
__attribute__((constructor)) static void video_loaded(void){resolve();if(active){fprintf(stderr,"GOO_VIDEO64: native bridge loaded\n");dl_iterate_phdr(core_cache,NULL);unsetenv("LD_PRELOAD");}}
#include "direct_gles.c"
#include "pcm_volume.c"
static void attributes(void){if(set_attribute){set_attribute(17,2);set_attribute(18,0);set_attribute(21,4);}}
static int video_attribute(int attr,int value){if(attr==17)value=2;else if(attr==18)value=0;else if(attr==21)value=4;return set_attribute(attr,value);}
static void*video_window(const char*title,int x,int y,int w,int h,uint32_t flags){
 attributes();
 int displays=display_count?display_count():-1;
 flags=(flags&~12u)|4u|4097u;w=1024;h=768;
 x=y=0x2fff0001;
 fprintf(stderr,"GOO_VIDEO64: SDL displays=%d target=1\n",displays);
 void*window=create_window(title,x,y,w,h,flags);fprintf(stderr,"GOO_VIDEO64: window=%p %dx%d flags=%x\n",window,w,h,flags);return window;
}
static void*video_context(void*window){
 attributes();void*context=create_context(window);if(!context)return NULL;
 fprintf(stderr,"GOO_GLES: native SDL GLES2 context ready\n");
 return context;
}
#include "name_overlay.c"
#include "mirror.c"
static void video_swap(void*window){
 static unsigned frames;
 if(!frames&&show_window)show_window(window);
 if(!frames&&drawable_size){int w=0,h=0;drawable_size(window,&w,&h);fprintf(stderr,"GOO_VIDEO64: first frame drawable=%dx%d\n",w,h);}
 name_overlay(window);
 mirror_frame(window);
 swap_window(window);
 if(!frames++){
  int display=window_display?window_display(window):-1;
  fprintf(stderr,"GOO_VIDEO64: first swap on SDL display %d\n",display);

 }
}
static void*bridge_dlsym(void*handle,const char*name){
 resolve();void*p=lookup(handle,name);if(!active||!p)return p;
 if(!strcmp(name,"Mix_OpenAudio")){
  real_mixer_open=p;
  set_postmix=lookup(handle,"Mix_SetPostMix");
  query_mixer=lookup(handle,"Mix_QuerySpec");
  return open_mixer;
 }
 if(!strcmp(name,"SDL_GL_SetAttribute")){set_attribute=p;return video_attribute;}
 if(!strcmp(name,"SDL_CreateWindow")){
  create_window=p;
  display_count=lookup(handle,"SDL_GetNumVideoDisplays");
  window_display=lookup(handle,"SDL_GetWindowDisplayIndex");
  set_attribute=lookup(handle,"SDL_GL_SetAttribute");return video_window;
 }
 if(!strcmp(name,"SDL_GL_CreateContext")){fprintf(stderr,"GOO_VIDEO64: native SDL context lookup intercepted\n");
  create_context=p;set_attribute=lookup(handle,"SDL_GL_SetAttribute");return video_context;
 }
 if(!strcmp(name,"SDL_ShowSimpleMessageBox")){message_box=p;return log_message_box;}
 if(!strcmp(name,"SDL_GL_GetProcAddress")){sdl_proc=p;return direct_proc;}
 if(!strcmp(name,"SDL_GL_SwapWindow")){
  mirror_sdl=handle;
  swap_window=p;show_window=lookup(handle,"SDL_ShowWindow");drawable_size=lookup(handle,"SDL_GL_GetDrawableSize");return video_swap;
 }
 return p;
}

void*dlsym(void*handle,const char*name){return bridge_dlsym(handle,name);}
