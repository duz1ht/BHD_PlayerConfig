#include "animation_blend_fix.h"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstddef>

namespace animation_blend_fix {
namespace {
static_assert(sizeof(void*) == 4, "DFBHD is x86");
constexpr uintptr_t kLocalPlayer=0x0096C290, kActorPool=0x00715900;
constexpr uintptr_t kRootCall=0x0047CB31, kPaletteCall=0x0048421F;
constexpr uintptr_t kRoot=0x00413F30, kPalette=0x00418060;
constexpr size_t kBones=19, kActorSize=0x29C;
struct Track { uintptr_t clip; float phase, step; uint32_t flags; };
struct State {
    Track primary, secondary;
    int remaining;
    float weight, increment;
    uintptr_t bind, frameEvent;
    uint32_t unused, unused2, id;
    uintptr_t targetClip, targetEntry, table;
    int32_t lastBottom;
};
static_assert(sizeof(Track)==16 && sizeof(State)==0x50 && offsetof(State,id)==0x3c);
struct Channel { uint16_t count, reserved; const uint16_t* lengths; const float* keys; };
static_assert(sizeof(Channel)==12);
using Quat=std::array<float,4>;
struct Event { float v[5]; uint32_t mask; };
static_assert(sizeof(Event)==24);
using Slerp=void(__cdecl*)(const float*,const float*,float,float*);
using SampleEvent=void(__cdecl*)(const Track*,Event*);
using Root=void(__cdecl*)(int32_t*,State*,uintptr_t);
using Palette=void(__cdecl*)(State*,void*);
constexpr auto NativeSlerp=0x005606D0u, NativeEvent=0x00414560u;

template<class T> T Read(uintptr_t p) { T value; std::memcpy(&value,reinterpret_cast<const void*>(p),sizeof value); return value; }
bool Readable(uintptr_t p,size_t n) {
    MEMORY_BASIC_INFORMATION m{};
    return p && n && VirtualQuery(reinterpret_cast<const void*>(p),&m,sizeof m) &&
        m.State==MEM_COMMIT && !(m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) &&
        n<=m.RegionSize && p-reinterpret_cast<uintptr_t>(m.BaseAddress)<=m.RegionSize-n;
}
bool SupportedMovement(uint32_t id) {
    // Native on-foot selector (0x47ca90) and DELTA01.ADM:
    // standing run/walk, crouched and prone locomotion, all eight directions.
    // Forward can be remapped to 0x64/0x65 by weapon and movement modifiers.
    // Roll left/right and jump start/loop use the same 19-channel BAD format.
    // Preserve native secondary playback, queued completion and events.
    // Keep the native duration and destination flags for every variant.
    return id==1||id==2||id==0xd||id==0xe||
        (id>=0x24&&id<=0x43)||id==0x64||id==0x65||
        id==0x5f||id==0x60||id==0x72||id==0x73;
}
bool LocalOnFoot(uintptr_t actor,State* state) {
    if (!actor || actor!=Read<uintptr_t>(kLocalPlayer)) return false;
    const auto pool=Read<uintptr_t>(kActorPool);
    if (!pool||actor<pool||(actor-pool)%kActorSize||(actor-pool)/kActorSize>=256 ||
        !Readable(actor,kActorSize)) return false;
    return state && Read<uintptr_t>(actor+0x27c)==reinterpret_cast<uintptr_t>(state) &&
        Read<int16_t>(actor+0xe2)>0 && Read<uintptr_t>(actor+0x130)==0 &&
        Read<uintptr_t>(actor+0x13c)==0 && Readable(reinterpret_cast<uintptr_t>(state),sizeof(State));
}
bool ValidTrack(const Track& t) {
    if (!Readable(t.clip,0x44)||!std::isfinite(t.phase)||t.phase<0||t.phase>=1) return false;
    const auto frames=Read<uint32_t>(t.clip+0xc),div=Read<uint32_t>(t.clip+0x38);
    return frames>0&&frames<=65535&&div>0&&div<=frames&&
        !(Read<uint32_t>(t.clip+0x10)&2)&&Read<uint32_t>(t.clip+0x14)==kBones&&
        Readable(Read<uintptr_t>(t.clip+0x1c),kBones*sizeof(Channel))&&
        Readable(Read<uintptr_t>(t.clip+0x40)+size_t(t.phase*frames/div)*sizeof(Event),2*sizeof(Event));
}
bool SampleQuat(const Track& t,size_t bone,Quat& out,Slerp slerp) {
    const auto frames=Read<uint32_t>(t.clip+0xc);
    const auto ch=Read<Channel>(Read<uintptr_t>(t.clip+0x1c)+bone*sizeof(Channel));
    // BAD stores an endpoint key at frame == frames. The native loader
    // (0x416fb0) relocates the channel pointers, but does not change count.
    // Retail locomotion clips have frames + 1 keys, not frames.
    if (ch.count<2||uint32_t(ch.count)>frames+1||!Readable(reinterpret_cast<uintptr_t>(ch.lengths),ch.count*2)||
        !Readable(reinterpret_cast<uintptr_t>(ch.keys),size_t(ch.count)*sizeof(Quat))) return false;
    const double f=double(t.phase)*frames;
    unsigned start=0;
    for(unsigned j=0;j+1<ch.count;++j){
        const unsigned length=ch.lengths[j];
        if(length && f<start+length){
            const auto* a=ch.keys+4*j;
            slerp(a,a+4,float((f-start)/length),out.data());
            float norm=0;for(float v:out){if(!std::isfinite(v))return false;norm+=v*v;}
            return norm>0.9f&&norm<1.1f;
        }
        start+=length;
    }
    return false;
}
bool ValidDestination(const State* state,uint32_t id) {
    if(!Readable(state->table+id*4,4))return false;
    const auto entry=Read<uintptr_t>(state->table+id*4);
    if(!Readable(entry,0x48))return false;
    return ValidTrack({Read<uintptr_t>(entry+0x40),0,0,1});
}
struct Frozen {
    alignas(4) std::array<unsigned char,0x44> header{};
    std::array<Channel,kBones> channels{};
    std::array<std::array<Quat,2>,kBones> keys{};
    std::array<Event,2> events{};
    std::array<uint16_t,2> lengths{1,1};
    template<class T> void Put(size_t offset,T value){std::memcpy(header.data()+offset,&value,sizeof value);}
    Track Source() const {return {reinterpret_cast<uintptr_t>(header.data()),0,0,1};}
    void Prepare(uintptr_t original,const std::array<Quat,kBones>& pose,const Event& event){
        std::memcpy(header.data(),reinterpret_cast<const void*>(original),header.size());
        for(size_t i=0;i<kBones;++i){
            keys[i]={pose[i],pose[i]};
            channels[i]={2,0,lengths.data(),keys[i][0].data()};
        }
        events={event,event};
        Put(0xc,uint32_t{1});Put(0x1c,channels.data());Put(0x38,uint32_t{1});
        Put(0x40,events.data());
    }
};
struct Snapshot {
    Frozen frozen;
    uintptr_t actor=0, definition=0, identity=0, rig=0;
    State* state=nullptr;
    uintptr_t primary=0,secondary=0;
    uint32_t id=0;
    int remaining=0;
    bool valid=false;
    bool Matches(uintptr_t a,State* s) const {
        return valid&&a==actor&&s==state&&s->primary.clip==primary&&s->secondary.clip==secondary&&
            s->id==id&&s->remaining==remaining&&remaining>0&&
            Read<uintptr_t>(a+0x24)==definition&&Read<uintptr_t>(a+0x4c)==identity&&
            Read<uintptr_t>(a+0x38)==rig;
    }
    void Remember(uintptr_t a,State* s){
        actor=a;state=s;definition=Read<uintptr_t>(a+0x24);identity=Read<uintptr_t>(a+0x4c);
        rig=Read<uintptr_t>(a+0x38);primary=s->primary.clip;secondary=s->secondary.clip;
        id=s->id;remaining=s->remaining;valid=remaining>0&&secondary!=0;
    }
};
Snapshot g_snapshot;
bool g_enabled=false;
// Snapshot raw BAD channels and five root/bounds components at the exact
// current blend weight. Keep native discrete event ownership on the target.
bool Capture(Frozen& dst,const Track& a,const Track& b,float w,Slerp slerp,SampleEvent sample){
    if (!ValidTrack(a)||!ValidTrack(b)||!std::isfinite(w)||w<0||w>1) return false;
    std::array<Quat,kBones> pose{};
    for(size_t i=0;i<kBones;++i){
        Quat p{},q{};
        if(!SampleQuat(a,i,p,slerp)||!SampleQuat(b,i,q,slerp))return false;
        slerp(p.data(),q.data(),w,pose[i].data());
        for(float v:pose[i])if(!std::isfinite(v))return false;
    }
    Event e{},f{},result{};
    sample(&a,&e);sample(&b,&f);
    for(size_t i=0;i<5;++i){
        result.v[i]=e.v[i]*(1-w)+f.v[i]*w;
        if(!std::isfinite(result.v[i])||std::abs(result.v[i])>100)return false;
    }
    // When a is already frozen, its header is dst.header (self-copy).
    if(a.clip==reinterpret_cast<uintptr_t>(dst.header.data())) dst.Prepare(b.clip,pose,result);
    else dst.Prepare(a.clip,pose,result);
    return true;
}

void __cdecl UpdateRoot(int32_t* output,State* s,uintptr_t actor){
    const auto original=reinterpret_cast<Root>(kRoot);
    if(!g_enabled||actor!=Read<uintptr_t>(kLocalPlayer)){original(output,s,actor);return;}
    if(!LocalOnFoot(actor,s)){g_snapshot.valid=false;original(output,s,actor);return;}
    bool frozen=g_snapshot.Matches(actor,s);
    if(!frozen)g_snapshot.valid=false;
    const auto requested=Read<uint32_t>(actor+0x164);
    if(requested!=s->id){
        if(s->secondary.clip&&s->remaining>0&&SupportedMovement(s->id)&&SupportedMovement(requested)&&ValidDestination(s,requested)){
            frozen=Capture(g_snapshot.frozen,frozen?g_snapshot.frozen.Source():s->primary,
                s->secondary,s->weight,reinterpret_cast<Slerp>(NativeSlerp),
                reinterpret_cast<SampleEvent>(NativeEvent));
        }else frozen=false;
    }
    if(!frozen){g_snapshot.valid=false;original(output,s,actor);return;}
    // A private synthetic source exists only during this native call.
    // The game's timers, flags, secondary clip, events and collision math
    // remain native. Never publish a synthetic BAD pointer to the game.
    const Track saved=s->primary;
    const Track source=g_snapshot.frozen.Source();
    s->primary=source;
    original(output,s,actor);
    if(s->primary.clip==source.clip)s->primary=saved;
    if(s->secondary.clip && s->remaining>0)g_snapshot.Remember(actor,s);
    else g_snapshot.valid=false;
}

void __cdecl BuildPalette(State* s,void* output){
    const auto original=reinterpret_cast<Palette>(kPalette);
    const auto actor=Read<uintptr_t>(kLocalPlayer);
    if(!g_enabled||!LocalOnFoot(actor,s)||!g_snapshot.Matches(actor,s)){original(s,output);return;}
    State copy=*s;
    copy.primary=g_snapshot.frozen.Source();
    original(&copy,output);
}
bool Match(uintptr_t p,const unsigned char* bytes,size_t n){
    return Readable(p,n)&&std::memcmp(reinterpret_cast<const void*>(p),bytes,n)==0;
}
bool Write(uintptr_t p,const unsigned char* bytes,size_t n){
    DWORD old=0;auto* dst=reinterpret_cast<void*>(p);
    if(!VirtualProtect(dst,n,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(dst,bytes,n);FlushInstructionCache(GetCurrentProcess(),dst,n);
    DWORD ignored;VirtualProtect(dst,n,old,&ignored);return true;
}
std::array<unsigned char,5> Call(uintptr_t site,uintptr_t target){
    std::array<unsigned char,5> b{0xe8};const uint32_t rel=uint32_t(target-site-5);
    std::memcpy(b.data()+1,&rel,4);return b;
}
} // namespace
bool Install(bool enabled){
    if(!enabled)return false;
    if(g_enabled)return true;
    if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000)return false;
    const auto rootOld=Call(kRootCall,kRoot),paletteOld=Call(kPaletteCall,kPalette);
    const unsigned char rootSig[]={0x55,0x8b,0xec,0x83,0xec,0x50,0x53,0x8b,0x5d,0x10};
    const unsigned char paletteSig[]={0x55,0x8b,0xec,0x83,0xec,0x50,0x53,0x56,0x8b,0x75,0x08};
    const unsigned char eventSig[]={0x55,0x8b,0xec,0x83,0xec,0x08,0x53,0x56,0x57,0x8b,0x7d,0x08};
    const unsigned char slerpSig[]={0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x4d,0x0c,0xd9,0x41,0x0c};
    if(!Match(kRootCall,rootOld.data(),5)||!Match(kPaletteCall,paletteOld.data(),5)||
        !Match(kRoot,rootSig,sizeof rootSig)||!Match(kPalette,paletteSig,sizeof paletteSig)||!Match(NativeEvent,eventSig,sizeof eventSig)||
        !Match(NativeSlerp,slerpSig,sizeof slerpSig))return false;
    const auto a=Call(kRootCall,reinterpret_cast<uintptr_t>(&UpdateRoot));
    const auto b=Call(kPaletteCall,reinterpret_cast<uintptr_t>(&BuildPalette));
    if(!Write(kRootCall,a.data(),5))return false;
    if(!Write(kPaletteCall,b.data(),5)){Write(kRootCall,rootOld.data(),5);return false;}
    g_enabled=true;return true;
}
} // namespace animation_blend_fix

