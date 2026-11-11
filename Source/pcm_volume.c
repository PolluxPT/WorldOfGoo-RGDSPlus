/* Apply the Yokoi volume curve once, to final native signed 16-bit PCM. */
#include <fcntl.h>
#include <sys/mman.h>
static const uint32_t *volume_mailbox;
static int (*real_mixer_open)(int,uint16_t,int,int);
static void (*set_postmix)(void (*)(void *,unsigned char *,int),void *);
static int (*query_mixer)(int *,uint16_t *,int *);

static void scale_pcm(void *unused,unsigned char *stream,int bytes)
{
    (void)unused;
    unsigned level=volume_mailbox ? __atomic_load_n(volume_mailbox+1,__ATOMIC_RELAXED) : 2;
    if (level>10) level=10;
    // 0.35 * (level/10)^2 = 7 * level^2 / 2000.
    int numerator=(int)(7*level*level);
    if (!numerator) { memset(stream,0,(size_t)bytes);return; }
    int16_t *pcm=(int16_t *)stream;
    for (int i=0;i<bytes/2;i++) {
        int product=pcm[i]*numerator;
        pcm[i]=(int16_t)((product+(product<0?-1000:1000))/2000);
    }
}

static int open_mixer(int rate,uint16_t format,int channels,int frames)
{
    int buffered=frames<4096?4096:frames;
    int result=real_mixer_open(rate,format,channels,buffered);
    if (result) return result;
    uint16_t actual=format;
    if (query_mixer) query_mixer(NULL,&actual,NULL);
    if (actual!=0x8010 || !set_postmix) {
        fprintf(stderr,"GOO_AUDIO: unsupported postmix format 0x%x\n",actual);
        return result;
    }
    if (!volume_mailbox) {
        const char *path=getenv("GOO_INPUT_STATE");
        int fd=path?open(path,O_RDONLY|O_CLOEXEC):-1;
        if (fd>=0) {
            void *mapped=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,0);
            close(fd);
            if (mapped!=MAP_FAILED) volume_mailbox=mapped;
        }
    }
    set_postmix(scale_pcm,NULL);
    fprintf(stderr,"GOO_AUDIO: Yokoi PCM curve 0.35*(level/10)^2, %d Hz, buffer %d\n",rate,buffered);
    return result;
}
