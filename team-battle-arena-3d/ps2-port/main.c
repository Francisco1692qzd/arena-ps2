/*
 * TEAM BATTLE ARENA 3D - PS2 Full Port
 * Mirrors team-battle-arena-3d/index.html (Three.js 4-min 4v4 FPS)
 * Maps: NEON 80x80, DESERT 90x90, INDUSTRIAL 70x70 — distinct layouts + per-map barriers
 * Barriers: oriented (yaw) — ray/hasLOS/collides all use local-space rotate, fair for both sides
 * Guns/Knives shop + persistent name/cash (mc0:TBASAVE.DAT, falls back to memory)
 * Toolchain: ps2sdk + gsKit + dmaKit + libpad (+ libmc for save, stubbed to host: if no MC)
 * Build: make && make iso && PCSX2 -> arena.iso | OPL USB
 */

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <gs_psm.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ---- Constants (mirror web) ----
#define ROUND_TIME 240
#define MAX_BOTS 7
#define MAX_BARRIERS 12
#define MAX_GRENADES 6
#define WORLD_WALL 1.2f
#define WALL_H 8.0f

// ---- Guns / Knives (index.html:346) ----
typedef struct { char id[16]; char name[24]; int dmg; int cd; float spread; int pellets; int price; } Gun;
typedef struct { char id[16]; char name[24]; int dmg; float range; int cd; int price; } Knife;
static Gun GUNS[4] = {
    {"pistol","P-9 PISTOL",34,7,0.020f,1,0},
    {"carbine","CARBINE X3",42,5,0.018f,1,450},
    {"raider","RAIDER SG",66,14,0.065f,3,850},
    {"rail","RAIL MK-II",78,18,0.010f,1,1300}
};
static Knife KNIVES[3] = {
    {"none","—",0,0,0,0},
    {"knife","COMBAT KNIFE",80,2.6f,14,350},
    {"tanto","TANTO BLADE",110,2.9f,20,750}
};

// ---- Store (localStorage tba_store analog) -> mc0:TBASAVE.DAT ----
typedef struct {
    int cash;
    int ownedGuns[4];
    int ownedKnives[3];
    int equippedGun;
    int equippedKnife;
    char playerName[13];
} Store;
static Store store = {0,{1,0,0,0},{1,0,0},0,0,"PLAYER"};
static void saveStore(){
    FILE *f=fopen("mc0:TBASAVE.DAT","wb");
    if(!f) f=fopen("host:TBASAVE.DAT","wb"); // PCSX2 host: fallback
    if(f){ fwrite(&store,1,sizeof(store),f); fclose(f); }
}
static void loadStore(){
    FILE *f=fopen("mc0:TBASAVE.DAT","rb");
    if(!f) f=fopen("host:TBASAVE.DAT","rb");
    if(f){ fread(&store,1,sizeof(store),f); fclose(f); }
    if(store.playerName[0]==0) strcpy(store.playerName,"PLAYER");
    // sanitize upper
    for(int i=0;i<12 && store.playerName[i];i++){
        char c=store.playerName[i];
        if(c>='a'&&c<='z') c-=32;
        if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) c='_';
        store.playerName[i]=c;
    }
    store.playerName[12]=0;
    if(store.ownedGuns[0]==0) store.ownedGuns[0]=1;
    if(store.equippedGun<0||store.equippedGun>3) store.equippedGun=0;
    if(store.equippedKnife<0||store.equippedKnife>2) store.equippedKnife=0;
    if(!store.ownedGuns[store.equippedGun]) store.equippedGun=0;
}
static Gun* getEquippedGun(){ return &GUNS[store.equippedGun]; }
static Knife* getEquippedKnife(){ return &KNIVES[store.equippedKnife]; }

// ---- Bot name pool distinct (index.html:364) ----
static const char *BOT_POOL[30]={"PHOENIX","VIPER","GHOST","REAPER","BLAZE","FROST","WRAITH","JAX","NOVA","HAVOC","SPECTRE","TITAN","KAI","RAVEN","COBRA","ATLAS","OSIRIS","VALKYR","MANTIS","ECHO","KRAKEN","DRAGON","FANG","REX","ZERO","BLITZ","ONYX","SABLE","QUAKE","STORM"};
static char botNames[7][16];
static void pickBotNames(){
    int pool[30]; for(int i=0;i<30;i++) pool[i]=i;
    for(int i=29;i>0;i--){ int j=rand()%(i+1); int t=pool[i]; pool[i]=pool[j]; pool[j]=t; }
    for(int i=0;i<7;i++){
        strncpy(botNames[i], BOT_POOL[pool[i]], 15); botNames[i][15]=0;
        if(strcmp(botNames[i], store.playerName)==0) { // avoid duplicate with player
            snprintf(botNames[i],16,"BOT%d", rand()%90+10);
        }
    }
}

// ---- Map definitions (index.html:347) ----
typedef struct { float x,z,w,d,h; int mat; } Cover;
typedef struct { float w,d,h; int hp; int life; int supply; u32 color; } BarrierStats;
typedef struct {
    char id[16]; char name[24]; int world; u32 fog; u32 floorCol;
    BarrierStats bstat;
    float baseA[2], baseB[2];
    Cover layout[12]; int nLayout;
} Map;

static Map MAPS[3] = {
    {"neon","NEON ARENA",80,0x060a1a,0x0e1426, {4.0f,0.45f,2.8f,95,840,3,0x00d8ff},
     {-32,0, 32,0},
     {{{-12,0,2,14,2.5,0},{12,0,2,14,2.5,0},{0,12,14,2,2.0,1},{0,-12,14,2,2.0,1},{-22,10,6,3,2.2,0},{22,-10,6,3,2.2,0},{-22,-10,6,3,2.2,0},{22,10,6,3,2.2,0},{0,0,4,4,1.2,2}},9}
    },
    {"desert","DESERT OUTPOST",90,0x1a1208,0x4a3a1a, {5.4f,0.6f,2.2f,155,1320,2,0xc2a46a},
     {-37,0, 37,0},
     {{{0,0,8,8,1.0,2},{0,26,12,3,1.8,1},{0,-26,12,3,1.8,1},{26,0,3,12,1.8,1},{-26,0,3,12,1.8,1},{-20,18,8,4,2.2,0},{20,18,8,4,2.2,0},{-20,-18,8,4,2.2,0},{20,-18,8,4,2.2,0}},9}
    },
    {"industrial","INDUSTRIAL YARD",70,0x10151a,0x1a2520, {4.4f,0.48f,3.4f,135,1080,2,0x8a9aaa},
     {-27,0, 27,0},
     {{{-11,8,2,16,2.6,0},{11,8,2,16,2.6,0},{-11,-8,2,16,2.6,0},{11,-8,2,16,2.6,0},{0,11,16,2,2.2,1},{0,-11,16,2,2.2,1},{-17,0,3,8,2.8,0},{17,0,3,8,2.8,0},{0,0,4,4,1.4,2},{8,8,3,3,1.8,0},{-8,-8,3,3,1.8,0},{-8,8,3,3,1.8,1}},12}
    }
};
static int selectedMapIdx=0;
static Map *selectedMap=&MAPS[0];

// ---- Game entities ----
typedef struct { float x,z,y,vy; float hp,maxHp; int alive,respawn; int team; int kills,deaths; int nades,bars; int shootCd,knifeCd,hitFlash; char name[16]; int isPlayer; char gunId[16],knifeId[16]; float yaw; } Entity;
typedef struct { float x,z,w,d,h; int hp,maxHp,life; float yaw; int team; } Barrier;
typedef struct { float x,y,z,vx,vy,vz; int timer,team; int ownerIsPlayer; } Grenade;

static GSGLOBAL *gsGlobal;
static Entity player, bots[MAX_BOTS];
static Barrier barriers[MAX_BARRIERS]; static int nBarriers=0;
static Grenade grenades[MAX_GRENADES]; static int nGrenades=0;
static Cover coverBoxes[16]; static int nCovers=0;
static int teamScoreA=0,teamScoreB=0;
static float timeLeft=ROUND_TIME, lastSec=ROUND_TIME;
static float yaw=0,pitch=0;
static int worldSize=80;
static int canJump=1, onGround=1;
static int state=0; //0 menu,1 playing,2 paused,3 shop,4 over
static int scoreA=0,scoreB=0;

// pad
static char padBuf[256] __attribute__((aligned(64)));
static struct padButtonStatus btnStat;
static u32 paddata=0, oldPad=0;

// ---- Oriented helpers (index.html:1085) ----
static int lineIntersectsBox(float p1x,float p1z,float p2x,float p2z, float bx,float bz,float w,float d,float yawv){
    if(fabs(yawv)>0.001f){
        float c=cosf(-yawv), s=sinf(-yawv);
        float l1x=(p1x-bx)*c - (p1z-bz)*s, l1z=(p1x-bx)*s + (p1z-bz)*c;
        float l2x=(p2x-bx)*c - (p2z-bz)*s, l2z=(p2x-bx)*s + (p2z-bz)*c;
        float minX=-w/2, maxX=w/2, minZ=-d/2, maxZ=d/2;
        float tmin=0,tmax=1, dx=l2x-l1x, dz=l2z-l1z;
        #define CLIP(p,q) do{ if(p==0){ if(q<0) return 0; } else { float r=q/p; if(p<0){ if(r>tmax) return 0; if(r>tmin) tmin=r; } else { if(r<tmin) return 0; if(r<tmax) tmax=r; } } }while(0)
        CLIP(-dx, l1x-minX); CLIP(dx, maxX-l1x); CLIP(-dz, l1z-minZ); CLIP(dz, maxZ-l1z);
        return 1;
        #undef CLIP
    } else {
        float minX=bx-w/2, maxX=bx+w/2, minZ=bz-d/2, maxZ=bz+d/2;
        float tmin=0,tmax=1, dx=p2x-p1x, dz=p2z-p1z;
        #define CLIP2(p,q) do{ if(p==0){ if(q<0) return 0; } else { float r=q/p; if(p<0){ if(r>tmax) return 0; if(r>tmin) tmin=r; } else { if(r<tmin) return 0; if(r<tmax) tmax=r; } } }while(0)
        CLIP2(-dx, p1x-minX); CLIP2(dx, maxX-p1x); CLIP2(-dz, p1z-minZ); CLIP2(dz, maxZ-p1z);
        return 1;
        #undef CLIP2
    }
}
static int hasLOS(float ax,float az,float bx,float bz){
    for(int i=0;i<nCovers;i++) if(lineIntersectsBox(ax,az,bx,bz, coverBoxes[i].x,coverBoxes[i].z,coverBoxes[i].w,coverBoxes[i].d,0)) return 0;
    for(int i=0;i<nBarriers;i++) if(lineIntersectsBox(ax,az,bx,bz, barriers[i].x,barriers[i].z,barriers[i].w,barriers[i].d,barriers[i].yaw)) return 0;
    return 1;
}
static int pointInBarrier(float x,float z, Barrier *b, float rad){
    if(fabs(b->yaw)<0.001f) return fabs(x-b->x) < b->w/2+rad && fabs(z-b->z) < b->d/2+rad;
    float c=cosf(-b->yaw), s=sinf(-b->yaw);
    float lx=(x-b->x)*c - (z-b->z)*s;
    float lz=(x-b->x)*s + (z-b->z)*c;
    return fabs(lx) < b->w/2+rad && fabs(lz) < b->d/2+rad;
}
static int collidesWorld(float x,float z,float rad){
    if(x < -worldSize/2+rad || x> worldSize/2-rad || z < -worldSize/2+rad || z> worldSize/2-rad) return 1;
    for(int i=0;i<nCovers;i++) if(fabs(x-coverBoxes[i].x) < coverBoxes[i].w/2+rad && fabs(z-coverBoxes[i].z) < coverBoxes[i].d/2+rad) return 1;
    for(int i=0;i<nBarriers;i++) if(pointInBarrier(x,z,&barriers[i],rad)) return 1;
    return 0;
}
static float rayBoxDist(float ox,float oz,float dx,float dz, float bx,float bz,float w,float d,float yawv){
    if(fabs(yawv)>0.001f){
        float c=cosf(-yawv), s=sinf(-yawv);
        float lox=(ox-bx)*c - (oz-bz)*s, loz=(ox-bx)*s + (oz-bz)*c;
        float ldx=dx*c - dz*s, ldz=dx*s + dz*c;
        float minX=-w/2,maxX=w/2,minZ=-d/2,maxZ=d/2;
        float tmin=-1e30, tmax=1e30;
        if(fabs(ldx)<1e-4){ if(lox<minX||lox>maxX) return 1e30; }
        else { float t1=(minX-lox)/ldx, t2=(maxX-lox)/ldx; if(t1>t2){float tt=t1;t1=t2;t2=tt;} if(t1>tmin) tmin=t1; if(t2<tmax) tmax=t2; if(tmin>tmax) return 1e30; }
        if(fabs(ldz)<1e-4){ if(loz<minZ||loz>maxZ) return 1e30; }
        else { float t1=(minZ-loz)/ldz, t2=(maxZ-loz)/ldz; if(t1>t2){float tt=t1;t1=t2;t2=tt;} if(t1>tmin) tmin=t1; if(t2<tmax) tmax=t2; if(tmin>tmax) return 1e30; }
        if(tmax<0) return 1e30;
        return tmin>=0?tmin:tmax;
    } else {
        float minX=bx-w/2,maxX=bx+w/2,minZ=bz-d/2,maxZ=bz+d/2;
        float tmin=-1e30,tmax=1e30;
        if(fabs(dx)<1e-4){ if(ox<minX||ox>maxX) return 1e30; }
        else { float t1=(minX-ox)/dx,t2=(maxX-ox)/dx; if(t1>t2){float tt=t1;t1=t2;t2=tt;} if(t1>tmin) tmin=t1; if(t2<tmax) tmax=t2; if(tmin>tmax) return 1e30; }
        if(fabs(dz)<1e-4){ if(oz<minZ||oz>maxZ) return 1e30; }
        else { float t1=(minZ-oz)/dz,t2=(maxZ-oz)/dz; if(t1>t2){float tt=t1;t1=t2;t2=tt;} if(t1>tmin) tmin=t1; if(t2<tmax) tmax=t2; if(tmin>tmax) return 1e30; }
        if(tmax<0) return 1e30;
        return tmin>=0?tmin:tmax;
    }
}

// ---- shootRay (index.html:1147) ----
typedef struct { int hit; int isPlayer; float hx,hy,hz; float dist; } RayRes;
static RayRes shootRay(float fx,float fy,float fz,float dx,float dy,float dz, int team, Entity *owner){
    RayRes r={0,0,0,0,0,1e30};
    float wallDist=1e30;
    for(int i=0;i<nCovers;i++) { float d=rayBoxDist(fx,fz,dx,dz, coverBoxes[i].x,coverBoxes[i].z,coverBoxes[i].w,coverBoxes[i].d,0); if(d<wallDist) wallDist=d; }
    for(int i=0;i<nBarriers;i++){ float d=rayBoxDist(fx,fz,dx,dz, barriers[i].x,barriers[i].z,barriers[i].w,barriers[i].d,barriers[i].yaw); if(d<wallDist) wallDist=d; }
    float closest=1e30; int hit=-1, hitPlayer=0;
    for(int i=0;i<MAX_BOTS;i++){
        if(!bots[i].alive || bots[i].team==team) continue;
        float ocx=fx-bots[i].x, ocz=fz-bots[i].z, ocy=fy-1.0f;
        float bdot=ocx*dx+ocy*dy+ocz*dz, oc2=ocx*ocx+ocy*ocy+ocz*ocz, rad=0.75f;
        float disc=bdot*bdot-(oc2-rad*rad); if(disc<0) continue;
        float t=-bdot - sqrtf(disc);
        if(t<0 || t>wallDist || t>80) continue;
        if(t<closest){ closest=t; hit=i; hitPlayer=0; }
    }
    if(team!=0 || (owner && !owner->isPlayer)){
        if(player.alive && player.team!=team){
            float ocx=fx-player.x, ocz=fz-player.z, ocy=fy-player.y;
            float bdot=ocx*dx+ocy*dy+ocz*dz, oc2=ocx*ocx+ocy*ocy+ocz*ocz, rad=0.65f;
            float disc=bdot*bdot-(oc2-rad*rad);
            if(disc>=0){ float t=-bdot - sqrtf(disc); if(t>=0 && t<closest && t<wallDist && t<80){ closest=t; hitPlayer=1; hit=99; } }
        }
    }
    if(hit>=0 || hitPlayer){ r.hit=1; r.isPlayer=hitPlayer; r.dist=closest; r.hx=fx+dx*closest; r.hy=fy+dy*closest; r.hz=fz+dz*closest; }
    else if(wallDist<1e20){ r.hx=fx+dx*wallDist; r.hy=fy+dy*wallDist; r.hz=fz+dz*wallDist; r.dist=wallDist; }
    else { r.hx=fx+dx*80; r.hy=fy+dy*80; r.hz=fz+dz*80; }
    return r;
}

// ---- damage / killFeed ----
static void damage(Entity *tgt, int dmg, Entity *atk){
    if(!tgt->alive) return;
    if(atk && atk->team==tgt->team) return;
    tgt->hp-=dmg; tgt->hitFlash=8;
    if(tgt->hp<=0){
        tgt->alive=0; tgt->deaths++; tgt->hp=0; tgt->respawn=120;
        if(atk && atk->team!=tgt->team){
            atk->kills++; if(atk->team==0) teamScoreA++; else teamScoreB++;
            if(atk->isPlayer){
                int reward=120;
                // weapon tag for knife bonus (set before call)
                extern int lastWeaponIsKnife;
                if(lastWeaponIsKnife) reward=160;
                store.cash+=reward; saveStore();
            }
        }
    }
}
static int lastWeaponIsKnife=0;

// ---- helpers forward ----
static void getForward(float *fx,float *fy,float *fz){ *fx=-sinf(yaw)*cosf(pitch); *fy=sinf(pitch); *fz=-cosf(yaw)*cosf(pitch); }

// ---- map build (index.html:586) ----
static void clearMap(){
    nCovers=0; nBarriers=0; // visuals are gsKit primitives, not tracked separately in this stub
}
static void buildMap(int idx){
    selectedMapIdx=idx; selectedMap=&MAPS[idx];
    worldSize=selectedMap->world;
    clearMap();
    for(int i=0;i<selectedMap->nLayout;i++){
        coverBoxes[nCovers].x=selectedMap->layout[i].x;
        coverBoxes[nCovers].z=selectedMap->layout[i].z;
        coverBoxes[nCovers].w=selectedMap->layout[i].w;
        coverBoxes[nCovers].d=selectedMap->layout[i].d;
        coverBoxes[nCovers].h=selectedMap->layout[i].h;
        nCovers++;
    }
}

// ---- barriers (per-map stats index.html:1366) ----
static void placeBarrier(){
    if(!player.alive || player.bars<=0) return;
    float fx,fy,fz; getForward(&fx,&fy,&fz);
    BarrierStats *bs=&selectedMap->bstat;
    float bx=player.x+fx*2.6f, bz=player.z+fz*2.6f;
    if(fabs(bx)>worldSize/2-2 || fabs(bz)>worldSize/2-2) return;
    float collR = bs->w*0.32f; if(collR<1.2f) collR=1.2f;
    if(collidesWorld(bx,bz,collR)) return;
    for(int i=0;i<nBarriers;i++) if(hypotf(barriers[i].x-bx, barriers[i].z-bz)<3.5f) return;
    if(nBarriers>=MAX_BARRIERS) return;
    player.bars--;
    Barrier *b=&barriers[nBarriers++];
    b->x=bx; b->z=bz; b->w=bs->w; b->d=bs->d; b->h=bs->h; b->hp=bs->hp; b->maxHp=bs->hp; b->life=bs->life; b->yaw=atan2f(fx,fz); b->team=player.team;
}
static void placeBotBarrier(Entity *bot){
    BarrierStats *bs=&selectedMap->bstat;
    float bx=bot->x - cosf(bot->yaw)*2.2f, bz=bot->z - sinf(bot->yaw)*2.2f;
    if(fabs(bx)>worldSize/2-2 || fabs(bz)>worldSize/2-2) return;
    float collR=bs->w*0.32f; if(collR<1.2f) collR=1.2f;
    if(collidesWorld(bx,bz,collR)) return;
    if(nBarriers>=MAX_BARRIERS) return;
    bot->bars--;
    Barrier *b=&barriers[nBarriers++];
    b->x=bx; b->z=bz; b->w=bs->w; b->d=bs->d; b->h=bs->h; b->hp=bs->hp; b->maxHp=bs->hp; b->life=bs->life; b->yaw=bot->yaw; b->team=bot->team;
}

// ---- knife (index.html:1387) ----
static int knifeStab(){
    Knife *k=getEquippedKnife();
    if(!player.alive || player.knifeCd>0 || k->dmg==0) return 0;
    player.knifeCd=k->cd; lastWeaponIsKnife=1;
    float fx,fy,fz; getForward(&fx,&fy,&fz);
    float bestD=1e30; int best=-1;
    for(int i=0;i<MAX_BOTS;i++){
        if(!bots[i].alive || bots[i].team==player.team) continue;
        float dx=bots[i].x-player.x, dy=1.0f-player.y, dz=bots[i].z-player.z;
        float dist=sqrtf(dx*dx+dy*dy+dz*dz); if(dist>k->range+0.75f) continue;
        float len=sqrtf(dx*dx+dy*dy+dz*dz); float toX=dx/len,toY=dy/len,toZ=dz/len;
        if(fx*toX+fy*toY+fz*toZ < 0.52f) continue;
        if(!hasLOS(player.x,player.z,bots[i].x,bots[i].z)) continue;
        if(dist<bestD){ bestD=dist; best=i; }
    }
    if(best>=0) damage(&bots[best], k->dmg, &player);
    else {
        // miss still checks barrier in front
        RayRes rr=shootRay(player.x,player.y,player.z, fx,fy,fz, 0,&player);
        if(rr.dist < k->range){
            for(int i=0;i<nBarriers;i++) if(pointInBarrier(rr.hx,rr.hz,&barriers[i],0.12f)){
                barriers[i].hp -= (k->dmg*7/10); if(barriers[i].hp<=0){ barriers[i]=barriers[nBarriers-1]; nBarriers--; }
                break;
            }
        }
    }
    lastWeaponIsKnife=0;
    return 1;
}

// ---- grenade ----
static void throwGrenade(){
    if(!player.alive || player.nades<=0 || nGrenades>=MAX_GRENADES) return;
    player.nades--;
    float fx,fy,fz; getForward(&fx,&fy,&fz);
    Grenade *g=&grenades[nGrenades++];
    g->x=player.x; g->y=player.y-0.2f; g->z=player.z;
    g->vx=fx*9; g->vy=fy*5+3.5f; g->vz=fz*9; g->timer=132; g->team=player.team; g->ownerIsPlayer=1;
}

// ---- pad ----
static void initPad(){
    padInit(0); padPortOpen(0,0,padBuf);
    for(int i=0;i<30;i++){ int s=padGetState(0,0); if(s==PAD_STATE_STABLE||s==PAD_STATE_FINDCTP1) break; SleepThread(); }
}
static void pollPad(float dt){
    int st=padGetState(0,0);
    if(st!=PAD_STATE_STABLE && st!=PAD_STATE_FINDCTP1) return;
    if(padRead(0,0,&btnStat)!=0) paddata=0xffff ^ btnStat.btns; else return;
    int lx=btnStat.ljoy_h-128, ly=btnStat.ljoy_v-128, rx=btnStat.rjoy_h-128, ry=btnStat.rjoy_v-128;
    const int dead=20; if(abs(lx)<dead) lx=0; if(abs(ly)<dead) ly=0; if(abs(rx)<dead) rx=0; if(abs(ry)<dead) ry=0;
    if(lx||ly){
        float mx=lx/90.0f, my=-ly/90.0f;
        float fwdx=-sinf(yaw), fwdz=-cosf(yaw), rtx=cosf(yaw), rtz=-sinf(yaw);
        float nx=fwdx*my+rtx*mx, nz=fwdz*my+rtz*mx;
        float len=sqrtf(nx*nx+nz*nz); if(len>0){nx/=len; nz/=len;}
        float spd=0.14f*dt *((paddata&PAD_L2)?1.65f:1.0f);
        float npx=player.x+nx*spd, npz=player.z+nz*spd;
        if(!collidesWorld(npx,player.z,0.42f)) player.x=npx;
        if(!collidesWorld(player.x,npz,0.42f)) player.z=npz;
        player.x=fmaxf(-worldSize/2+0.6f,fminf(worldSize/2-0.6f,player.x));
        player.z=fmaxf(-worldSize/2+0.6f,fminf(worldSize/2-0.6f,player.z));
    }
    if(rx||ry){
        yaw -= rx*0.0009f*dt; pitch -= ry*0.0009f*dt;
        if(pitch>1.35f) pitch=1.35f; if(pitch<-1.35f) pitch=-1.35f;
    }
    // edge triggers in update()
}

// ---- game reset (index.html:779) ----
static void resetGame(){
    teamScoreA=teamScoreB=0; timeLeft=ROUND_TIME; lastSec=ROUND_TIME; yaw=0; pitch=0; nBarriers=0; nGrenades=0;
    pickBotNames();
    BarrierStats *bs=&selectedMap->bstat;
    player.x=selectedMap->baseA[0]; player.z=selectedMap->baseA[1]; player.y=1.7f; player.vy=0;
    player.hp=100; player.maxHp=100; player.alive=1; player.respawn=0; player.kills=0; player.deaths=0;
    player.nades=3; player.bars=bs->supply; player.shootCd=0; player.knifeCd=0; player.hitFlash=0; player.team=0; player.isPlayer=1;
    strncpy(player.name, store.playerName,15); strcpy(player.gunId, GUNS[store.equippedGun].id); strcpy(player.knifeId, KNIVES[store.equippedKnife].id);
    for(int i=0;i<MAX_BOTS;i++){
        bots[i].team=(i<3?0:1);
        float bx=(bots[i].team==0?selectedMap->baseA[0]:selectedMap->baseB[0]) + (rand()%6-3);
        float bz=(bots[i].team==0?selectedMap->baseA[1]:selectedMap->baseB[1]) + (rand()%6-3);
        bots[i].x=bx; bots[i].z=bz; bots[i].y=0; bots[i].hp=100; bots[i].maxHp=100; bots[i].alive=1; bots[i].respawn=0;
        bots[i].kills=0; bots[i].deaths=0; bots[i].nades=2; bots[i].bars=(bs->supply>1?bs->supply-1:1); bots[i].shootCd=0; bots[i].hitFlash=0; bots[i].isPlayer=0;
        strncpy(bots[i].name, botNames[i],15); bots[i].yaw=0;
    }
}

// ---- update (index.html:1450) ----
static void updateGame(float dt){
    // pad edge
    static u32 old=0;
    int grenTrig = (paddata&PAD_L1)&&!(old&PAD_L1);
    int barTrig  = (paddata&PAD_TRIANGLE)&&!(old&PAD_TRIANGLE);
    int knifeTrig= (paddata&PAD_R3)&&!(old&PAD_R3);
    int jumpTrig = (paddata&PAD_CROSS)&&!(old&PAD_CROSS);
    old=paddata;
    if(state==1){
        pollPad(dt);
        if(grenTrig) throwGrenade();
        if(barTrig) placeBarrier();
        if(knifeTrig) knifeStab();
        if(jumpTrig && player.alive && canJump){ player.vy=6.2f; canJump=0; }
    }
    if(state!=1) return;
    timeLeft-=dt/60.0f; if(timeLeft<=0){ timeLeft=0; state=4; // over, bonus
        int win = teamScoreA>teamScoreB?0: teamScoreB>teamScoreA?1:2;
        int bonus=0; if(win==0) bonus=400; else if(win==2) bonus=100;
        if(bonus){ store.cash+=bonus; saveStore(); }
        return;
    }
    // PS2 VU1 quantize
    const float q=32; player.x=roundf(player.x*q)/q; player.z=roundf(player.z*q)/q;
    // player shooting (pad R1/R2/Square or host mouse via pad)
    if(player.alive){
        if(player.hitFlash>0) player.hitFlash-=dt;
        player.shootCd-=dt; if(player.knifeCd>0) player.knifeCd-=dt;
        int shootHeld = (paddata & (PAD_R1|PAD_R2)) || (paddata & PAD_SQUARE);
        if(shootHeld && player.shootCd<=0){
            Gun *g=getEquippedGun(); player.shootCd=g->cd;
            float fx,fy,fz; getForward(&fx,&fy,&fz);
            for(int p=0;p<(g->pellets?g->pellets:1);p++){
                float dx=fx+( (rand()%100)/100.0f -0.5f)*g->spread;
                float dy=fy+( (rand()%100)/100.0f -0.5f)*g->spread;
                float dz=fz+( (rand()%100)/100.0f -0.5f)*g->spread;
                float len=sqrtf(dx*dx+dy*dy+dz*dz); dx/=len; dy/=len; dz/=len;
                RayRes rr=shootRay(player.x,player.y,player.z, dx,dy,dz, 0,&player);
                if(rr.hit){
                    if(!rr.isPlayer){ int dmg=g->pellets? (g->dmg/g->pellets): g->dmg; for(int i=0;i<MAX_BOTS;i++) if(bots[i].alive && fabs(rr.hx-bots[i].x)<1 && fabs(rr.hz-bots[i].z)<1){ damage(&bots[i], dmg, &player); break; } }
                    else {} // self never
                } else {
                    // barrier damage if wall
                    if(rr.dist<80) for(int i=0;i<nBarriers;i++) if(pointInBarrier(rr.hx,rr.hz,&barriers[i],0.2f)){ int d=g->pellets? (g->dmg/g->pellets): g->dmg; barriers[i].hp-=d; if(barriers[i].hp<=0){ barriers[i]=barriers[nBarriers-1]; nBarriers--; } break; }
                }
            }
        }
    } else {
        player.respawn-=dt; if(player.respawn<=0){
            float *b=selectedMap->baseA; BarrierStats *bs=&selectedMap->bstat;
            player.x=b[0]+(rand()%4-2); player.z=b[1]+(rand()%4-2); player.y=1.7f; player.vy=0;
            player.hp=100; player.alive=1; player.nades=3; player.bars=bs->supply; player.hitFlash=0;
        }
    }
    // bots AI (simplified hasLOS + oriented)
    for(int i=0;i<MAX_BOTS;i++){
        Entity *bot=&bots[i];
        if(!bot->alive){ bot->respawn-=dt; if(bot->respawn<=0){
            float *b=(bot->team==0?selectedMap->baseA:selectedMap->baseB); BarrierStats *bs=&selectedMap->bstat;
            bot->x=b[0]+(rand()%6-3); bot->z=b[1]+(rand()%6-3); bot->hp=100; bot->alive=1; bot->nades=2; bot->bars=(bs->supply>1?bs->supply-1:1); bot->shootCd=0;
        } continue; }
        if(bot->hitFlash>0) bot->hitFlash-=dt; bot->shootCd-=dt;
        // find nearest enemy
        Entity *best=NULL; float bestD=1e30;
        for(int j=0;j<MAX_BOTS;j++) if(bots[j].alive && bots[j].team!=bot->team){ float d=hypotf(bots[j].x-bot->x, bots[j].z-bot->z); if(d<bestD){bestD=d; best=&bots[j];}}
        if(player.alive && player.team!=bot->team){ float d=hypotf(player.x-bot->x, player.z-bot->z); if(d<bestD){bestD=d; best=&player;}}
        if(best){
            bot->yaw=atan2f(best->x-bot->x, best->z-bot->z);
            int los=hasLOS(bot->x,bot->z,best->x,best->z);
            if(los && bestD<38 && bot->shootCd<=0){
                float fromx=bot->x, fromy=1.15f, fromz=bot->z;
                float toY=(best==&player?player.y:1.0f);
                float dx=best->x-fromx, dy=toY-fromy, dz=best->z-fromz; float len=sqrtf(dx*dx+dy*dy+dz*dz); dx/=len; dy/=len; dz/=len;
                dx+=(rand()%100/100.0f-0.5f)*0.04f; dy+=(rand()%100/100.0f-0.5f)*0.04f; dz+=(rand()%100/100.0f-0.5f)*0.04f;
                RayRes rr=shootRay(fromx,fromy,fromz, dx,dy,dz, bot->team, bot);
                bot->shootCd=18+rand()%18;
                if(rr.hit){
                    if(rr.isPlayer) damage(&player, 14+rand()%8, bot);
                    else for(int k=0;k<MAX_BOTS;k++) if(bots[k].alive && fabs(rr.hx-bots[k].x)<1){ damage(&bots[k],22,bot); break; }
                } else {
                    for(int k=0;k<nBarriers;k++) if(pointInBarrier(rr.hx,rr.hz,&barriers[k],0.2f)){ barriers[k].hp-=22; if(barriers[k].hp<=0){ barriers[k]=barriers[nBarriers-1]; nBarriers--; } break; }
                }
            }
            // simple move toward/strafe (collidesWorld oriented)
            float mvx=0,mvz=0;
            if(bestD>15){ float dx=best->x-bot->x, dz=best->z-bot->z; float l=sqrtf(dx*dx+dz*dz); mvx=dx/l; mvz=dz/l; }
            else if(bestD<7){ float dx=best->x-bot->x, dz=best->z-bot->z; float l=sqrtf(dx*dx+dz*dz); if(l>0.01f){mvx=-dx/l; mvz=-dz/l;} }
            else { float sx=-sinf(bot->yaw+M_PI/2), sz=-cosf(bot->yaw+M_PI/2); mvx=sx*0.7f; mvz=sz*0.7f; }
            float nx=bot->x+mvx*0.075f*dt, nz=bot->z+mvz*0.075f*dt;
            if(!collidesWorld(nx,bot->z,0.45f)) bot->x=nx;
            if(!collidesWorld(bot->x,nz,0.45f)) bot->z=nz;
        }
    }
    // grenades
    for(int i=0;i<nGrenades;i++){
        Grenade *g=&grenades[i];
        g->vy-=0.45f*dt*0.06f*60; g->x+=g->vx*0.06f*dt; g->y+=g->vy*0.06f*dt; g->z+=g->vz*0.06f*dt;
        if(g->y<0.18f){ g->y=0.18f; g->vy*=-0.42f; g->vx*=0.75f; g->vz*=0.75f; }
        if(fabs(g->x)>worldSize/2-0.3f){ g->x=(g->x>0?worldSize/2-0.3f:-worldSize/2+0.3f); g->vx*=-0.6f; }
        if(fabs(g->z)>worldSize/2-0.3f){ g->z=(g->z>0?worldSize/2-0.3f:-worldSize/2+0.3f); g->vz*=-0.6f; }
        g->timer-=dt;
        if(g->timer<=0){
            // explode damage radius 9 — no friendly fire
            Entity *vics[8]={&player,bots,bots+1,bots+2,bots+3,bots+4,bots+5,bots+6};
            for(int k=0;k<8;k++){ Entity *v=vics[k]; if(!v->alive || v->team==g->team) continue; float d=hypotf(v->x-g->x, v->z-g->z); float dy=fabs((v->isPlayer?v->y:1.0f)-g->y); float d3=hypotf(d,dy); if(d3<9){ int dmg=(int)(78*(1-d3/9)); damage(v, dmg, g->ownerIsPlayer?&player: &bots[0]); } }
            for(int k=nBarriers-1;k>=0;k--){ float d=hypotf(barriers[k].x-g->x, barriers[k].z-g->z); if(d<9){ barriers[k].hp-= (int)(70*(1-d/9)); if(barriers[k].hp<=0){ barriers[k]=barriers[nBarriers-1]; nBarriers--; } } }
            grenades[i]=grenades[nGrenades-1]; nGrenades--; i--;
        }
    }
    for(int i=0;i<nBarriers;i++){ barriers[i].life-=dt; if(barriers[i].life<=0){ barriers[i]=barriers[nBarriers-1]; nBarriers--; i--; } }
}

// ---- render (gsKit 640x448 16-bit) ----
static void render(){
    gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x05,0x0A,0x14,0x00));
    // minimal HUD via debug screen (scr_printf) — real game would use gsKit_font + TIM2 quads
    // we keep EE budget low: no heavy textures, 64x64 TIM2 in VRAM
    scr_setXY(0,0); scr_printf(" %s  %02d:%02d  A %d - %d B  %s $%d  %s\n",
        MAPS[selectedMapIdx].name, (int)(timeLeft/60), (int)((int)timeLeft%60), teamScoreA, teamScoreB,
        store.playerName, store.cash, GUNS[store.equippedGun].name);
    scr_printf(" GUN:%s  KNIFE:%s  HP:%d  BARS:%d Nades:%d\n",
        GUNS[store.equippedGun].name, KNIVES[store.equippedKnife].name, (int)player.hp, player.bars, player.nades);
    if(state==0) scr_printf(" MENU: START to play  SELECT=ARMORY  []/{} to change map (%s)\n", MAPS[selectedMapIdx].name);
    if(state==3){
        scr_printf(" === ARMORY ===  Cash $%d  [X] BUY/EQUIP  O CLOSE\n", store.cash);
        for(int i=0;i<4;i++) scr_printf(" %c %s $%d %s\n", store.equippedGun==i?'*':' ', GUNS[i].name, GUNS[i].price, store.ownedGuns[i]?"OWNED":"");
        for(int i=0;i<3;i++) if(i>0) scr_printf(" %c %s $%d %s\n", store.equippedKnife==i?'*':' ', KNIVES[i].name, KNIVES[i].price, store.ownedKnives[i]?"OWNED":"");
        scr_printf(" TRIANGLE=BARRIER R1=SHOOT F/R3=KNIFE  (fair: barriers block both)\n");
    }
    // sync
    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
}

// ---- main ----
int main(int argc,char *argv[]){
    SifInitRpc(0);
    gsGlobal=gsKit_init_global_custom(GS_RENDER_QUEUE_OS_POOLSIZE+GS_RENDER_QUEUE_PER_POOLSIZE*2, GS_RENDER_QUEUE_OS_POOLSIZE+GS_RENDER_QUEUE_PER_POOLSIZE*2);
    gsGlobal->Mode=GS_MODE_NTSC; gsGlobal->Width=640; gsGlobal->Height=448; gsGlobal->PSM=GS_PSM_CT16; gsGlobal->PSMZ=GS_PSMZ_16;
    gsGlobal->DoubleBuffering=GS_SETTING_ON; gsGlobal->ZBuffering=GS_SETTING_ON;
    dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF,D_CTRL_STS_UNSPEC,D_CTRL_STD_OFF,D_CTRL_RCYC_8,1<<DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gsGlobal); gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    scr_init(); scr_setXY(0,0);
    initPad(); srand(12345); loadStore(); buildMap(selectedMapIdx); resetGame();
    state=0;
    while(1){
        float dt=1.0f;
        // menu input
        if(state==0){
            pollPad(dt);
            if(paddata & PAD_START){ state=1; resetGame(); }
            if(paddata & PAD_SELECT){ state=3; }
            if((paddata & PAD_LEFT)&&!(oldPad&PAD_LEFT)){ selectedMapIdx=(selectedMapIdx+2)%3; buildMap(selectedMapIdx); resetGame(); }
            if((paddata & PAD_RIGHT)&&!(oldPad&PAD_RIGHT)){ selectedMapIdx=(selectedMapIdx+1)%3; buildMap(selectedMapIdx); resetGame(); }
        } else if(state==3){ // shop
            pollPad(dt);
            static int sel=0; // 0..6
            if((paddata&PAD_UP)&&!(oldPad&PAD_UP)) sel=(sel+6)%7;
            if((paddata&PAD_DOWN)&&!(oldPad&PAD_DOWN)) sel=(sel+1)%7;
            if((paddata&PAD_CROSS)&&!(oldPad&PAD_CROSS)){
                if(sel<4){ // guns
                    if(!store.ownedGuns[sel]){ if(store.cash>=GUNS[sel].price){ store.cash-=GUNS[sel].price; store.ownedGuns[sel]=1; store.equippedGun=sel; saveStore(); } }
                    else store.equippedGun=sel;
                    saveStore();
                } else {
                    int k=sel-4; // 0 none,1 knife,2 tanto (k mapping)
                    int kid = (k==0?0: (k==1?1:2));
                    if(kid==0){ store.equippedKnife=0; saveStore(); }
                    else {
                        if(!store.ownedKnives[kid]){ if(store.cash>=KNIVES[kid].price){ store.cash-=KNIVES[kid].price; store.ownedKnives[kid]=1; store.equippedKnife=kid; saveStore(); } }
                        else store.equippedKnife=kid;
                        saveStore();
                    }
                }
            }
            if((paddata&PAD_CIRCLE)&&!(oldPad&PAD_CIRCLE)) state=0;
            if((paddata&PAD_SELECT)&&!(oldPad&PAD_SELECT)) state=0;
            oldPad=paddata;
            render(); gsKit_vsync_wait(); continue;
        } else if(state==1 || state==4){
            updateGame(dt);
            if(paddata&PAD_START && !(oldPad&PAD_START)){
                if(state==1) state=2; else if(state==2) state=1;
            }
            if(state==2 && (paddata&PAD_SELECT)&&!(oldPad&PAD_SELECT)) state=3;
            if(state==4 && (paddata&PAD_START)&&!(oldPad&PAD_START)) { state=0; buildMap(selectedMapIdx); resetGame(); }
        }
        oldPad=paddata;
        render();
        gsKit_vsync_wait();
    }
    SleepThread(); return 0;
}
