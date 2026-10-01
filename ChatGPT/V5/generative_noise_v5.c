#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

/* Self-contained prototype: no SDL/FluidSynth headers required at build time.
   Runtime libraries are loaded dynamically. SDL2 uses the native Wayland backend.
*/

typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef int32_t Sint32;
typedef struct SDL_Window SDL_Window; typedef struct SDL_Renderer SDL_Renderer; typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Rect { int x,y,w,h; } SDL_Rect;

typedef int (*PFN_SDL_Init)(Uint32); typedef void (*PFN_SDL_Quit)(void);
typedef SDL_Window* (*PFN_SDL_CreateWindow)(const char*,int,int,int,int,Uint32);
typedef SDL_Renderer* (*PFN_SDL_CreateRenderer)(SDL_Window*,int,Uint32);
typedef SDL_Texture* (*PFN_SDL_CreateTexture)(SDL_Renderer*,Uint32,int,int,int);
typedef int (*PFN_SDL_UpdateTexture)(SDL_Texture*,const SDL_Rect*,const void*,int);
typedef int (*PFN_SDL_RenderClear)(SDL_Renderer*); typedef int (*PFN_SDL_RenderCopy)(SDL_Renderer*,SDL_Texture*,const SDL_Rect*,const SDL_Rect*);
typedef void (*PFN_SDL_RenderPresent)(SDL_Renderer*); typedef void (*PFN_SDL_DestroyTexture)(SDL_Texture*); typedef void (*PFN_SDL_DestroyRenderer)(SDL_Renderer*); typedef void (*PFN_SDL_DestroyWindow)(SDL_Window*);
typedef int (*PFN_SDL_PollEvent)(void*); typedef void (*PFN_SDL_Delay)(Uint32); typedef const char* (*PFN_SDL_GetError)(void);
typedef int (*PFN_SDL_SetHint)(const char*,const char*); typedef void (*PFN_SDL_ShowWindow)(SDL_Window*); typedef void (*PFN_SDL_RaiseWindow)(SDL_Window*); typedef void (*PFN_SDL_SetWindowPosition)(SDL_Window*,int,int); typedef const char* (*PFN_SDL_GetCurrentVideoDriver)(void);

static void *hsdl;
#define S(name) static PFN_SDL_##name p_##name
S(Init); S(Quit); S(CreateWindow); S(CreateRenderer); S(CreateTexture); S(UpdateTexture); S(RenderClear); S(RenderCopy); S(RenderPresent); S(DestroyTexture); S(DestroyRenderer); S(DestroyWindow); S(PollEvent); S(Delay); S(GetError); S(SetHint); S(ShowWindow); S(RaiseWindow); S(SetWindowPosition); S(GetCurrentVideoDriver);
#undef S

static int load_sdl(void){
    const char *names[]={"libSDL2-2.0.so.0","libSDL2-2.0.so",NULL};
    for(int i=0;names[i]&&!hsdl;i++) hsdl=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);
    if(!hsdl) return 0;
#define L(name) do{ *(void**)(&p_##name)=dlsym(hsdl,"SDL_" #name); if(!p_##name) return 0; }while(0)
    L(Init);L(Quit);L(CreateWindow);L(CreateRenderer);L(CreateTexture);L(UpdateTexture);L(RenderClear);L(RenderCopy);L(RenderPresent);L(DestroyTexture);L(DestroyRenderer);L(DestroyWindow);L(PollEvent);L(Delay);L(GetError);L(SetHint);L(ShowWindow);L(RaiseWindow);L(SetWindowPosition);L(GetCurrentVideoDriver);
#undef L
    return 1;
}

/* FluidSynth opaque ABI declarations. */
typedef struct fluid_settings_t fluid_settings_t; typedef struct fluid_synth_t fluid_synth_t; typedef struct fluid_audio_driver_t fluid_audio_driver_t; typedef struct fluid_sfont_t fluid_sfont_t; typedef struct fluid_preset_t fluid_preset_t;
typedef int (*PFN_fs_settings_setstr)(fluid_settings_t*,const char*,const char*);
typedef fluid_settings_t* (*PFN_fs_settings_new)(void); typedef void (*PFN_fs_settings_delete)(fluid_settings_t*);
typedef fluid_synth_t* (*PFN_fs_synth_new)(fluid_settings_t*); typedef void (*PFN_fs_synth_delete)(fluid_synth_t*);
typedef fluid_audio_driver_t* (*PFN_fs_audio_new)(fluid_settings_t*,fluid_synth_t*); typedef void (*PFN_fs_audio_delete)(fluid_audio_driver_t*);
typedef int (*PFN_fs_sfload)(fluid_synth_t*,const char*,int); typedef int (*PFN_fs_sfunload)(fluid_synth_t*,int,int);
typedef int (*PFN_fs_program_select)(fluid_synth_t*,int,int,int,int); typedef int (*PFN_fs_noteon)(fluid_synth_t*,int,int,int); typedef int (*PFN_fs_noteoff)(fluid_synth_t*,int,int);
typedef int (*PFN_fs_program_change)(fluid_synth_t*,int,int);
typedef fluid_sfont_t* (*PFN_fs_get_sfont_by_id)(fluid_synth_t*,int);
typedef void (*PFN_fs_iteration_start)(fluid_sfont_t*);
typedef fluid_preset_t* (*PFN_fs_iteration_next)(fluid_sfont_t*);
typedef int (*PFN_fs_preset_banknum)(fluid_preset_t*); typedef int (*PFN_fs_preset_num)(fluid_preset_t*);
static void *hfs; static PFN_fs_settings_setstr fs_settings_setstr; static PFN_fs_settings_new fs_settings_new; static PFN_fs_settings_delete fs_settings_delete;
static PFN_fs_synth_new fs_synth_new; static PFN_fs_synth_delete fs_synth_delete; static PFN_fs_audio_new fs_audio_new; static PFN_fs_audio_delete fs_audio_delete;
static PFN_fs_get_sfont_by_id fs_get_sfont_by_id; static PFN_fs_iteration_start fs_iteration_start; static PFN_fs_iteration_next fs_iteration_next; static PFN_fs_preset_banknum fs_preset_banknum; static PFN_fs_preset_num fs_preset_num;
static PFN_fs_sfload fs_sfload; static PFN_fs_sfunload fs_sfunload; static PFN_fs_program_select fs_program_select; static PFN_fs_noteon fs_noteon; static PFN_fs_noteoff fs_noteoff; static PFN_fs_program_change fs_program_change;
static int load_fs(void){
    const char *names[]={"libfluidsynth.so.3","libfluidsynth.so",NULL};
    for(int i=0;names[i]&&!hfs;i++) hfs=dlopen(names[i],RTLD_NOW|RTLD_LOCAL);
    if(!hfs) return 0;
#define F(var,sym) do{ *(void**)(&var)=dlsym(hfs,sym); if(!var) return 0; }while(0)
    F(fs_settings_setstr,"fluid_settings_setstr");F(fs_settings_new,"new_fluid_settings");F(fs_settings_delete,"delete_fluid_settings");F(fs_synth_new,"new_fluid_synth");F(fs_synth_delete,"delete_fluid_synth");F(fs_audio_new,"new_fluid_audio_driver");F(fs_audio_delete,"delete_fluid_audio_driver");F(fs_sfload,"fluid_synth_sfload");F(fs_sfunload,"fluid_synth_sfunload");F(fs_program_select,"fluid_synth_program_select");F(fs_get_sfont_by_id,"fluid_synth_get_sfont_by_id");F(fs_iteration_start,"fluid_sfont_iteration_start");F(fs_iteration_next,"fluid_sfont_iteration_next");F(fs_preset_banknum,"fluid_preset_get_banknum");F(fs_preset_num,"fluid_preset_get_num");F(fs_noteon,"fluid_synth_noteon");F(fs_noteoff,"fluid_synth_noteoff");F(fs_program_change,"fluid_synth_program_change");
#undef F
    return 1;
}

static uint64_t rng_state;
static uint32_t rnd_u32(void){ uint64_t x=rng_state; x^=x>>12; x^=x<<25; x^=x>>27; rng_state=x; return (uint32_t)((x*2685821657736338717ULL)>>32); }
static float frand(void){ return rnd_u32()/4294967295.0f; }
static int irand(int n){ return n? (int)(rnd_u32()%(uint32_t)n):0; }
static float clampf(float x,float a,float b){return x<a?a:x>b?b:x;}
static float smooth(float x){return x*x*(3.0f-2.0f*x);}
static float lerpf(float a,float b,float t){return a+(b-a)*t;}

/* Compact value-noise. Base noise never changes hue and stays dark. */
#define W 320
#define H 180
static float base[W*H];
static float n1[W*H];
static float n2[W*H];
static float noise_hash(int x,int y){ uint32_t h=(uint32_t)x*374761393u+(uint32_t)y*668265263u; h=(h^(h>>13))*1274126177u; h^=h>>16; return (h&0xffff)/65535.0f; }
static float value_noise(float x,float y,float t){
    int ix=(int)floorf(x), iy=(int)floorf(y); float fx=smooth(x-ix), fy=smooth(y-iy);
    float a=noise_hash(ix,iy), b=noise_hash(ix+1,iy), c=noise_hash(ix,iy+1), d=noise_hash(ix+1,iy+1);
    float v=lerpf(lerpf(a,b,fx),lerpf(c,d,fx),fy);
    float w=0.5f+0.5f*sinf(t*0.23f+x*0.07f-y*0.05f);
    return lerpf(v,0.5f+0.5f*sinf(x*12.9898f+y*78.233f+t*1.7f),0.18f*w);
}
static void make_base(void){
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        float v=value_noise(x*0.62f,y*0.62f,0.0f)*0.62f + frand()*0.38f;
        base[y*W+x]=clampf(v,0,1);
    }
}

typedef struct { float r,g,b; } RGB;
static RGB palette[35]; static int colors;
static void make_palette(void){
    /* Saturated hues: fixed candidate family, randomized subset/order every run. */
    static const float hs[35]={0,10,20,30,40,50,60,75,90,105,120,135,150,165,180,195,210,225,240,255,270,285,300,315,330,345,15,45,105,195,255,285,325,70,160};
    colors=5+irand(31);
    for(int i=0;i<colors;i++){
        float h=hs[irand(35)], s=0.88f+frand()*0.12f, v=0.72f+frand()*0.28f;
        float c=v*s, hh=h/60.0f, x=c*(1-fabsf(fmodf(hh,2)-1)), m=v-c; float r=0,g=0,b=0;
        if(hh<1){r=c;g=x;} else if(hh<2){r=x;g=c;} else if(hh<3){g=c;b=x;} else if(hh<4){g=x;b=c;} else if(hh<5){r=x;b=c;} else {r=c;b=x;}
        palette[i]=(RGB){r+m,g+m,b+m};
    }
}

typedef enum {M_LINE,M_CIRCLE,M_RING,M_BOX,M_TRI,M_SPIRAL,M_WAVE,M_FACE,M_PERSON,M_CLUSTER} Motif;
typedef struct { Motif type; float x,y,s,rot; int color; } Shape;
static Shape cur,nexts; static double scene_t=0, scene_len=30, morph_len=7;
static void new_shape(Shape *s){
    s->type=(Motif)irand(10); s->x=0.15f+frand()*0.70f; s->y=0.15f+frand()*0.70f; s->s=0.08f+frand()*0.28f; s->rot=frand()*6.2831853f; s->color=irand(colors);
}
static float dist2(float ax,float ay,float bx,float by){float x=ax-bx,y=ay-by;return sqrtf(x*x+y*y);}
static float motif_field(const Shape*s,float x,float y){
    float dx=x-s->x,dy=y-s->y, c=cosf(s->rot),q=sinf(s->rot), u=dx*c+dy*q,v=-dx*q+dy*c; float d=99;
    switch(s->type){
      case M_LINE: d=fabsf(v); break;
      case M_CIRCLE: d=fabsf(dist2(x,y,s->x,s->y)-s->s); break;
      case M_RING: d=fabsf(dist2(x,y,s->x,s->y)-s->s*0.55f); break;
      case M_BOX: d=fmaxf(fabsf(u)-s->s,fabsf(v)-s->s); d=fabsf(d); break;
      case M_TRI:{ float a=fabsf(v), b=fabsf(u)*0.8f+v*0.3f; d=fminf(a,b); d=fabsf(d-s->s*0.18f); }break;
      case M_SPIRAL:{ float r=dist2(x,y,s->x,s->y); float a=atan2f(v,u); d=fabsf(r-(0.05f+s->s*(a+3.14f)/(6.28f))); }break;
      case M_WAVE: d=fabsf(v-sinf(u*22.0f)*s->s*0.35f); break;
      case M_FACE:{ d=fabsf(dist2(x,y,s->x,s->y*0.92f)-s->s*0.8f); float eye1=dist2(x,y,s->x-s->s*.32f,s->y-s->s*.18f); float eye2=dist2(x,y,s->x+s->s*.32f,s->y-s->s*.18f); float mouth=fabsf(v-s->s*.25f); d=fminf(d,fminf(eye1,eye2)); d=fminf(d,mouth); }break;
      case M_PERSON:{ float head=dist2(x,y,s->x,s->y-s->s*.8f); float body=fabsf(u)+fabsf(v-s->s*.1f); float arm=fabsf(v-(s->s*.3f*sinf(u*7.0f))); d=fminf(head, fminf(body,arm)); }break;
      default: d=dist2(x,y,s->x,s->y); break;
    }
    return expf(-d*d/(0.00025f+s->s*0.008f));
}
static void morph_shape(Shape *a,const Shape*b,float t){
    a->x=lerpf(a->x,b->x,t);a->y=lerpf(a->y,b->y,t);a->s=lerpf(a->s,b->s,t);a->rot=lerpf(a->rot,b->rot,t);a->color=(t<0.5f?a->color:b->color);
}

/* FluidSynth state. */
static fluid_settings_t *fs_settings; static fluid_synth_t *fs_synth; static fluid_audio_driver_t *fs_audio; static int sfid=-1;
static int sound_ok=0; static int channel_for_color[35]; static int preset_index_for_color[35]; static int preset_bank[256], preset_prog[256], preset_count_global=0;
static void find_sf2(char *out,size_t cap){
    char exe[PATH_MAX]; ssize_t n=readlink("/proc/self/exe",exe,sizeof(exe)-1); if(n<=0)return; exe[n]=0;
    char *slash=strrchr(exe,'/'); if(!slash)return; *slash=0;
    DIR *d=opendir(exe); if(!d)return; struct dirent *e;
    while((e=readdir(d))){ size_t l=strlen(e->d_name); if(l>4 && !strcasecmp(e->d_name+l-4,".sf2")){snprintf(out,cap,"%s/%s",exe,e->d_name);break;} } closedir(d);
}
static void init_sound(void){
    if(!load_fs()) return; char sf2[PATH_MAX]={0}; find_sf2(sf2,sizeof(sf2)); if(!sf2[0]) return;
    fs_settings=fs_settings_new(); if(!fs_settings)return;
    fs_settings_setstr(fs_settings,"audio.driver","pulseaudio");
    fs_synth=fs_synth_new(fs_settings); if(!fs_synth){fs_settings_delete(fs_settings);fs_settings=NULL;return;}
    sfid=fs_sfload(fs_synth,sf2,1); if(sfid<0){fs_synth_delete(fs_synth);fs_settings_delete(fs_settings);fs_synth=NULL;fs_settings=NULL;return;}
    fs_audio=fs_audio_new(fs_settings,fs_synth); if(!fs_audio){fs_sfunload(fs_synth,sfid,1);fs_synth_delete(fs_synth);fs_settings_delete(fs_settings);fs_synth=NULL;return;}
    /* Enumerate actual presets in the loaded SF2. Random MIDI program numbers are not
       valid for arbitrary SoundFonts, so only choose programs that really exist. */
    int *banks=preset_bank, *progs=preset_prog, preset_count=0;
    fluid_sfont_t *sfont=fs_get_sfont_by_id(fs_synth,sfid);
    if(sfont){
        fs_iteration_start(sfont);
        fluid_preset_t *pr;
        while(preset_count < 256 && (pr=fs_iteration_next(sfont)) != NULL){
            banks[preset_count]=fs_preset_banknum(pr);
            progs[preset_count]=fs_preset_num(pr);
            preset_count++;
        }
    }
    preset_count_global=preset_count;
    if(preset_count==0){
        fprintf(stderr,"No usable presets found in SoundFont; continuing without sound.\n");
        fs_sfunload(fs_synth,sfid,1); fs_synth_delete(fs_synth); fs_settings_delete(fs_settings);
        fs_synth=NULL; fs_settings=NULL; sfid=-1;
        return;
    }
    sound_ok=1;
    int usable_channels[15], uc=0;
    for(int ch=0;ch<16;ch++) if(ch!=9) usable_channels[uc++]=ch;
    for(int i=0;i<35;i++){
        int pi=irand(preset_count);
        channel_for_color[i]=usable_channels[i%uc];
        channel_for_color[i]=usable_channels[irand(uc)];
        preset_index_for_color[i]=pi;
        fs_program_select(fs_synth,channel_for_color[i],sfid,banks[pi],progs[pi]);
    }
}
static void note_color(int color,float strength){
    if(!sound_ok)return; int ch=channel_for_color[color]; int note=42+irand(36); int vel=(int)(45+clampf(strength,0,1)*75); fs_noteon(fs_synth,ch,note,vel);
    /* short blocking note-off is intentionally avoided; next scene resets channels. */
}
static void reset_sound(void){ if(sound_ok) for(int c=0;c<16;c++) for(int n=0;n<128;n++) fs_noteoff(fs_synth,c,n); }

static float color_energy[35];
static void render_frame(uint32_t *pix,float t){
    for(int k=0;k<35;k++) color_energy[k]=0.0f;
    Shape s1=cur,s2=nexts; float mt=clampf((float)((scene_t-(scene_len-morph_len))/morph_len),0,1); if(scene_t<scene_len-morph_len)mt=0; morph_shape(&s1,&s2,smooth(mt));
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int i=y*W+x; float b=base[i];
        /* Moving fine grayscale background: dark, never hue-shifted, never allowed to become large. */
        float micro=value_noise(x*1.35f+t*0.12f,y*1.35f-t*0.09f,t)*0.55f+0.45f*b;
        float gray=4.0f+micro*28.0f; float rr=gray,gg=gray,bb=gray;
        float field=motif_field(&s1,x/(float)W,y/(float)H);
        float carrier=value_noise(x*2.2f+t*0.5f,y*2.2f-t*0.33f,t*0.7f);
        float gate=clampf((carrier-0.44f)*3.0f,0,1)*field;
        /* A few dark colored background points always remain, but never larger than base pixels. */
        float bgc=clampf((carrier-0.76f)*4.0f,0,1)*0.12f;
        int ci=s1.color%colors; RGB col=palette[ci];
        rr+=bgc*col.r*35;gg+=bgc*col.g*35;bb+=bgc*col.b*35;
        /* Foreground brightness follows the effective point size/field. */
        float size=clampf(field*(0.25f+0.75f*carrier),0,1);
        float gain=gate*(0.35f+0.65f*size);
        color_energy[ci]+=gain;
        rr=lerpf(rr,255*col.r,gain);gg=lerpf(gg,255*col.g,gain);bb=lerpf(bb,255*col.b,gain);
        /* Sparse white structures can emerge where the colored field is strongest. */
        if(gain>0.92f){float w=(gain-0.92f)*12.5f;rr=lerpf(rr,255,w);gg=lerpf(gg,255,w);bb=lerpf(bb,255,w);}
        pix[i]=((uint32_t)clampf(bb,0,255)<<24)|((uint32_t)clampf(gg,0,255)<<16)|((uint32_t)clampf(rr,0,255)<<8)|255;
    }
}

int main(void){
    rng_state=(uint64_t)time(NULL) ^ ((uint64_t)getpid()<<32) ^ (uintptr_t)&rng_state;
    make_base(); make_palette(); new_shape(&cur); new_shape(&nexts); scene_len=15+frand()*105; scene_t=0;
    if(!load_sdl()){fprintf(stderr,"SDL2 not available.\n");return 1;}
    /* Do not force a video backend: SDL selects Wayland/X11 automatically. */
    p_SetHint("SDL_RENDER_SCALE_QUALITY","0");
    if(p_Init(0x00000020u|0x00000010u)!=0){fprintf(stderr,"SDL_Init failed: %s\n",p_GetError());return 1;}
    SDL_Window *win=p_CreateWindow("Generative Noise",0x2FFF0000,0x2FFF0000,960,540,0x00000004u|0x00000020u); /* shown + resizable, no OpenGL dependency */
    if(!win){fprintf(stderr,"Window failed: %s\n",p_GetError());p_Quit();return 1;}
    SDL_Renderer *ren=p_CreateRenderer(win,-1,0x00000002u|0x00000004u); /* accelerated + vsync */
    if(!ren){
        fprintf(stderr,"Accelerated renderer unavailable (%s), using software renderer.\n",p_GetError());
        ren=p_CreateRenderer(win,-1,0);
    }
    if(!ren){fprintf(stderr,"Renderer failed: %s\n",p_GetError());p_DestroyWindow(win);p_Quit();return 1;}
    const Uint32 ARGB8888=0x16462004u; SDL_Texture *tex=p_CreateTexture(ren,ARGB8888,1,W,H); if(!tex){fprintf(stderr,"Texture failed: %s\n",p_GetError());return 1;}
    uint32_t *pixels=malloc(W*H*4); if(!pixels)return 1;
    p_ShowWindow(win); p_RaiseWindow(win); p_SetWindowPosition(win,0x2FFF0000,0x2FFF0000);
    fprintf(stderr,"Generative Noise: window created, renderer ready. video=%s\n", p_GetCurrentVideoDriver()?p_GetCurrentVideoDriver():"unknown");
    /* Present one frame BEFORE touching FluidSynth/audio. This guarantees that a sound backend cannot prevent the visual window from appearing. */
    render_frame(pixels,0.0f); if(p_UpdateTexture(tex,NULL,pixels,W*4)!=0){fprintf(stderr,"Texture update failed: %s\n",p_GetError());} p_RenderClear(ren); p_RenderCopy(ren,tex,NULL,NULL); p_RenderPresent(ren);
    p_Delay(20);
    init_sound();
    int running=1; float timef=0; double scene_start=0; double next_note=0;
    while(running){
        unsigned char ev[64]; while(p_PollEvent(ev)){ Uint32 type=*(Uint32*)ev; if(type==0x100u)running=0; /* quit */ if(type==0x300u && *(Sint32*)(ev+20)==27)running=0; }
        static struct timespec ts0; static int init=0; struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); if(!init){ts0=ts;init=1;} double now=(ts.tv_sec-ts0.tv_sec)+(ts.tv_nsec-ts0.tv_nsec)/1e9;
        scene_t=now-scene_start; timef=(float)now;
        if(scene_t>=scene_len){ reset_sound(); cur=nexts; new_shape(&nexts); scene_len=15+frand()*105; scene_start=now; scene_t=0; next_note=0; colors=5+irand(31); make_palette(); for(int i=0;i<colors;i++) if(sound_ok){ int pi=irand(preset_count_global); preset_index_for_color[i]=pi; fs_program_select(fs_synth,channel_for_color[i],sfid,preset_bank[pi],preset_prog[pi]); } }
        render_frame(pixels,timef);
        if(p_UpdateTexture(tex,NULL,pixels,W*4)!=0){fprintf(stderr,"Texture update failed: %s\n",p_GetError());} p_RenderClear(ren); p_RenderCopy(ren,tex,NULL,NULL); p_RenderPresent(ren);
        /* Trigger music from emergent colored structure at a low rate. */
        if(sound_ok && scene_t>0.2 && scene_t<scene_len-0.2 && scene_t>=next_note){
            float total=0; for(int k=0;k<colors;k++) total+=color_energy[k];
            int c=cur.color%colors;
            if(total>0.001f){ float pick=frand()*total; float acc=0; for(int k=0;k<colors;k++){acc+=color_energy[k]; if(acc>=pick){c=k;break;}} }
            float strength=total>0 ? clampf(color_energy[c]/(total/colors+1.0f),0.35f,1.0f) : 0.5f;
            note_color(c,strength); next_note=scene_t+0.18f+frand()*0.75f;
        }
    }
    reset_sound(); if(fs_audio)fs_audio_delete(fs_audio); if(sfid>=0)fs_sfunload(fs_synth,sfid,1); if(fs_synth)fs_synth_delete(fs_synth); if(fs_settings)fs_settings_delete(fs_settings);
    free(pixels); p_DestroyTexture(tex);p_DestroyRenderer(ren);p_DestroyWindow(win);p_Quit(); if(hfs)dlclose(hfs);if(hsdl)dlclose(hsdl); return 0;
}
