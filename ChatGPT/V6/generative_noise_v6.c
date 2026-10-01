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
#define W 480
#define H 270
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

typedef enum { M_LINE, M_CIRCLE, M_RING, M_TRI, M_SPIRAL, M_STAR, M_WAVE, M_FLOWER, M_CAT, M_BIRD, M_FISH, M_PERSON, M_BOX } Motif;
#define PARTICLES 10500
#define TARGETS 4200

typedef struct { float x,y,px,py,life,phase,size; int color; } Particle;
typedef struct { Motif type; float x,y,s,rot; int color; } Scene;
static Particle particles[PARTICLES];
static float targets[TARGETS*2];
static int target_count=0;
static Scene scene, next_scene;
static double scene_t=0, scene_len=55, fade_len=12;

static float hash01(int x,int y,int z){
    uint32_t h=(uint32_t)x*374761393u+(uint32_t)y*668265263u+(uint32_t)z*1442695041u;
    h=(h^(h>>13))*1274126177u; h^=h>>16; return (h&0xffff)/65535.0f;
}
static void scene_random(Scene *s){
    s->type=(Motif)irand(13); s->x=.22f+frand()*.56f; s->y=.22f+frand()*.56f;
    s->s=.14f+frand()*.25f; s->rot=frand()*6.2831853f; s->color=irand(colors);
}
static void add_target(float x,float y){
    if(target_count<TARGETS){ targets[target_count*2]=clampf(x,.03f,.97f); targets[target_count*2+1]=clampf(y,.03f,.97f); target_count++; }
}
static void add_line(float x0,float y0,float x1,float y1,int n){
    for(int i=0;i<n;i++){float t=(i+.5f)/n; add_target(lerpf(x0,x1,t),lerpf(y0,y1,t));}
}
static void build_targets(Scene *s){
    target_count=0; float c=cosf(s->rot), q=sinf(s->rot);
    #define XFORM(U,V) do{float _x=s->x+(U)*s->s*c-(V)*s->s*q; float _y=s->y+(U)*s->s*q+(V)*s->s*c; add_target(_x,_y);}while(0)
    switch(s->type){
    case M_LINE: add_line(s->x-s->s,s->y,s->x+s->s,s->y,1000); break;
    case M_CIRCLE: case M_RING: for(int i=0;i<1800;i++){float a=6.2831853f*i/1800.f; float r=(s->type==M_RING?.72f:.92f)+frand()*.08f; XFORM(cosf(a)*r,sinf(a)*r);} break;
    case M_TRI: for(int e=0;e<3;e++){float a0=-1.5708f+e*2.0944f,a1=-1.5708f+(e+1)*2.0944f; add_line(s->x+s->s*.9f*cosf(a0),s->y+s->s*.9f*sinf(a0),s->x+s->s*.9f*cosf(a1),s->y+s->s*.9f*sinf(a1),700);} break;
    case M_BOX: add_line(s->x-s->s,s->y-s->s,s->x+s->s,s->y-s->s,500);add_line(s->x+s->s,s->y-s->s,s->x+s->s,s->y+s->s,500);add_line(s->x+s->s,s->y+s->s,s->x-s->s,s->y+s->s,500);add_line(s->x-s->s,s->y+s->s,s->x-s->s,s->y-s->s,500);break;
    case M_SPIRAL: for(int i=0;i<2800;i++){float t=i/2799.f,a=t*12.566f,r=.03f+.9f*t; XFORM(cosf(a)*r,sinf(a)*r);}break;
    case M_STAR: for(int i=0;i<2600;i++){float a=6.2831853f*(i%500)/500.f;float r=(i%500)/499.f; r=fmodf(r*5.f,1.f); r=.2f+r*.72f*(1.f+.75f*cosf(5*a));XFORM(cosf(a)*r,sinf(a)*r);}break;
    case M_WAVE: for(int i=0;i<3000;i++){float u=-1.f+2.f*(i/3000.f); for(int j=0;j<2;j++)XFORM(u,(j?-.02f:.02f)+.24f*sinf(u*9.f));}break;
    case M_FLOWER: for(int i=0;i<3500;i++){float a=6.2831853f*(i/3500.f)*12.f;float r=.12f+.78f*(.5f+.5f*cosf(6*a));XFORM(r*cosf(a),r*sinf(a));}break;
    case M_CAT: {
        /* head, ears, body, tail, legs: deliberately only a point-skeleton so noise fills it. */
        for(int i=0;i<1200;i++){float a=6.2831853f*i/1200.f;XFORM(.46f*cosf(a),-.42f+.38f*sinf(a));}
        add_line(s->x-.30f*s->s,s->y-.78f*s->s,s->x-.18f*s->s,s->y-1.12f*s->s,350);add_line(s->x+.30f*s->s,s->y-.78f*s->s,s->x+.18f*s->s,s->y-1.12f*s->s,350);
        for(int i=0;i<1400;i++){float a=6.2831853f*i/1400.f;XFORM(.55f*cosf(a),.25f+.72f*sinf(a));}
        add_line(s->x+.52f*s->s,s->y+.45f*s->s,s->x+1.05f*s->s,s->y+.05f*s->s,900);add_line(s->x+1.05f*s->s,s->y+.05f*s->s,s->x+.82f*s->s,s->y-.28f*s->s,600);
        add_line(s->x-.25f*s->s,s->y+.65f*s->s,s->x-.25f*s->s,s->y+1.0f*s->s,450);add_line(s->x+.25f*s->s,s->y+.65f*s->s,s->x+.25f*s->s,s->y+1.0f*s->s,450); break; }
    case M_BIRD: add_line(s->x-s->s,s->y,s->x+s->s,s->y,700); for(int i=0;i<1800;i++){float t=i/1800.f;float u=-1.f+2.f*t; XFORM(u,-.08f-.55f*(1-fabsf(u))*sin(fabsf(u)*3.1415f));}break;
    case M_FISH: {for(int i=0;i<1600;i++){float a=6.2831853f*i/1600.f;XFORM(.72f*cosf(a),.42f*sinf(a));} add_line(s->x-.7f*s->s,s->y,s->x-1.15f*s->s,s->y-.5f*s->s,500);add_line(s->x-1.15f*s->s,s->y-.5f*s->s,s->x-1.15f*s->s,s->y+.5f*s->s,500);add_line(s->x-1.15f*s->s,s->y+.5f*s->s,s->x-.7f*s->s,s->y,500);break;}
    case M_PERSON: {add_line(s->x,s->y-.65f*s->s,s->x,s->y+.45f*s->s,1000);for(int i=0;i<900;i++){float a=6.2831853f*i/900.f;XFORM(.25f*cosf(a),-.82f+.25f*sinf(a));}add_line(s->x,s->y-.1f*s->s,s->x-.65f*s->s,s->y+.15f*s->s,550);add_line(s->x,s->y-.1f*s->s,s->x+.65f*s->s,s->y-.35f*s->s,550);add_line(s->x,s->y+.42f*s->s,s->x-.55f*s->s,s->y+1.0f*s->s,600);add_line(s->x,s->y+.42f*s->s,s->x+.55f*s->s,s->y+.8f*s->s,600);break;}
    default: for(int i=0;i<3000;i++){float a=frand()*6.2831853f,r=sqrtf(frand());XFORM(r*cosf(a),r*sinf(a));} break;
    }
    #undef XFORM
}
static void init_particles(void){
    for(int i=0;i<PARTICLES;i++){particles[i].x=frand();particles[i].y=frand();particles[i].px=particles[i].x;particles[i].py=particles[i].y;particles[i].life=frand();particles[i].phase=frand()*6.28f;particles[i].size=.45f+frand()*1.3f;particles[i].color=irand(colors);}
}
static void retarget_particles(void){
    build_targets(&scene); build_targets(&next_scene);
    for(int i=0;i<PARTICLES;i++){particles[i].px=particles[i].x; particles[i].py=particles[i].y; if(frand()<.68f && target_count>0){int j=irand(target_count);particles[i].x=targets[j*2];particles[i].y=targets[j*2+1];} }
}
static float target_near(float x,float y){
    float best=99; int count=target_count; for(int k=0;k<18 && count;k++){int j=irand(count);float dx=x-targets[j*2],dy=y-targets[j*2+1],d=dx*dx+dy*dy;if(d<best)best=d;} return expf(-best/(.00025f));
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
    for(int k=0;k<35;k++) color_energy[k]=0;
    /* Base: immutable fine grayscale noise, continuously jittered only in intensity. */
    for(int y=0;y<H;y++) for(int x=0;x<W;x++){
        int i=y*W+x; float m=hash01(x+(int)(t*9),y-(int)(t*7),17); float v=base[i]*.72f+m*.28f;
        float g=2.0f+v*30.0f; pix[i]=((uint32_t)g<<24)|((uint32_t)g<<16)|((uint32_t)g<<8)|255;
    }
    float progress=(float)scene_t; float build=clampf(progress/16.f,0,1), dissolve=clampf((scene_len-progress)/fade_len,0,1); float morph=clampf((progress-(scene_len-fade_len))/fade_len,0,1);
    if(progress>scene_len-fade_len){ build_targets(&next_scene); scene=next_scene; build=1; }
    else { build_targets(&scene); }
    for(int i=0;i<PARTICLES;i++){
        Particle *p=&particles[i];
        float tx=p->x,ty=p->y;
        /* During the middle of a scene particles are attracted to the current target set;
           at the beginning/end the attraction weakens, so they remain noise. */
        float attract=smooth(build)*smooth(dissolve);
        if(morph>0){ if(target_count){int j=(i*37)%target_count;tx=targets[j*2];ty=targets[j*2+1];}}
        else if(target_count){int j=(i*37)%target_count;tx=targets[j*2];ty=targets[j*2+1];}
        float local=attract*(0.25f+0.75f*target_near(p->x,p->y));
        float nx=p->x,ny=p->y;
        nx += (tx-p->x)*0.025f*local; ny += (ty-p->y)*0.025f*local;
        float flow=value_noise(p->x*9.f+t*.25f,p->y*9.f-t*.19f,t*.2f)-.5f;
        nx += cosf(flow*6.28f+p->phase)*.0018f; ny += sinf(flow*6.28f+p->phase)*.0018f;
        if(nx<0)nx+=1;if(nx>1)nx-=1;if(ny<0)ny+=1;if(ny>1)ny-=1;
        p->px=p->x;p->py=p->y;p->x=nx;p->y=ny;
        float emergence=attract*(.35f+.65f*target_near(nx,ny));
        if(frand()<.00015f) p->color=irand(colors);
        int c=p->color%colors; RGB col=palette[c];
        float brightness=clampf(emergence*(.5f+.5f*sinf(p->phase+t*.6f)),0,1);
        if(brightness<.025f) continue;
        int cx=(int)(nx*W),cy=(int)(ny*H); int rad=(int)(.5f+p->size*(.4f+2.2f*brightness));
        for(int yy=-rad;yy<=rad;yy++) for(int xx=-rad;xx<=rad;xx++){
            int X=cx+xx,Y=cy+yy;if(X<0||X>=W||Y<0||Y>=H)continue;float d=sqrtf((float)(xx*xx+yy*yy));if(d>rad)continue;float a=(1-d/(rad+1.f))*brightness;
            int idx=Y*W+X; uint32_t q=pix[idx];float oldr=(q>>8)&255,oldg=(q>>16)&255,oldb=(q>>24)&255;
            float rr=lerpf(oldr,255*col.r,a),gg=lerpf(oldg,255*col.g,a),bb=lerpf(oldb,255*col.b,a);
            pix[idx]=((uint32_t)clampf(bb,0,255)<<24)|((uint32_t)clampf(gg,0,255)<<16)|((uint32_t)clampf(rr,0,255)<<8)|255;
        }
        color_energy[c]+=brightness;
    }
}

int main(void){
    rng_state=(uint64_t)time(NULL) ^ ((uint64_t)getpid()<<32) ^ (uintptr_t)&rng_state;
    make_base(); make_palette(); scene_random(&scene); scene_random(&next_scene); scene_len=35+frand()*85; scene_t=0; init_particles(); retarget_particles();
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
        if(scene_t>=scene_len){ scene=next_scene; scene_random(&next_scene); scene_len=35+frand()*85; scene_start=now; scene_t=0; colors=5+irand(31); make_palette(); retarget_particles(); reset_sound(); next_note=0; }
        render_frame(pixels,timef);
        if(p_UpdateTexture(tex,NULL,pixels,W*4)!=0){fprintf(stderr,"Texture update failed: %s\n",p_GetError());} p_RenderClear(ren); p_RenderCopy(ren,tex,NULL,NULL); p_RenderPresent(ren);
        /* Trigger music from emergent colored structure at a low rate. */
        if(sound_ok && scene_t>0.2 && scene_t<scene_len-0.2 && scene_t>=next_note){
            float total=0; for(int k=0;k<colors;k++) total+=color_energy[k];
            int c=irand(colors);
            if(total>0.001f){ float pick=frand()*total; float acc=0; for(int k=0;k<colors;k++){acc+=color_energy[k]; if(acc>=pick){c=k;break;}} }
            float strength=total>0 ? clampf(color_energy[c]/(total/colors+1.0f),0.35f,1.0f) : 0.5f;
            note_color(c,strength); next_note=scene_t+0.18f+frand()*0.75f;
        }
    }
    reset_sound(); if(fs_audio)fs_audio_delete(fs_audio); if(sfid>=0)fs_sfunload(fs_synth,sfid,1); if(fs_synth)fs_synth_delete(fs_synth); if(fs_settings)fs_settings_delete(fs_settings);
    free(pixels); p_DestroyTexture(tex);p_DestroyRenderer(ren);p_DestroyWindow(win);p_Quit(); if(hfs)dlclose(hfs);if(hsdl)dlclose(hsdl); return 0;
}
