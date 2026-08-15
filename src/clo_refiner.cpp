#include "clo_refiner.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <vector>

namespace ntc {
namespace {
constexpr std::uint32_t kSampleRate = 44100;
constexpr std::size_t kCoeffBase = 0x88;
constexpr std::size_t kAnalysisFrames = 12u * kSampleRate; // experimental v1: fast deterministic subset
constexpr std::size_t kWindow = 1024;
constexpr int kWindowCount = 6;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24); }
float lef(const std::uint8_t* p) { auto u=le32(p); float v{}; std::memcpy(&v,&u,4); return v; }
double led(const std::uint8_t* p) { std::uint64_t u=0; for(int i=0;i<8;++i) u|=static_cast<std::uint64_t>(p[i])<<(8*i); double v{}; std::memcpy(&v,&u,8); return v; }
void putf(std::uint8_t* p, float v) { std::uint32_t u{}; std::memcpy(&u,&v,4); p[0]=u&255; p[1]=(u>>8)&255; p[2]=(u>>16)&255; p[3]=(u>>24)&255; }

bool readMono44100(const fs::path& path, std::vector<float>& out, std::string& error) {
    std::ifstream f(path, std::ios::binary); if(!f){ error="Cannot read WAV: "+pathToUtf8(path); return false; }
    std::array<std::uint8_t,12> h{}; f.read(reinterpret_cast<char*>(h.data()),12);
    if(f.gcount()!=12 || std::memcmp(h.data(),"RIFF",4)!=0 || std::memcmp(h.data()+8,"WAVE",4)!=0){ error="Invalid WAV: "+pathToUtf8(path); return false; }
    std::uint16_t fmt=0,ch=0,bits=0,align=0; std::uint32_t sr=0; std::vector<std::uint8_t> data;
    while(f){ std::array<std::uint8_t,8> c{}; f.read(reinterpret_cast<char*>(c.data()),8); if(f.gcount()!=8) break; auto n=le32(c.data()+4); std::vector<std::uint8_t> b(n); if(n){ f.read(reinterpret_cast<char*>(b.data()),n); if(static_cast<std::uint32_t>(f.gcount())!=n){ error="Truncated WAV"; return false; }} if(n&1) f.seekg(1,std::ios::cur);
        if(std::memcmp(c.data(),"fmt ",4)==0 && n>=16){ fmt=le16(b.data()); ch=le16(b.data()+2); sr=le32(b.data()+4); align=le16(b.data()+12); bits=le16(b.data()+14); if(fmt==0xfffe && n>=40) fmt=le16(b.data()+24); }
        else if(std::memcmp(c.data(),"data",4)==0) data=std::move(b);
    }
    if(sr==0 || ch==0 || align==0 || data.empty()){
        error="Invalid/empty WAV for refinement: "+pathToUtf8(path);
        return false;
    }
    const std::size_t frames=data.size()/align; const int bps=(bits+7)/8;
    if(bps<=0 || static_cast<std::size_t>(bps)*ch>align){
        error="Unsupported WAV block alignment for refinement: "+pathToUtf8(path);
        return false;
    }
    std::vector<float> decoded(frames);
    for(std::size_t i=0;i<frames;++i){ const auto* p=data.data()+i*align; double sum=0; for(std::uint16_t cc=0;cc<ch;++cc){ const auto* q=p+cc*bps; double v=0;
            if(fmt==1 && bits==8) v=(static_cast<int>(q[0])-128)/128.0;
            else if(fmt==1 && bits==16) v=static_cast<std::int16_t>(le16(q))/32768.0;
            else if(fmt==1 && bits==24){ std::int32_t x=q[0]|(q[1]<<8)|(q[2]<<16); if(x&0x800000)x|=0xff000000; v=x/8388608.0; }
            else if(fmt==1 && bits==32){ auto x=static_cast<std::int32_t>(le32(q)); v=x/2147483648.0; }
            else if(fmt==3 && bits==32){ auto u=le32(q); float x{}; std::memcpy(&x,&u,4); v=std::isfinite(x)?x:0; }
            else {
                error="Unsupported WAV format for refinement ("+std::to_string(sr)+" Hz, "+std::to_string(ch)+" ch, "+std::to_string(bits)+" bit, fmt "+std::to_string(fmt)+"): "+pathToUtf8(path);
                return false;
            }
            sum+=v; }
        decoded[i]=static_cast<float>(sum/ch); }

    if(sr==kSampleRate){ out=std::move(decoded); return true; }

    // HTUSBTools may render its NAM reference WAV at a rate different from the
    // 44.1 kHz stimulus. For refinement we adapt both sides to the CLO engine
    // rate instead of rejecting an otherwise valid render. Linear interpolation
    // is sufficient here because the refinement metric is evaluated at 44.1 kHz
    // and the same deterministic adaptation is used for every candidate.
    const double ratio=static_cast<double>(sr)/static_cast<double>(kSampleRate);
    const std::size_t outFrames=static_cast<std::size_t>(std::llround(static_cast<double>(decoded.size())/ratio));
    out.resize(outFrames);
    for(std::size_t i=0;i<outFrames;++i){
        const double pos=static_cast<double>(i)*ratio;
        const std::size_t i0=std::min(static_cast<std::size_t>(pos),decoded.size()-1);
        const std::size_t i1=std::min(i0+1,decoded.size()-1);
        const double f=pos-static_cast<double>(i0);
        out[i]=static_cast<float>(decoded[i0]+(decoded[i1]-decoded[i0])*f);
    }
    return true;
}

struct Biquad { double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; float process(float x){ double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return static_cast<float>(y);} };
struct AP { float a=0,s=0; float process(float x){ float y=s+a*x; s=x-a*y; return y; } };
template<std::size_t N> float run(std::array<AP,N>& a,float x){ for(auto& s:a)x=s.process(x); return x; }
struct Poly {
    std::vector<AP> a,b; float delay=0;
    Poly(std::initializer_list<float> aa,std::initializer_list<float> bb){ for(float x:aa)a.push_back({x,0}); for(float x:bb)b.push_back({x,0}); }
    float r(std::vector<AP>& v,float x){for(auto& s:v)x=s.process(x);return x;}
    void up(float x,float& e,float& o){e=r(a,x);o=r(b,x);} float down(float e,float o){float x=r(a,e), y=r(b,o); float z=.5f*(x+delay);delay=y;return z;}
};

struct Model {
    Biquad pre,post; std::vector<float>A,B; float pp=0,pn=0,kp=0,kn=0;
};
bool parseModel(const std::vector<std::uint8_t>& d, Model& m, std::string& error){
    if(d.size()<0x88 || std::memcmp(d.data(),"VTSI",4)!=0){error="Invalid VTSI CLO.";return false;}
    m.pre={led(d.data()+0x18),led(d.data()+0x20),led(d.data()+0x28),led(d.data()+0x30),led(d.data()+0x38)};
    m.post={led(d.data()+0x40),led(d.data()+0x48),led(d.data()+0x50),led(d.data()+0x58),led(d.data()+0x60)};
    m.pp=lef(d.data()+0x68);m.pn=lef(d.data()+0x6c);m.kp=lef(d.data()+0x70);m.kn=lef(d.data()+0x74);
    auto sa=le32(d.data()+0x78),ca=le32(d.data()+0x7c),sb=le32(d.data()+0x80),cb=le32(d.data()+0x84);
    std::uint64_t need=kCoeffBase+4ull*std::max<std::uint64_t>(sa+ca,sb+cb); if(ca==0||cb==0||need>d.size()){error="Truncated CLO coefficients.";return false;}
    m.A.resize(ca);m.B.resize(cb);for(std::size_t i=0;i<ca;++i)m.A[i]=lef(d.data()+kCoeffBase+4ull*(sa+i));for(std::size_t i=0;i<cb;++i)m.B[i]=lef(d.data()+kCoeffBase+4ull*(sb+i));return true;
}

std::vector<float> precomputeA(const Model& src,const std::vector<float>& in,std::size_t n){
    Model m=src; std::vector<float> hist(m.A.size(),0), out(n); std::size_t ix=0;
    for(std::size_t i=0;i<n;++i){ float x=m.pre.process(in[i]); hist[ix]=x; double s=0;std::size_t h=ix;for(float t:m.A){s+=double(t)*hist[h];h=h? h-1:hist.size()-1;}ix=(ix+1)%hist.size();out[i]=float(s);}return out;
}

std::vector<std::pair<std::size_t,std::size_t>> chooseWindows(const std::vector<float>& target,std::size_t n){
    struct E{double e;std::size_t s;};std::vector<E> blocks; for(std::size_t s=0;s+kWindow<=n;s+=kWindow){double e=0;for(std::size_t i=0;i<kWindow;++i)e+=double(target[s+i])*target[s+i];blocks.push_back({e/kWindow,s});}
    std::sort(blocks.begin(),blocks.end(),[](auto&a,auto&b){return a.e<b.e;});std::vector<std::pair<std::size_t,std::size_t>> w; if(blocks.empty())return w;
    constexpr double q[kWindowCount]={0.10,0.25,0.45,0.65,0.82,0.96}; for(double x:q){auto idx=std::min(blocks.size()-1,std::size_t(x*(blocks.size()-1)));w.push_back({blocks[idx].s,blocks[idx].s+kWindow});} std::sort(w.begin(),w.end());return w;
}

struct Eval { double nmse=1e100,scale=1; };
Eval evaluate(const Model& base,const std::vector<float>& aout,const std::vector<float>& target,const std::vector<std::pair<std::size_t,std::size_t>>& windows,float pp,float pn,float kp,float kn){
    Biquad post=base.post;
    Poly u1({.045728147029876709f,.3325011134147644f,.66320204734802246f,.93385583162307739f},{.16808754205703735f,.50448572635650635f,.80378085374832153f});
    Poly u2({.054230779409408569f,.39879697561264038f,.86291784048080444f},{.19969958066940308f,.62109684944152832f});
    Poly d1({.070765949785709381f,.51316756010055542f},{.25785309076309204f,.81731736660003662f});
    Poly d2({.054217524826526642f,.38308733701705933f,.74872094392776489f},{.19679796695709229f,.57313638925552368f,.91429370641708374f});
    std::vector<float> hist(base.B.size(),0);std::size_t bix=0, wi=0;std::vector<double> c,t;c.reserve(windows.size()*kWindow);t.reserve(c.capacity());
    auto shape=[&](float x){return x>0?pp*(1-std::exp(-kp*x)):pn*(std::exp(kn*x)-1);};
    for(std::size_t i=0;i<aout.size();++i){float a,b,c0,c1;u1.up(aout[i],a,b);u2.up(a,c0,c1);c0=shape(c0);c1=shape(c1);float e0=d1.down(c0,c1);u2.up(b,c0,c1);c0=shape(c0);c1=shape(c1);float e1=d1.down(c0,c1);float y=d2.down(e0,e1);y=post.process(y);hist[bix]=y;
        bool take=wi<windows.size() && i>=windows[wi].first && i<windows[wi].second; if(take){double s=0;std::size_t h=bix;for(float q:base.B){s+=double(q)*hist[h];h=h?h-1:hist.size()-1;}c.push_back(s);t.push_back(target[i]);}
        bix=(bix+1)%hist.size(); if(wi<windows.size() && i+1>=windows[wi].second)++wi;
    }
    if(c.empty())return {};double cc=0,ct=0,tt=0;for(std::size_t i=0;i<c.size();++i){cc+=c[i]*c[i];ct+=c[i]*t[i];tt+=t[i]*t[i];}double scale=cc>1e-30?ct/cc:1;double er=0;for(std::size_t i=0;i<c.size();++i){double d=scale*c[i]-t[i];er+=d*d;}return {tt>1e-30?er/tt:1e100,scale};
}
}

bool refineCloPk(const fs::path& inputClo2048,const fs::path& stimulusWav,const fs::path& targetWav,const fs::path& outputClo2048,const CloRefineConfig& config,CloRefineStats& stats,std::string& error,const RefineStatusCallback& status){
    std::vector<std::uint8_t> bytes;if(!readFileBytes(inputClo2048,bytes,error))return false;Model m;if(!parseModel(bytes,m,error))return false;
    std::vector<float> in,target;if(!readMono44100(stimulusWav,in,error)||!readMono44100(targetWav,target,error))return false;std::size_t n=std::min({in.size(),target.size(),kAnalysisFrames});if(n<kWindow*4){error="Not enough rendered audio for refinement.";return false;}in.resize(n);target.resize(n);
    if(status)status(L"Refine P/K: precomputing fixed PRE + FIR A...");auto aout=precomputeA(m,in,n);auto windows=chooseWindows(target,n);if(windows.empty()){error="Could not select refinement windows.";return false;}
    stats.pPosBefore=m.pp;stats.pNegBefore=m.pn;stats.kPosBefore=m.kp;stats.kNegBefore=m.kn;
    Eval best=evaluate(m,aout,target,windows,m.pp,m.pn,m.kp,m.kn);stats.originalNmse=best.nmse;
    std::array<float,4> p={m.pp,m.pn,m.kp,m.kn};std::array<double,4> step={0.10,0.10,0.10,0.10};
    const int passes=std::clamp(config.passes,1,8);for(int pass=0;pass<passes;++pass){if(status)status(L"Refine P/K: pass "+std::to_wstring(pass+1)+L"/"+std::to_wstring(passes));bool any=false;for(int j=0;j<4;++j){auto localP=p;Eval local=best;for(int dir:{-1,1}){auto test=p;test[j]=std::max(1e-7f,float(double(p[j])*std::exp(dir*step[j])));auto e=evaluate(m,aout,target,windows,test[0],test[1],test[2],test[3]);if(e.nmse<local.nmse){local=e;localP=test;}}if(local.nmse<best.nmse){best=local;p=localP;any=true;}}for(auto& s:step)s*= any?0.65:0.45;}
    stats.refinedNmse=best.nmse;stats.outputScale=best.scale;stats.improved=best.nmse<stats.originalNmse;stats.improvementPercent=stats.originalNmse>0?100.0*(stats.originalNmse-best.nmse)/stats.originalNmse:0;
    stats.pPosAfter=p[0];stats.pNegAfter=p[1];stats.kPosAfter=p[2];stats.kNegAfter=p[3];
    if(stats.improved){putf(bytes.data()+0x68,p[0]);putf(bytes.data()+0x6c,p[1]);putf(bytes.data()+0x70,p[2]);putf(bytes.data()+0x74,p[3]);}
    if(!writeFileBytes(outputClo2048,bytes.data(),bytes.size(),error))return false;return true;
}

} // namespace ntc
