#include "../src/animation_blend_fix.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
using namespace animation_blend_fix;
static void __cdecl TestSlerp(const float* a,const float* b,float w,float* out){
    float dot=0;for(int i=0;i<4;++i)dot+=a[i]*b[i];
    const float sign=dot<0?-1.0f:1.0f;dot=std::abs(dot);
    float x=1-w,y=w;
    if(1-dot>.01f){const float t=std::acos(std::clamp(dot,0.0f,1.0f));x=std::sin((1-w)*t)/std::sin(t);y=std::sin(w*t)/std::sin(t);}
    for(int i=0;i<4;++i)out[i]=a[i]*x+b[i]*sign*y;
}
static void __cdecl TestEvent(const Track* t,Event* out){
    const double frame=double(t->phase)*Read<uint32_t>(t->clip+0xc)/Read<uint32_t>(t->clip+0x38);
    const auto* events=reinterpret_cast<const Event*>(Read<uintptr_t>(t->clip+0x40));
    const size_t i=size_t(frame);
    const double w=frame-i;
    for(int j=0;j<5;++j)out->v[j]=float(events[i].v[j]*(1-w)+events[i+1].v[j]*w);
    out->mask=events[i].mask;
    if(t->flags&0x10000){out->v[0]=out->v[1]=out->v[2]=0;out->mask=0;}
}
static bool Close(float a,float b){return std::abs(a-b)<1e-5f;}
// Independent retail-format fixture: three stored keys for two frames.
static bool EndpointKeyRegression(){
    std::array<unsigned char,0x44> header{};
    std::array<Channel,kBones> channels{};
    const uint16_t lengths[]={1,1,1};
    const Quat keys[]={{0,0,0,1},{0,0,.70710678f,.70710678f},{0,0,1,0}};
    const Event events[]={{{.1f,0,0,.85f,1.5f},1},{{.2f,0,0,.85f,1.5f},2},{{.3f,0,0,.85f,1.5f},0}};
    for(auto& ch:channels)ch={3,0,lengths,keys[0].data()};
    auto put=[&](size_t o,uint32_t x){std::memcpy(header.data()+o,&x,4);};
    put(0xc,2);put(0x14,kBones);put(0x1c,reinterpret_cast<uintptr_t>(channels.data()));
    put(0x38,1);put(0x40,reinterpret_cast<uintptr_t>(events));
    Track t{reinterpret_cast<uintptr_t>(header.data()),.75f,0,1};
    Quat result{};
    if(!SampleQuat(t,0,result,TestSlerp)||!Close(result[2],std::sin(3*3.14159265f/8)))return false;
    Frozen f;
    return Capture(f,t,t,.5f,TestSlerp,TestEvent)&&Close(f.events[0].v[0],.25f);
}
struct RetailClip {
    std::vector<unsigned char> bytes;
    Track track{};
    bool Load(const std::filesystem::path& path){
        std::ifstream in(path,std::ios::binary|std::ios::ate);
        if(!in)return false;
        const auto size=in.tellg();if(size<0x50||size>500000)return false;
        bytes.resize(size_t(size));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);
        if(!in)return false;
        const uintptr_t base=reinterpret_cast<uintptr_t>(bytes.data());
        auto fix=[&](size_t offset){
            if(offset+4>bytes.size())return false;
            uint32_t n=Read<uint32_t>(base+offset);
            if(n>=bytes.size())return false;
            n+=base;std::memcpy(bytes.data()+offset,&n,4);return true;
        };
        if(Read<uint32_t>(base+0x14)!=kBones||!fix(0x1c)||!fix(0x40))return false;
        const auto off=Read<uintptr_t>(base+0x1c)-base;
        for(size_t i=0;i<kBones;++i)if(!fix(off+i*12+4)||!fix(off+i*12+8))return false;
        track={base,0,0,Read<uint32_t>(base+0x10)&0xffff};return ValidTrack(track);
    }
};
static bool RetailRegression(const char* path){
    // All unique locomotion BADs referenced by the retail ADM, including
    // the two weapon/movement-dependent forward variants, rolls and jumps.
    const char* names[]={
        "Dt1RunL.bad","Dt1RunR.bad","Dt1RunF.bad","Dt1RunB.bad",
        "Dt1RunFL.bad","Dt1RunBR.bad","Dt1RunFR.bad","Dt1RunBL.bad",
        "Dt1IdleA.bad","Dt1idlA2.bad","Dt1kneel.bad","Dt1Prone.bad",
        "Dt1CrF.bad","Dt1CrFR.bad","Dt1CrR.bad","Dt1CrBR.bad",
        "Dt1CrB.bad","Dt1CrBL.bad","Dt1CrL.bad","Dt1CrFL.bad",
        "Dt1PrF.bad","Dt1PrR.bad","Dt1PrB.bad","Dt1PrL.bad",
        "RunFastK.bad","RunFastW.bad",
        "Dt1RollL.bad","Dt1RollR.bad","Dt1JumpS.bad","Dt1JumpL.bad"
    };
    std::array<RetailClip,std::size(names)> clips;
    size_t count=0;
    for(size_t i=0;i<clips.size();++i){
        if(!clips[i].Load(std::filesystem::path(path)/names[i])){std::printf("Load failed: %s\n",names[i]);return false;}
        for(unsigned phase=0;phase<100;++phase){
            auto t=clips[i].track;t.phase=phase/100.f;
            for(size_t bone=0;bone<kBones;++bone){
                Quat q{};if(!SampleQuat(t,bone,q,TestSlerp)){
                    std::printf("Sample failed: %s phase %u bone %zu\n",names[i],phase,bone);return false;
                }++count;
            }
        }
    }
    Frozen current;
    auto a=clips[0].track,b=clips[1].track;a.phase=.37f;b.phase=.12f;
    if(!Capture(current,a,b,.4f,TestSlerp,TestEvent))return false;
    for(unsigned i=0;i<200;++i){
        b=clips[i%clips.size()].track;b.phase=(i%97)/100.f;
        const auto old=current.events[0];Event next{};TestEvent(&b,&next);
        const auto source=current.Source();
        if(!Capture(current,source,b,3.f/13,TestSlerp,TestEvent))return false;
        for(int j=0;j<5;++j)if(!Close(current.events[0].v[j],old.v[j]*(10.f/13)+next.v[j]*(3.f/13)))return false;
    }
    // Exercise all 42 logical movement IDs, including ADM aliases.
    struct Mapping { uint32_t id; size_t clip; };
    const Mapping states[]={
        {1,8},{2,9},{0xd,10},{0xe,11},
        {0x24,2},{0x25,6},{0x26,1},{0x27,5},{0x28,3},{0x29,7},{0x2a,0},{0x2b,4},
        {0x2c,2},{0x2d,6},{0x2e,1},{0x2f,5},{0x30,3},{0x31,7},{0x32,0},{0x33,4},
        {0x34,12},{0x35,13},{0x36,14},{0x37,15},{0x38,16},{0x39,17},{0x3a,18},{0x3b,19},
        {0x3c,20},{0x3d,21},{0x3e,21},{0x3f,21},{0x40,22},{0x41,23},{0x42,23},{0x43,23},
        {0x64,24},{0x65,25},{0x5f,26},{0x60,27},{0x72,28},{0x73,29}
    };
    std::array<std::array<unsigned char,0x48>,std::size(states)> entries{};
    std::array<uintptr_t,0x100> table{};
    State state{};
    state.table=reinterpret_cast<uintptr_t>(table.data());
    for(size_t i=0;i<std::size(states);++i){
        const auto& m=states[i];
        const uintptr_t ptr=clips[m.clip].track.clip;
        std::memcpy(entries[i].data()+0x40,&ptr,4);
        table[m.id]=reinterpret_cast<uintptr_t>(entries[i].data());
        if(!SupportedMovement(m.id)||!ValidDestination(&state,m.id))return false;
    }
    size_t pairs=0;
    for(const auto& from:states)for(const auto& to:states){
        if(from.id==to.id)continue;
        auto a=clips[from.clip].track,b=clips[to.clip].track;
        a.phase=.37f;b.phase=.12f;
        if(!Capture(current,a,b,.4f,TestSlerp,TestEvent)){
            std::printf("Capture failed: %x -> %x\n",from.id,to.id);return false;
        }
        ++pairs;
    }
    // Repeated W A S D with every forward variant, including both 9/13-tick weights.
    for(size_t w:{size_t(2),size_t(24),size_t(25)}){
        auto a=clips[w].track,b=clips[0].track;
        if(!Capture(current,a,b,.3f,TestSlerp,TestEvent))return false;
        for(int round=0;round<50;++round)for(size_t i:{size_t(3),size_t(1),w,size_t(0)}){
            auto target=clips[i].track;target.phase=.1f;
            const float weight=(i>=24?3.f/9:3.f/13);
            const Event old=current.events[0];Event next{};TestEvent(&target,&next);
            if(!Capture(current,current.Source(),target,weight,TestSlerp,TestEvent))return false;
            for(int j=0;j<5;++j)if(!Close(current.events[0].v[j],old.v[j]*(1-weight)+next.v[j]*weight))return false;
        }
    }
    // Native pending/completed/stopped playback of special animations.
    // Capture must neither mutate the source/target tracks nor replay their
    // discrete events, even near the terminal sample of a queued clip.
    size_t special=0;
    for(size_t i=26;i<30;++i)for(size_t j=0;j<clips.size();++j){
        for(uint32_t flags:{0u,0x40000u,0x20000u,0x30000u}){
            auto a=clips[j].track,b=clips[i].track;
            a.phase=.43f;b.phase=.99999f;b.flags|=flags;
            const Track oldA=a,oldB=b;
            Event ea{},eb{};TestEvent(&a,&ea);TestEvent(&b,&eb);
            if(!Capture(current,a,b,.6f,TestSlerp,TestEvent))return false;
            if(std::memcmp(&a,&oldA,sizeof a)||std::memcmp(&b,&oldB,sizeof b)||current.events[0].mask)return false;
            for(int k=0;k<5;++k)if(!Close(current.events[0].v[k],ea.v[k]*.4f+eb.v[k]*.6f))return false;
            ++special;
        }
    }
    std::printf("PASS retail BAD: %zu bone/phase samples; %zu ordered ID pairs; 200 mixed + 600 circular + %zu special playback captures.\n",count,pairs,special);
    return true;
}
int main(int argc,char** argv){
    if(!EndpointKeyRegression()){std::puts("FAIL: terminal key regression");return 11;}
    if(argc>1&&!RetailRegression(argv[1]))return 12;
    Frozen a,b,c,composite;
    std::array<unsigned char,0x44> header{};
    uint32_t bones=19;std::memcpy(header.data()+0x14,&bones,4);
    std::array<Quat,kBones> p{},q{},r{};
    for(size_t i=0;i<kBones;++i){p[i]={0,0,0,1};q[i]={0,0,.70710678f,.70710678f};r[i]={0,0,-.70710678f,.70710678f};}
    const uintptr_t h=reinterpret_cast<uintptr_t>(header.data());
    a.Prepare(h,p,Event{{.1f,.04f,0,.85f,1.5f},1});
    b.Prepare(h,q,Event{{-.16f,-.02f,0,.9f,1.6f},2});
    c.Prepare(h,r,Event{{.03f,.15f,0,.2f,.4f},4});
    if(!Capture(composite,a.Source(),b.Source(),.25f,TestSlerp,TestEvent))return 1;
    if(!Close(composite.events[0].v[0],.035f)||composite.events[0].mask!=0||
       !Close(composite.keys[0][0][2],std::sin(3.14159265f/16)))return 2;
    for(int i=0;i<100;++i){
        auto target=i%2?b.Source():c.Source();
        Event previous=composite.events[0],next{};
        TestEvent(&target,&next);
        Quat old=composite.keys[0][0],targetPose{},expected{};
        if(!SampleQuat(target,0,targetPose,TestSlerp))return 3;
        TestSlerp(old.data(),targetPose.data(),.2f,expected.data());
        if(!Capture(composite,composite.Source(),target,.2f,TestSlerp,TestEvent))return 4;
        for(int j=0;j<5;++j)if(!Close(composite.events[0].v[j],previous.v[j]*.8f+next.v[j]*.2f))return 5;
        for(int j=0;j<4;++j)if(!Close(composite.keys[0][0][j],expected[j]))return 6;
        if(composite.events[0].mask||composite.Source().phase!=0||composite.Source().step!=0)return 7;
    }
    if(!Capture(composite,composite.Source(),b.Source(),1,TestSlerp,TestEvent)||
       !Close(composite.events[0].v[0],-.16f))return 8;
    auto invalid=b.Source();invalid.phase=1;
    if(Capture(composite,a.Source(),invalid,.2f,TestSlerp,TestEvent))return 9;
    // Check the entire policy, not just four example IDs.
    for(uint32_t id=0;id<256;++id){
        const bool expected=id==1||id==2||id==0xd||id==0xe||
            (id>=0x24&&id<=0x43)||id==0x64||id==0x65||
        id==0x5f||id==0x60||id==0x72||id==0x73;
        if(SupportedMovement(id)!=expected)return 10;
    }
    std::puts("PASS: captured pose/root/bounds, 100 interrupted blends, exact target endpoint, event suppression, invalid track rejection.");
    return 0;
}

