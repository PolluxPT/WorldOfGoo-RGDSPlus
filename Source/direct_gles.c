/* Direct GLES compatibility for World of Goo. MIT 2026 PolluxPT. */
#include <limits.h>

static void (*gles_source)(unsigned, int, const char *const *, const int *);
static void (*gles_compile)(unsigned);
static void (*gles_link)(unsigned);
static void (*gles_clear_depth)(float);
static void (*gles_tex_image)(unsigned,int,int,int,int,int,unsigned,unsigned,const void *);

static void shader_source(unsigned shader, int count, const char *const *parts, const int *lengths)
{
    static const char prefix[] = "#version 100\nprecision highp float;\nprecision mediump int;\n";
    size_t size = 0;
    for (int i = 0; i < count; ++i) {
        size_t length = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(parts[i]);
        if (length > INT_MAX - size - sizeof(prefix)) _exit(8);
        size += length;
    }
    char *source = malloc(size + 1);
    if (!source) _exit(8);
    size_t position = 0;
    for (int i = 0; i < count; ++i) {
        size_t length = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(parts[i]);
        memcpy(source + position, parts[i], length);
        position += length;
    }
    source[size] = '\0';
    const char *body = source;
    while (*body == ' ' || *body == '\t' || *body == '\n' || *body == '\r') ++body;
    if (!strncmp(body, "#version", 8)) {
        const char *end = strchr(body, '\n');
        body = end ? end + 1 : body + strlen(body);
    }
    const char *converted[] = {prefix, body};
    gles_source(shader, 2, converted, NULL);
    free(source);
}

static void check_shader_or_program(unsigned object, int program)
{
    const char *status_name = program ? "glGetProgramiv" : "glGetShaderiv";
    const char *log_name = program ? "glGetProgramInfoLog" : "glGetShaderInfoLog";
    void (*get_status)(unsigned,unsigned,int *) = sdl_proc(status_name);
    void (*get_log)(unsigned,int,int *,char *) = sdl_proc(log_name);
    int success = 0;
    if (get_status) get_status(object, program ? 0x8b82 : 0x8b81, &success);
    if (success) return;
    char log[2048] = {0};
    if (get_log) get_log(object, sizeof(log), NULL, log);
    fprintf(stderr, "GOO_GLES: %s failed: %s\n", program ? "link" : "compile", log);
    _exit(8);
}

static void compile_shader(unsigned shader)
{
    gles_compile(shader);
    check_shader_or_program(shader, 0);
}

static void link_program(unsigned program)
{
    gles_link(program);
    check_shader_or_program(program, 1);
    fprintf(stderr, "GOO_GLES: shader program linked\n");
}

static void clear_depth(double depth)
{
    gles_clear_depth((float)depth);
}

static void tex_image(unsigned target,int level,int internal,int width,int height,
                      int border,unsigned format,unsigned type,const void *data)
{
    // GLES2 requires identical unsized internal/external formats for these textures.
    if ((internal == 0x8058 && format == 0x1908) ||
        (internal == 0x8051 && format == 0x1907)) internal = format;
    // The original @2x assets remain available to the game. Keep their UV
    // geometry. Keep small sprites and UI art sharp, and halve large uploads.
    if (target==0x0de1 && level==0 && data && !border && format==0x1908 &&
        internal==0x1908 && type==0x1401 && width>=128 && height>=128 &&
        (width>256 || height>256) && width<=4096 && height<=4096) {
        int w=width/2, h=height/2;
        unsigned char *reduced=malloc((size_t)w*h*4);
        if (reduced) {
            const unsigned char *source=data;
            for (int y=0;y<h;y++) for(int x=0;x<w;x++) {
                const unsigned char *p=source+((size_t)y*2*width+x*2)*4;
                unsigned char *q=reduced+((size_t)y*w+x)*4;
                unsigned alpha=p[3]+p[7]+p[width*4+3]+p[width*4+7];
                for (int c=0;c<3;c++)
                    q[c]=alpha ? (p[c]*p[3]+p[c+4]*p[7]+
                        p[width*4+c]*p[width*4+3]+p[width*4+c+4]*p[width*4+7]+alpha/2)/alpha : 0;
                q[3]=(alpha+2)/4;
            }
            gles_tex_image(target,level,internal,w,h,border,format,type,reduced);
            free(reduced);
            return;
        }
    }
    gles_tex_image(target,level,internal,width,height,border,format,type,data);
}

static void *direct_proc(const char *name)
{
    if (!strcmp(name,"glClearDepth")) {
        gles_clear_depth = sdl_proc("glClearDepthf");
        return gles_clear_depth ? clear_depth : NULL;
    }
    void *address = sdl_proc(name);
    if (!address) return NULL;
    if (!strcmp(name,"glShaderSource")) { gles_source = address; return shader_source; }
    if (!strcmp(name,"glCompileShader")) { gles_compile = address; return compile_shader; }
    if (!strcmp(name,"glLinkProgram")) { gles_link = address; return link_program; }
    if (!strcmp(name,"glTexImage2D")) { gles_tex_image = address; return tex_image; }
    return address;
}
