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
constexpr std::size_t kStimulusFrames = 50u * kSampleRate;
constexpr std::size_t kPreferredFftSize = 32768;
constexpr std::array<std::size_t,3> kStftFftSizes = {512, 2048, 8192};
constexpr std::array<std::size_t,3> kStftHopSizes = {256, 1024, 4096};
constexpr std::array<std::size_t,3> kEnvelopeWindows = {256, 2048, 8192};
constexpr double kMetricEpsilon = 1.0e-12;

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

    const double ratio=static_cast<double>(sr)/static_cast<double>(kSampleRate);
    const std::size_t outFrames=static_cast<std::size_t>(std::llround(static_cast<double>(decoded.size())/ratio));
    out.resize(outFrames);
    for(std::size_t i=0;i<outFrames;++i){
        const double pos=static_cast<double>(i)*ratio;
        const std::size_t i0=std::min(static_cast<std::size_t>(pos),decoded.size()-1);
        const std::size_t i1=std::min(i0+1,decoded.size()-1);
        const double frac=pos-static_cast<double>(i0);
        out[i]=static_cast<float>(decoded[i0]+(decoded[i1]-decoded[i0])*frac);
    }
    return true;
}

struct Biquad { double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; float process(float x){ double y=b0*x+z1; z1=b1*x-a1*y+z2; z2=b2*x-a2*y; return static_cast<float>(y);} };
struct AP { float a=0,s=0; float process(float x){ float y=s+a*x; s=x-a*y; return y; } };
struct Poly {
    std::vector<AP> a,b; float delay=0;
    Poly(std::initializer_list<float> aa,std::initializer_list<float> bb){ for(float x:aa)a.push_back({x,0}); for(float x:bb)b.push_back({x,0}); }
    float r(std::vector<AP>& v,float x){for(auto& s:v)x=s.process(x);return x;}
    void up(float x,float& e,float& o){e=r(a,x);o=r(b,x);} float down(float e,float o){float x=r(a,e), y=r(b,o); float z=.5f*(x+delay);delay=y;return z;}
};

struct Model { Biquad pre,post; std::vector<float>A,B; float pp=0,pn=0,kp=0,kn=0; };
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

void fft(std::vector<std::complex<float>>& a, bool inverse){
    const std::size_t n=a.size();
    for(std::size_t i=1,j=0;i<n;++i){
        std::size_t bit=n>>1;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j) std::swap(a[i],a[j]);
    }
    constexpr float pi=3.14159265358979323846f;
    for(std::size_t len=2;len<=n;len<<=1){
        const float ang=(inverse?2.0f:-2.0f)*pi/static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang),std::sin(ang));
        for(std::size_t i=0;i<n;i+=len){
            std::complex<float> w(1.0f,0.0f);
            for(std::size_t j=0;j<len/2;++j){
                const auto u=a[i+j];
                const auto v=a[i+j+len/2]*w;
                a[i+j]=u+v;
                a[i+j+len/2]=u-v;
                w*=wlen;
            }
        }
    }
    if(inverse){ const float inv=1.0f/static_cast<float>(n); for(auto& v:a)v*=inv; }
}

std::size_t nextPow2(std::size_t n){ std::size_t p=1; while(p<n)p<<=1; return p; }

struct FirFftPlan {
    std::size_t fftSize=0, filterLen=0, hop=0;
    std::vector<std::complex<float>> filterSpectrum;

    explicit FirFftPlan(const std::vector<float>& h){
        filterLen=h.size();
        fftSize=nextPow2(std::max(kPreferredFftSize,filterLen*2));
        hop=fftSize-filterLen+1;
        filterSpectrum.assign(fftSize,{});
        for(std::size_t i=0;i<h.size();++i) filterSpectrum[i]=std::complex<float>(h[i],0.0f);
        fft(filterSpectrum,false);
    }

    void process(const std::vector<float>& input,std::vector<float>& output) const {
        output.assign(input.size(),0.0f);
        std::vector<std::complex<float>> buf(fftSize);
        const std::size_t overlap=filterLen-1;
        for(std::size_t pos=0;pos<input.size();pos+=hop){
            std::fill(buf.begin(),buf.end(),std::complex<float>{});
            for(std::size_t j=0;j<overlap;++j){
                if(pos+j>=overlap) buf[j]=std::complex<float>(input[pos+j-overlap],0.0f);
            }
            const std::size_t count=std::min(hop,input.size()-pos);
            for(std::size_t j=0;j<count;++j) buf[overlap+j]=std::complex<float>(input[pos+j],0.0f);
            fft(buf,false);
            for(std::size_t k=0;k<fftSize;++k) buf[k]*=filterSpectrum[k];
            fft(buf,true);
            for(std::size_t j=0;j<count;++j) output[pos+j]=buf[overlap+j].real();
        }
    }
};

void renderPreB(const Model& base,const std::vector<float>& aout,float pp,float pn,float kp,float kn,std::vector<float>& out){
    Biquad post=base.post;
    Poly u1({.045728147029876709f,.3325011134147644f,.66320204734802246f,.93385583162307739f},{.16808754205703735f,.50448572635650635f,.80378085374832153f});
    Poly u2({.054230779409408569f,.39879697561264038f,.86291784048080444f},{.19969958066940308f,.62109684944152832f});
    Poly d1({.070765949785709381f,.51316756010055542f},{.25785309076309204f,.81731736660003662f});
    Poly d2({.054217524826526642f,.38308733701705933f,.74872094392776489f},{.19679796695709229f,.57313638925552368f,.91429370641708374f});
    out.resize(aout.size());
    auto shape=[&](float x){return x>0?pp*(1-std::exp(-kp*x)):pn*(std::exp(kn*x)-1);};
    for(std::size_t i=0;i<aout.size();++i){
        float a,b,c0,c1;
        u1.up(aout[i],a,b);
        u2.up(a,c0,c1); c0=shape(c0); c1=shape(c1); const float e0=d1.down(c0,c1);
        u2.up(b,c0,c1); c0=shape(c0); c1=shape(c1); const float e1=d1.down(c0,c1);
        out[i]=post.process(d2.down(e0,e1));
    }
}

double fitScale(const std::vector<float>& candidate,const std::vector<float>& target){
    const std::size_t n=std::min(candidate.size(),target.size());
    long double cc=0.0L,ct=0.0L;
    for(std::size_t i=0;i<n;++i){ cc+=static_cast<long double>(candidate[i])*candidate[i]; ct+=static_cast<long double>(candidate[i])*target[i]; }
    return cc>1e-30L?static_cast<double>(ct/cc):1.0;
}

double nmseRange(const std::vector<float>& candidate,const std::vector<float>& target,double scale,std::size_t begin,std::size_t end){
    const std::size_t n=std::min(candidate.size(),target.size());
    begin=std::min(begin,n); end=std::min(end,n); if(end<=begin)return 0.0;
    long double er=0.0L,tt=0.0L;
    for(std::size_t i=begin;i<end;++i){ const long double t=target[i]; const long double d=scale*static_cast<long double>(candidate[i])-t; er+=d*d; tt+=t*t; }
    return tt>1e-30L?static_cast<double>(er/tt):0.0;
}

struct StftReference {
    std::size_t fftSize = 0;
    std::size_t hopSize = 0;
    std::size_t bins = 0;
    std::size_t frames = 0;
    std::vector<float> magnitude;
    long double energy = 0.0L;
};

std::vector<float> hannWindow(std::size_t n){
    std::vector<float> w(n);
    constexpr double pi=3.14159265358979323846;
    if(n<=1){ if(n==1)w[0]=1.0f; return w; }
    for(std::size_t i=0;i<n;++i) w[i]=static_cast<float>(0.5-0.5*std::cos(2.0*pi*double(i)/double(n-1)));
    return w;
}

StftReference buildStftReference(const std::vector<float>& signal,std::size_t fftSize,std::size_t hopSize,double scale){
    StftReference r;
    r.fftSize=fftSize; r.hopSize=hopSize; r.bins=fftSize/2+1;
    r.frames=signal.empty()?0:((signal.size()+hopSize-1)/hopSize);
    r.magnitude.resize(r.frames*r.bins);
    const auto window=hannWindow(fftSize);
    std::vector<std::complex<float>> buf(fftSize);
    for(std::size_t frame=0;frame<r.frames;++frame){
        const std::size_t pos=frame*hopSize;
        std::fill(buf.begin(),buf.end(),std::complex<float>{});
        const std::size_t count=pos<signal.size()?std::min(fftSize,signal.size()-pos):0;
        for(std::size_t i=0;i<count;++i) buf[i]=std::complex<float>(static_cast<float>(scale*signal[pos+i])*window[i],0.0f);
        fft(buf,false);
        for(std::size_t k=0;k<r.bins;++k){
            const float mag=std::abs(buf[k]);
            r.magnitude[frame*r.bins+k]=mag;
            r.energy+=static_cast<long double>(mag)*mag;
        }
    }
    return r;
}

struct StftLossParts { double spectralConvergence=0.0, logMagnitude=0.0, total=0.0; };

StftLossParts stftLoss(const std::vector<float>& candidate,double scale,const StftReference& target){
    if(target.frames==0 || target.bins==0) return {};
    const auto window=hannWindow(target.fftSize);
    std::vector<std::complex<float>> buf(target.fftSize);
    long double diff2=0.0L, logAbs=0.0L;
    std::size_t used=0;
    constexpr double eps=1.0e-7;
    for(std::size_t frame=0;frame<target.frames;++frame){
        const std::size_t pos=frame*target.hopSize;
        std::fill(buf.begin(),buf.end(),std::complex<float>{});
        const std::size_t count=pos<candidate.size()?std::min(target.fftSize,candidate.size()-pos):0;
        for(std::size_t i=0;i<count;++i) buf[i]=std::complex<float>(static_cast<float>(scale*candidate[pos+i])*window[i],0.0f);
        fft(buf,false);
        for(std::size_t k=0;k<target.bins;++k){
            const double cm=std::abs(buf[k]);
            const double tm=target.magnitude[frame*target.bins+k];
            const long double d=cm-tm;
            diff2+=d*d;
            logAbs+=std::abs(std::log(cm+eps)-std::log(tm+eps));
            ++used;
        }
    }
    StftLossParts out;
    out.spectralConvergence=target.energy>1e-30L?std::sqrt(static_cast<double>(diff2/target.energy)):0.0;
    out.logMagnitude=used?static_cast<double>(logAbs/used):0.0;
    // Parallel WaveGAN-style combination: spectral convergence + log-magnitude.
    out.total=out.spectralConvergence+out.logMagnitude;
    return out;
}

struct MultiStftReference { std::array<StftReference,3> resolutions; };

MultiStftReference buildMultiStftReference(const std::vector<float>& target){
    MultiStftReference r;
    for(std::size_t i=0;i<r.resolutions.size();++i)
        r.resolutions[i]=buildStftReference(target,kStftFftSizes[i],kStftHopSizes[i],1.0);
    return r;
}

double multiResolutionStftLoss(const std::vector<float>& candidate,double scale,const MultiStftReference& target){
    double total=0.0;
    for(const auto& r:target.resolutions) total+=stftLoss(candidate,scale,r).total;
    return total/static_cast<double>(target.resolutions.size());
}

std::vector<double> envelopeDb(const std::vector<float>& signal,double scale,std::size_t windowSize){
    std::vector<double> out;
    out.reserve((signal.size()+windowSize-1)/windowSize);
    for(std::size_t pos=0;pos<signal.size();pos+=windowSize){
        const std::size_t end=std::min(signal.size(),pos+windowSize);
        long double ss=0.0L;
        for(std::size_t i=pos;i<end;++i){const long double x=scale*static_cast<long double>(signal[i]);ss+=x*x;}
        const double rms=end>pos?std::sqrt(static_cast<double>(ss/(end-pos))):0.0;
        out.push_back(20.0*std::log10(std::max(rms,1.0e-8)));
    }
    return out;
}

double envelopeMae(const std::vector<double>& candidate,const std::vector<double>& target){
    const std::size_t n=std::min(candidate.size(),target.size()); if(n==0)return 0.0;
    long double e=0.0L; std::size_t used=0;
    for(std::size_t i=0;i<n;++i){
        if(candidate[i]<-95.0 && target[i]<-95.0) continue;
        e+=std::abs(candidate[i]-target[i]); ++used;
    }
    return used?static_cast<double>(e/used):0.0;
}

struct MultiEnvelopeReference { std::array<std::vector<double>,3> scales; };

MultiEnvelopeReference buildMultiEnvelopeReference(const std::vector<float>& target){
    MultiEnvelopeReference r;
    for(std::size_t i=0;i<r.scales.size();++i) r.scales[i]=envelopeDb(target,1.0,kEnvelopeWindows[i]);
    return r;
}

double multiScaleEnvelopeError(const std::vector<float>& candidate,double scale,const MultiEnvelopeReference& target){
    double e=0.0;
    for(std::size_t i=0;i<target.scales.size();++i)
        e+=envelopeMae(envelopeDb(candidate,scale,kEnvelopeWindows[i]),target.scales[i]);
    return e/static_cast<double>(target.scales.size());
}

struct MetricReference {
    MultiStftReference mrstft;
    MultiEnvelopeReference envelope;
};

struct Eval {
    double nmse=1e100, stimulusNmse=1e100, tailNmse=1e100;
    double spectral=1e100, envelope=1e100, composite=1e100;
};

Eval evaluate(const Model& base,const std::vector<float>& aout,const std::vector<float>& target,const FirFftPlan& bPlan,double fixedScale,
              const MetricReference& metricRef,double originalNmse,double originalSpectral,double originalEnvelope,
              float pp,float pn,float kp,float kn){
    std::vector<float> preB,candidate;
    renderPreB(base,aout,pp,pn,kp,kn,preB);
    bPlan.process(preB,candidate);
    const std::size_t n=std::min(candidate.size(),target.size());
    const std::size_t split=std::min(kStimulusFrames,n);
    Eval e;
    e.nmse=nmseRange(candidate,target,fixedScale,0,n);
    e.stimulusNmse=nmseRange(candidate,target,fixedScale,0,split);
    e.tailNmse=nmseRange(candidate,target,fixedScale,split,n);
    e.spectral=multiResolutionStftLoss(candidate,fixedScale,metricRef.mrstft);
    e.envelope=multiScaleEnvelopeError(candidate,fixedScale,metricRef.envelope);
    const double nNorm=e.nmse/std::max(originalNmse,kMetricEpsilon);
    const double sNorm=e.spectral/std::max(originalSpectral,kMetricEpsilon);
    const double dNorm=e.envelope/std::max(originalEnvelope,kMetricEpsilon);
    // Time accuracy remains mandatory, but MR-STFT receives the largest
    // share because the previous NMSE-only refinements could move tonal
    // balance in the wrong direction. Envelope is a dynamics guard.
    e.composite=0.35*nNorm+0.50*sNorm+0.15*dNorm;
    return e;
}

bool compositeBetter(const Eval& candidate,const Eval& best){
    constexpr double tol=1.0e-9;
    return std::isfinite(candidate.composite)
        && candidate.composite < best.composite * (1.0 - tol);
}

struct FinalDecision {
    bool accepted = false;
    std::string reason;
};

FinalDecision finalCandidateDecision(const Eval& candidate,const Eval& original){
    // v1.9.7 keeps the final gate unchanged; BEST is exported for audition. The change in this
    // version is diagnostic: the best searched point is preserved and exposed
    // even when this gate rejects it, so we can tune these tolerances from real
    // data instead of guessing.
    constexpr double maxNmseRegression = 0.0010;       // +0.10 %
    constexpr double maxSpectralRegression = 0.00001;  // +0.001 %
    constexpr double maxEnvelopeRegression = 0.0050;   // +0.50 %
    constexpr double minCompositeImprovement = 0.00025;// 0.025 %

    std::vector<std::string> failures;
    if(!(candidate.composite <= original.composite * (1.0 - minCompositeImprovement)))
        failures.emplace_back("combined loss did not improve by at least 0.025%");
    if(!(candidate.nmse <= original.nmse * (1.0 + maxNmseRegression)))
        failures.emplace_back("NMSE regressed by more than 0.10%");
    if(!(candidate.spectral <= original.spectral * (1.0 + maxSpectralRegression)))
        failures.emplace_back("MR-STFT regressed by more than 0.001%");
    if(!(candidate.envelope <= original.envelope * (1.0 + maxEnvelopeRegression)))
        failures.emplace_back("envelope RMS regressed by more than 0.50%");

    if(failures.empty()) return {true, "accepted by final safety gate"};
    std::string reason;
    for(std::size_t i=0;i<failures.size();++i){
        if(i) reason += "; ";
        reason += failures[i];
    }
    return {false, reason};
}

// Deterministic Halton sequence for a reproducible coarse exploration in
// log-parameter space. This avoids relying on a path of individually accepted
// coordinate steps and can jump directly to a better P/K region.
double halton(unsigned index,unsigned base){
    double f=1.0, r=0.0;
    while(index){ f/=static_cast<double>(base); r+=f*static_cast<double>(index%base); index/=base; }
    return r;
}

bool renderOriginal(const Model& base,const std::vector<float>& aout,const FirFftPlan& bPlan,std::vector<float>& candidate){
    std::vector<float> preB;
    renderPreB(base,aout,base.pp,base.pn,base.kp,base.kn,preB);
    bPlan.process(preB,candidate);
    return !candidate.empty();
}
}

bool refineCloPk(const fs::path& inputClo2048,const fs::path& stimulusWav,const fs::path& targetWav,const fs::path& outputClo2048,const fs::path& bestClo2048,const CloRefineConfig& config,CloRefineStats& stats,std::string& error,const RefineStatusCallback& status){
    std::vector<std::uint8_t> bytes;if(!readFileBytes(inputClo2048,bytes,error))return false;Model m;if(!parseModel(bytes,m,error))return false;
    std::vector<float> in,target;if(!readMono44100(stimulusWav,in,error)||!readMono44100(targetWav,target,error))return false;
    const std::size_t n=std::min(in.size(),target.size());
    if(n<static_cast<std::size_t>(kSampleRate)){error="Not enough rendered audio for full-length refinement.";return false;}
    in.resize(n);target.resize(n);

    if(status)status(L"Refine P/K: precomputing full PRE + FIR A...");
    auto aout=precomputeA(m,in,n);
    if(status)status(L"Refine P/K: preparing fixed FIR B FFT plan...");
    FirFftPlan bPlan(m.B);

    stats.pPosBefore=m.pp;stats.pNegBefore=m.pn;stats.kPosBefore=m.kp;stats.kNegBefore=m.kn;

    // Calibrate output level once from the original CLO. This scale is then
    // frozen for every candidate so the optimiser cannot hide excess gain or
    // compression by re-normalising each P/K proposal independently.
    if(status)status(L"Refine P/K: calibrating original CLO level over full render...");
    std::vector<float> originalCandidate;
    if(!renderOriginal(m,aout,bPlan,originalCandidate)){error="Could not render original CLO for refinement.";return false;}
    const double fixedScale=fitScale(originalCandidate,target);
    stats.outputScale=fixedScale;
    const std::size_t split=std::min(kStimulusFrames,n);
    stats.originalNmse=nmseRange(originalCandidate,target,fixedScale,0,n);
    stats.originalStimulusNmse=nmseRange(originalCandidate,target,fixedScale,0,split);
    stats.originalTailNmse=nmseRange(originalCandidate,target,fixedScale,split,n);
    if(status)status(L"Refine P/K: computing full-render MR-STFT + multi-scale envelope references...");
    MetricReference metricRef{buildMultiStftReference(target), buildMultiEnvelopeReference(target)};
    stats.originalSpectralError=multiResolutionStftLoss(originalCandidate,fixedScale,metricRef.mrstft);
    stats.originalEnvelopeError=multiScaleEnvelopeError(originalCandidate,fixedScale,metricRef.envelope);

    Eval best;
    best.nmse=stats.originalNmse; best.stimulusNmse=stats.originalStimulusNmse; best.tailNmse=stats.originalTailNmse;
    best.spectral=stats.originalSpectralError; best.envelope=stats.originalEnvelopeError; best.composite=1.0;
    const Eval originalEval=best;
    const std::array<float,4> originalP={m.pp,m.pn,m.kp,m.kn};
    std::array<float,4> p=originalP;

    // Phase 1: global/coarse deterministic exploration. Search in log space so
    // positive P/K parameters move proportionally. We allow up to +/-35 % in
    // log-domain around the official conversion. Every point is judged ONLY
    // by the research composite loss; individual metric guards are deferred
    // until the final candidate.
    if(status)status(L"Refine P/K: coarse research-loss exploration (24 candidates)...");
    constexpr std::array<unsigned,4> bases={2,3,5,7};
    constexpr double coarseRadius=0.35;
    for(unsigned sample=1;sample<=24;++sample){
        std::array<float,4> test{};
        for(int j=0;j<4;++j){
            const double u=halton(sample,bases[j]);
            const double q=(2.0*u-1.0)*coarseRadius;
            test[j]=std::max(1e-7f, float(double(originalP[j])*std::exp(q)));
        }
        const auto e=evaluate(m,aout,target,bPlan,fixedScale,metricRef,stats.originalNmse,stats.originalSpectralError,stats.originalEnvelopeError,
                              test[0],test[1],test[2],test[3]);
        if(compositeBetter(e,best)){best=e;p=test;}
    }

    // Phase 2: local pattern refinement around the best coarse point. Unlike
    // v1.9.4, intermediate points do NOT have to beat each original metric.
    // This is intentional: only the final solution is subjected to the strict
    // safety guards below.
    std::array<double,4> step={0.12,0.12,0.12,0.12};
    const int passes=std::clamp(config.passes,1,8);
    for(int pass=0;pass<passes;++pass){
        if(status)status(L"Refine P/K full 70 s: free composite pass "+std::to_wstring(pass+1)+L"/"+std::to_wstring(passes));
        bool any=false;
        for(int j=0;j<4;++j){
            auto localP=p; Eval local=best;
            for(int dir:{-1,1}){
                auto test=p;
                test[j]=std::max(1e-7f,float(double(p[j])*std::exp(dir*step[j])));
                const auto e=evaluate(m,aout,target,bPlan,fixedScale,metricRef,stats.originalNmse,stats.originalSpectralError,stats.originalEnvelopeError,
                                      test[0],test[1],test[2],test[3]);
                if(compositeBetter(e,local)){local=e;localP=test;}
            }
            if(compositeBetter(local,best)){best=local;p=localP;any=true;}
        }
        for(auto& st:step) st*=any?0.62:0.45;
    }

    // Preserve the actual best point found BEFORE applying the final safety
    // gate. v1.9.5 overwrote it with the original CLO, making every rejected
    // run look like 0% in the UI and hiding the useful diagnostic information.
    const Eval searchedBest = best;
    const std::array<float,4> searchedP = p;
    const FinalDecision decision = finalCandidateDecision(searchedBest,originalEval);
    const bool accepted=decision.accepted;

    stats.searchedCandidateAccepted = accepted;
    stats.searchedComposite = searchedBest.composite;
    stats.searchedCompositeImprovementPercent = 100.0 * (originalEval.composite - searchedBest.composite) / std::max(originalEval.composite,kMetricEpsilon);
    stats.searchedNmse = searchedBest.nmse;
    stats.searchedNmseImprovementPercent = stats.originalNmse>0?100.0*(stats.originalNmse-searchedBest.nmse)/stats.originalNmse:0.0;
    stats.searchedStimulusNmse = searchedBest.stimulusNmse;
    stats.searchedStimulusImprovementPercent = stats.originalStimulusNmse>0?100.0*(stats.originalStimulusNmse-searchedBest.stimulusNmse)/stats.originalStimulusNmse:0.0;
    stats.searchedTailNmse = searchedBest.tailNmse;
    stats.searchedTailImprovementPercent = stats.originalTailNmse>0?100.0*(stats.originalTailNmse-searchedBest.tailNmse)/stats.originalTailNmse:0.0;
    stats.searchedSpectralError = searchedBest.spectral;
    stats.searchedSpectralImprovementPercent = stats.originalSpectralError>0?100.0*(stats.originalSpectralError-searchedBest.spectral)/stats.originalSpectralError:0.0;
    stats.searchedEnvelopeError = searchedBest.envelope;
    stats.searchedEnvelopeImprovementPercent = stats.originalEnvelopeError>0?100.0*(stats.originalEnvelopeError-searchedBest.envelope)/stats.originalEnvelopeError:0.0;
    stats.searchedPPos=searchedP[0]; stats.searchedPNeg=searchedP[1]; stats.searchedKPos=searchedP[2]; stats.searchedKNeg=searchedP[3];
    stats.searchedDecisionReason = decision.reason;

    if(status){
        std::wstring msg = L"Refine diagnostics: best searched composite " + std::to_wstring(stats.searchedCompositeImprovementPercent)
            + L"%; NMSE " + std::to_wstring(stats.searchedNmseImprovementPercent)
            + L"%; MR-STFT " + std::to_wstring(stats.searchedSpectralImprovementPercent)
            + L"%; envelope " + std::to_wstring(stats.searchedEnvelopeImprovementPercent)
            + L"%. Final gate: " + (accepted?L"ACCEPTED":L"REJECTED") + L".";
        status(msg);
    }

    // Always export the unconstrained best searched candidate for A/B audition,
    // even when the conservative final gate rejects it. This file is clearly
    // labelled _BEST and never replaces the official/refined output.
    if (!bestClo2048.empty()) {
        auto bestBytes = bytes;
        putf(bestBytes.data()+0x68,searchedP[0]);
        putf(bestBytes.data()+0x6c,searchedP[1]);
        putf(bestBytes.data()+0x70,searchedP[2]);
        putf(bestBytes.data()+0x74,searchedP[3]);
        if(!writeFileBytes(bestClo2048,bestBytes.data(),bestBytes.size(),error)) return false;
    }

    if(!accepted){best=originalEval;p=originalP;}

    stats.refinedNmse=best.nmse;
    stats.refinedStimulusNmse=best.stimulusNmse;
    stats.refinedTailNmse=best.tailNmse;
    stats.refinedSpectralError=best.spectral;
    stats.refinedEnvelopeError=best.envelope;
    stats.spectralImprovementPercent=stats.originalSpectralError>0?100.0*(stats.originalSpectralError-best.spectral)/stats.originalSpectralError:0;
    stats.envelopeImprovementPercent=stats.originalEnvelopeError>0?100.0*(stats.originalEnvelopeError-best.envelope)/stats.originalEnvelopeError:0;
    stats.improved=accepted;
    stats.improvementPercent=stats.originalNmse>0?100.0*(stats.originalNmse-best.nmse)/stats.originalNmse:0;
    stats.stimulusImprovementPercent=stats.originalStimulusNmse>0?100.0*(stats.originalStimulusNmse-best.stimulusNmse)/stats.originalStimulusNmse:0;
    stats.tailImprovementPercent=stats.originalTailNmse>0?100.0*(stats.originalTailNmse-best.tailNmse)/stats.originalTailNmse:0;
    stats.pPosAfter=p[0];stats.pNegAfter=p[1];stats.kPosAfter=p[2];stats.kNegAfter=p[3];

    if(stats.improved){putf(bytes.data()+0x68,p[0]);putf(bytes.data()+0x6c,p[1]);putf(bytes.data()+0x70,p[2]);putf(bytes.data()+0x74,p[3]);}
    else { stats.pPosAfter=m.pp;stats.pNegAfter=m.pn;stats.kPosAfter=m.kp;stats.kNegAfter=m.kn; }

    if(!writeFileBytes(outputClo2048,bytes.data(),bytes.size(),error))return false;
    return true;
}

} // namespace ntc
