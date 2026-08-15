#include "clo_refiner.hpp"
#include "common.hpp"
#include "corrective_ir.hpp"

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
constexpr std::size_t kLevelWindow = 2048;
constexpr double kMetricEpsilon = 1.0e-12;
constexpr std::size_t kABandCount = 10;
constexpr std::array<double,kABandCount> kABandHz = {40.0, 80.0, 160.0, 315.0, 630.0, 1250.0, 2500.0, 5000.0, 10000.0, 18000.0};

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

std::vector<float> precomputePre(const Model& src,const std::vector<float>& in,std::size_t n){
    Model m=src; std::vector<float> out(n);
    for(std::size_t i=0;i<n;++i) out[i]=m.pre.process(in[i]);
    return out;
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

void renderPreB(const Model& base,const std::vector<float>& aout,float pp,float pn,float kp,float kn,std::vector<float>& out);

double interpABandDb(double hz,const std::array<double,kABandCount>& db){
    if(hz<=kABandHz.front()) return db.front();
    if(hz>=kABandHz.back()) return db.back();
    const double x=std::log(std::max(hz,1.0));
    for(std::size_t i=0;i+1<kABandCount;++i){
        if(hz<=kABandHz[i+1]){
            const double x0=std::log(kABandHz[i]), x1=std::log(kABandHz[i+1]);
            const double t=(x-x0)/std::max(x1-x0,1.0e-12);
            return db[i]+(db[i+1]-db[i])*t;
        }
    }
    return db.back();
}

std::vector<float> synthesizeA(const std::vector<float>& original,const std::array<double,kABandCount>& bandDb){
    // Preserve the original phase response and modify only a smooth magnitude
    // envelope. A 512-point workspace gives sufficient interpolation density
    // for the 128-tap Block A while keeping the resulting FIR causal/truncated.
    constexpr std::size_t N=512;
    std::vector<std::complex<float>> H(N);
    for(std::size_t i=0;i<std::min(original.size(),N);++i) H[i]=std::complex<float>(original[i],0.0f);
    fft(H,false);
    for(std::size_t k=0;k<=N/2;++k){
        const double hz=double(k)*double(kSampleRate)/double(N);
        const double db=interpABandDb(hz,bandDb);
        const float g=static_cast<float>(std::pow(10.0,db/20.0));
        H[k]*=g;
        if(k>0 && k<N/2) H[N-k]*=g;
    }
    fft(H,true);
    std::vector<float> out(original.size());
    for(std::size_t i=0;i<out.size();++i) out[i]=H[i].real();
    return out;
}

void renderAPlusPk(const Model& base,const std::vector<float>& preOut,const FirFftPlan& bPlan,
                   const std::array<double,kABandCount>& aBandDb,
                   float pp,float pn,float kp,float kn,
                   std::vector<float>& candidate){
    const auto a=synthesizeA(base.A,aBandDb);
    FirFftPlan aPlan(a);
    std::vector<float> aout,preB;
    aPlan.process(preOut,aout);
    renderPreB(base,aout,pp,pn,kp,kn,preB);
    bPlan.process(preB,candidate);
}

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



// v2.1: input-referenced spectral profile, intended to track the same broad
// frequency-response contour that is visible in external transfer-function
// analysers.  The known stimulus is used to remove its own spectral tilt.  We
// average Welch power spectra over the first 50 seconds and collapse them into
// equal-log-frequency bands so treble bins do not dominate merely because a
// linear FFT has more bins there.
constexpr std::size_t kResponseFftSize = 4096;
constexpr std::size_t kResponseHopSize = 2048;
constexpr std::size_t kResponseBandCount = 96;
constexpr double kResponseMinHz = 30.0;
constexpr double kResponseMaxHz = 20000.0;

struct ResponseSpectralReference {
    std::array<double,kResponseBandCount> targetDb{};
    std::array<double,kResponseBandCount> inputPower{};
    std::array<bool,kResponseBandCount> valid{};
};

std::size_t responseBandForHz(double hz){
    if(hz<=kResponseMinHz) return 0;
    if(hz>=kResponseMaxHz) return kResponseBandCount-1;
    const double t=std::log(hz/kResponseMinHz)/std::log(kResponseMaxHz/kResponseMinHz);
    return std::min<std::size_t>(kResponseBandCount-1,static_cast<std::size_t>(t*kResponseBandCount));
}

std::array<double,kResponseBandCount> responseTransferDb(const std::vector<float>& input,
                                                          const std::vector<float>& output,
                                                          double outputScale,
                                                          std::array<double,kResponseBandCount>* inputPowerOut=nullptr){
    std::array<long double,kResponseBandCount> pin{}, pout{};
    std::array<std::size_t,kResponseBandCount> count{};
    const std::size_t n=std::min({input.size(),output.size(),kStimulusFrames});
    const auto window=hannWindow(kResponseFftSize);
    std::vector<std::complex<float>> xi(kResponseFftSize), yo(kResponseFftSize);
    for(std::size_t pos=0;pos<n;pos+=kResponseHopSize){
        std::fill(xi.begin(),xi.end(),std::complex<float>{});
        std::fill(yo.begin(),yo.end(),std::complex<float>{});
        const std::size_t used=std::min(kResponseFftSize,n-pos);
        long double frameInput=0.0L;
        for(std::size_t i=0;i<used;++i){
            const float x=input[pos+i]*window[i];
            const float y=static_cast<float>(outputScale*output[pos+i])*window[i];
            xi[i]=std::complex<float>(x,0.0f); yo[i]=std::complex<float>(y,0.0f);
            frameInput+=static_cast<long double>(x)*x;
        }
        if(frameInput<1.0e-12L) continue;
        fft(xi,false); fft(yo,false);
        for(std::size_t k=1;k<=kResponseFftSize/2;++k){
            const double hz=double(k)*double(kSampleRate)/double(kResponseFftSize);
            if(hz<kResponseMinHz || hz>kResponseMaxHz) continue;
            const std::size_t b=responseBandForHz(hz);
            const long double ip=std::norm(xi[k]);
            const long double op=std::norm(yo[k]);
            pin[b]+=ip; pout[b]+=op; ++count[b];
        }
    }
    long double maxPin=0.0L; for(auto v:pin) maxPin=std::max(maxPin,v);
    std::array<double,kResponseBandCount> db{};
    for(std::size_t b=0;b<kResponseBandCount;++b){
        if(inputPowerOut) (*inputPowerOut)[b]=static_cast<double>(pin[b]);
        if(count[b]==0 || pin[b] <= maxPin*1.0e-8L){ db[b]=std::numeric_limits<double>::quiet_NaN(); continue; }
        db[b]=10.0*std::log10(std::max(static_cast<double>(pout[b]/pin[b]),1.0e-20));
    }
    return db;
}

ResponseSpectralReference buildResponseSpectralReference(const std::vector<float>& input,const std::vector<float>& target){
    ResponseSpectralReference r;
    r.targetDb=responseTransferDb(input,target,1.0,&r.inputPower);
    for(std::size_t b=0;b<kResponseBandCount;++b) r.valid[b]=std::isfinite(r.targetDb[b]);
    return r;
}

double responseSpectralError(const std::vector<float>& input,const std::vector<float>& candidate,double scale,
                             const ResponseSpectralReference& target){
    const auto cand=responseTransferDb(input,candidate,scale,nullptr);
    // Compare spectral SHAPE.  A single broad output-level offset is removed,
    // because level is already frozen by the original-CLO calibration and the
    // external analyser comparison is about the contour itself.
    long double meanDelta=0.0L; std::size_t used=0;
    for(std::size_t b=0;b<kResponseBandCount;++b){
        if(!target.valid[b] || !std::isfinite(cand[b])) continue;
        meanDelta+=cand[b]-target.targetDb[b]; ++used;
    }
    if(!used) return 0.0;
    meanDelta/=static_cast<long double>(used);
    long double mae=0.0L;
    for(std::size_t b=0;b<kResponseBandCount;++b){
        if(!target.valid[b] || !std::isfinite(cand[b])) continue;
        mae+=std::abs((cand[b]-target.targetDb[b])-static_cast<double>(meanDelta));
    }
    return static_cast<double>(mae/used);
}

struct LevelReference {
    std::size_t windowSize = kLevelWindow;
    std::vector<std::uint8_t> band; // 0=inactive, 1=low, 2=mid, 3=high
    double lowThreshold = 0.0;
    double highThreshold = 0.0;
};

LevelReference buildLevelReference(const std::vector<float>& stimulus){
    LevelReference r;
    const std::size_t windows=(stimulus.size()+r.windowSize-1)/r.windowSize;
    r.band.assign(windows,0);
    std::vector<double> rms(windows,0.0), active;
    active.reserve(windows);
    double maxRms=0.0;
    for(std::size_t w=0;w<windows;++w){
        const std::size_t begin=w*r.windowSize;
        const std::size_t end=std::min(stimulus.size(),begin+r.windowSize);
        long double ss=0.0L;
        for(std::size_t i=begin;i<end;++i){ const long double x=stimulus[i]; ss+=x*x; }
        const double v=end>begin?std::sqrt(static_cast<double>(ss/(end-begin))):0.0;
        rms[w]=v; maxRms=std::max(maxRms,v);
    }
    const double activeFloor=maxRms*1.0e-3; // -60 dB relative to the loudest stimulus window
    for(double v:rms) if(v>activeFloor) active.push_back(v);
    if(active.size()<3){
        for(std::size_t w=0;w<windows;++w) if(rms[w]>activeFloor) r.band[w]=2;
        return r;
    }
    std::sort(active.begin(),active.end());
    auto quantile=[&](double q){
        const double pos=q*double(active.size()-1);
        const std::size_t i0=static_cast<std::size_t>(pos);
        const std::size_t i1=std::min(i0+1,active.size()-1);
        const double f=pos-double(i0);
        return active[i0]+(active[i1]-active[i0])*f;
    };
    r.lowThreshold=quantile(1.0/3.0);
    r.highThreshold=quantile(2.0/3.0);
    for(std::size_t w=0;w<windows;++w){
        const double v=rms[w];
        if(v<=activeFloor) r.band[w]=0;
        else if(v<=r.lowThreshold) r.band[w]=1;
        else if(v<=r.highThreshold) r.band[w]=2;
        else r.band[w]=3;
    }
    return r;
}

std::array<double,3> levelNmse(const std::vector<float>& candidate,const std::vector<float>& target,double scale,const LevelReference& levels){
    std::array<long double,3> er{0,0,0}, tt{0,0,0};
    const std::size_t n=std::min(candidate.size(),target.size());
    for(std::size_t w=0;w<levels.band.size();++w){
        const auto b=levels.band[w]; if(b<1 || b>3) continue;
        const std::size_t begin=w*levels.windowSize;
        if(begin>=n) break;
        const std::size_t end=std::min(n,begin+levels.windowSize);
        const std::size_t bi=static_cast<std::size_t>(b-1);
        for(std::size_t i=begin;i<end;++i){
            const long double t=target[i];
            const long double d=scale*static_cast<long double>(candidate[i])-t;
            er[bi]+=d*d; tt[bi]+=t*t;
        }
    }
    std::array<double,3> out{};
    for(std::size_t i=0;i<3;++i) out[i]=tt[i]>1e-30L?static_cast<double>(er[i]/tt[i]):0.0;
    return out;
}

struct MetricReference {
    MultiStftReference mrstft;
    MultiEnvelopeReference envelope;
    ResponseSpectralReference responseSpectral;
    LevelReference levels;
    std::array<double,3> originalLevelNmse{};
};

struct Eval {
    double nmse=1e100, stimulusNmse=1e100, tailNmse=1e100;
    double spectral=1e100, responseSpectral=1e100, envelope=1e100, levelBalanced=1e100, composite=1e100;
    std::array<double,3> levelNmse{1e100,1e100,1e100};
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
    e.levelNmse=levelNmse(candidate,target,fixedScale,metricRef.levels);

    const double nNorm=e.nmse/std::max(originalNmse,kMetricEpsilon);
    const double sNorm=e.spectral/std::max(originalSpectral,kMetricEpsilon);
    const double dNorm=e.envelope/std::max(originalEnvelope,kMetricEpsilon);
    double levelNorm=0.0; int levelCount=0;
    for(std::size_t i=0;i<3;++i){
        if(metricRef.originalLevelNmse[i]>kMetricEpsilon){
            levelNorm += e.levelNmse[i]/metricRef.originalLevelNmse[i];
            ++levelCount;
        }
    }
    e.levelBalanced=levelCount?levelNorm/double(levelCount):nNorm;

    // v1.9.9: P/K are nonlinearity parameters, so waveform accuracy at the
    // actual excitation levels gets most of the weight. MR-STFT and envelope
    // remain secondary safeguards, not the main optimisation target.
    e.composite=0.35*nNorm + 0.35*e.levelBalanced + 0.15*sNorm + 0.15*dNorm;
    return e;
}

bool compositeBetter(const Eval& candidate,const Eval& best){
    constexpr double tol=1.0e-9;
    return std::isfinite(candidate.composite)
        && candidate.composite < best.composite * (1.0 - tol);
}

// v1.9.9: constrain the P/K search itself, instead of first optimizing an
// unconstrained scalar loss and rejecting the result afterwards. P/K directly
// control the static nonlinearity, so low/mid/high excitation fidelity is a
// structural requirement. MR-STFT and envelope remain useful secondary
// objectives, but they cannot be purchased by noticeably worsening temporal
// behaviour at any excitation level.
bool feasiblePkCandidate(const Eval& candidate,const Eval& original){
    constexpr double maxGlobalNmseRegression = 0.0000;  // global NMSE must not worsen
    constexpr double maxLevelRegression = 0.0050;       // <= +0.50% in low/mid/high
    constexpr double maxSpectralRegression = 0.05;      // secondary guard
    constexpr double maxEnvelopeRegression = 0.05;      // secondary guard

    if(!std::isfinite(candidate.composite)) return false;
    if(candidate.nmse > original.nmse * (1.0 + maxGlobalNmseRegression)) return false;
    for(std::size_t i=0;i<3;++i){
        if(original.levelNmse[i] > kMetricEpsilon
           && candidate.levelNmse[i] > original.levelNmse[i] * (1.0 + maxLevelRegression)) return false;
    }
    if(candidate.spectral > original.spectral * (1.0 + maxSpectralRegression)) return false;
    if(candidate.envelope > original.envelope * (1.0 + maxEnvelopeRegression)) return false;
    return true;
}

bool constrainedBetter(const Eval& candidate,const Eval& best,const Eval& original){
    return feasiblePkCandidate(candidate,original) && compositeBetter(candidate,best);
}

struct FinalDecision {
    bool accepted = false;
    std::string reason;
};

FinalDecision finalCandidateDecision(const Eval& candidate,const Eval& original){
    // P/K-specific safety gate. A refined P/K set must not buy a nicer average
    // spectrum by worsening the actual waveform or any excitation-level band.
    constexpr double maxNmseRegression = 0.0000;       // no global temporal regression
    constexpr double maxLevelRegression = 0.0050;      // max +0.50% in low/mid/high
    constexpr double maxSpectralRegression = 0.05;     // MR-STFT is secondary for P/K
    constexpr double maxEnvelopeRegression = 0.05;     // dynamics secondary guard
    constexpr double minCompositeImprovement = 0.00025;// 0.025 %

    std::vector<std::string> failures;
    if(!(candidate.composite <= original.composite * (1.0 - minCompositeImprovement)))
        failures.emplace_back("combined P/K loss did not improve by at least 0.025%");
    if(!(candidate.nmse <= original.nmse * (1.0 + maxNmseRegression)))
        failures.emplace_back("global NMSE regressed");
    static constexpr const char* names[3]={"low-level NMSE","mid-level NMSE","high-level NMSE"};
    for(std::size_t i=0;i<3;++i){
        if(original.levelNmse[i]>kMetricEpsilon && !(candidate.levelNmse[i] <= original.levelNmse[i]*(1.0+maxLevelRegression)))
            failures.emplace_back(std::string(names[i])+" regressed by more than 0.50%");
    }
    if(!(candidate.spectral <= original.spectral * (1.0 + maxSpectralRegression)))
        failures.emplace_back("MR-STFT regressed by more than 5%");
    if(!(candidate.envelope <= original.envelope * (1.0 + maxEnvelopeRegression)))
        failures.emplace_back("envelope RMS regressed by more than 5%");

    if(failures.empty()) return {true, "accepted by P/K nonlinearity safety gate"};
    std::string reason;
    for(std::size_t i=0;i<failures.size();++i){ if(i) reason += "; "; reason += failures[i]; }
    return {false,reason};
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

bool refineCloAPlusPk(const fs::path& inputClo2048,const fs::path& stimulusWav,const fs::path& targetWav,const fs::path& outputClo2048,const fs::path& bestClo2048,const CloRefineConfig& config,CloRefineStats& stats,std::string& error,const RefineStatusCallback& status){
    std::vector<std::uint8_t> bytes;
    if(!readFileBytes(inputClo2048,bytes,error)) return false;
    Model m;
    if(!parseModel(bytes,m,error)) return false;
    if(m.A.size()!=128){ error="v2.1 A+P/K refiner expects a 128-tap Block A."; return false; }

    std::vector<float> in,target;
    if(!readMono44100(stimulusWav,in,error)||!readMono44100(targetWav,target,error)) return false;
    const std::size_t n=std::min(in.size(),target.size());
    if(n<static_cast<std::size_t>(kSampleRate)){ error="Not enough rendered audio for full-length v2.1 refinement."; return false; }
    in.resize(n); target.resize(n);

    if(status) status(L"v2.1 A+P/K: precomputing PRE over the complete render...");
    const auto preOut=precomputePre(m,in,n);
    if(status) status(L"v2.1 A+P/K: preparing fixed FIR B FFT plan...");
    FirFftPlan bPlan(m.B);

    stats.pPosBefore=m.pp; stats.pNegBefore=m.pn; stats.kPosBefore=m.kp; stats.kNegBefore=m.kn;

    std::array<double,kABandCount> originalABands{};
    std::vector<float> originalCandidate;
    renderAPlusPk(m,preOut,bPlan,originalABands,m.pp,m.pn,m.kp,m.kn,originalCandidate);
    if(originalCandidate.empty()){ error="Could not render original CLO for v2.1 refinement."; return false; }

    // Freeze one output-level calibration from the official CLO. Candidates
    // are never independently normalized, so A/P/K changes must account for
    // their real effect on drive and dynamics.
    const double fixedScale=fitScale(originalCandidate,target);
    stats.outputScale=fixedScale;
    const std::size_t split=std::min(kStimulusFrames,n);
    stats.originalNmse=nmseRange(originalCandidate,target,fixedScale,0,n);
    stats.originalStimulusNmse=nmseRange(originalCandidate,target,fixedScale,0,split);
    stats.originalTailNmse=nmseRange(originalCandidate,target,fixedScale,split,n);

    if(status) status(L"v2.1 A+P/K: building temporal, MR-STFT, envelope and input-referenced spectral-profile references...");
    MetricReference metricRef;
    metricRef.mrstft=buildMultiStftReference(target);
    metricRef.envelope=buildMultiEnvelopeReference(target);
    metricRef.responseSpectral=buildResponseSpectralReference(in,target);
    metricRef.levels=buildLevelReference(in);
    metricRef.originalLevelNmse=levelNmse(originalCandidate,target,fixedScale,metricRef.levels);
    stats.originalSpectralError=multiResolutionStftLoss(originalCandidate,fixedScale,metricRef.mrstft);
    stats.originalEnvelopeError=multiScaleEnvelopeError(originalCandidate,fixedScale,metricRef.envelope);
    stats.originalResponseSpectralError=responseSpectralError(in,originalCandidate,fixedScale,metricRef.responseSpectral);
    stats.originalLowLevelNmse=metricRef.originalLevelNmse[0];
    stats.originalMidLevelNmse=metricRef.originalLevelNmse[1];
    stats.originalHighLevelNmse=metricRef.originalLevelNmse[2];

    auto evalCandidate=[&](const std::array<double,kABandCount>& aBands,const std::array<float,4>& pk)->Eval{
        std::vector<float> candidate;
        renderAPlusPk(m,preOut,bPlan,aBands,pk[0],pk[1],pk[2],pk[3],candidate);
        const std::size_t nn=std::min(candidate.size(),target.size());
        Eval e;
        e.nmse=nmseRange(candidate,target,fixedScale,0,nn);
        e.stimulusNmse=nmseRange(candidate,target,fixedScale,0,std::min(kStimulusFrames,nn));
        e.tailNmse=nmseRange(candidate,target,fixedScale,std::min(kStimulusFrames,nn),nn);
        e.spectral=multiResolutionStftLoss(candidate,fixedScale,metricRef.mrstft);
        e.responseSpectral=responseSpectralError(in,candidate,fixedScale,metricRef.responseSpectral);
        e.envelope=multiScaleEnvelopeError(candidate,fixedScale,metricRef.envelope);
        e.levelNmse=levelNmse(candidate,target,fixedScale,metricRef.levels);
        const double nNorm=e.nmse/std::max(stats.originalNmse,kMetricEpsilon);
        const double sNorm=e.spectral/std::max(stats.originalSpectralError,kMetricEpsilon);
        const double rNorm=e.responseSpectral/std::max(stats.originalResponseSpectralError,kMetricEpsilon);
        const double dNorm=e.envelope/std::max(stats.originalEnvelopeError,kMetricEpsilon);
        double levelNorm=0.0; int levelCount=0;
        for(std::size_t i=0;i<3;++i){
            if(metricRef.originalLevelNmse[i]>kMetricEpsilon){ levelNorm+=e.levelNmse[i]/metricRef.originalLevelNmse[i]; ++levelCount; }
        }
        e.levelBalanced=levelCount?levelNorm/double(levelCount):nNorm;
        // v2.1: keep temporal fidelity primary but reserve a dedicated 20% for
        // the input-referenced log-frequency response contour. MR-STFT remains
        // useful for time/frequency texture, but can no longer stand in for the
        // transfer-function curve seen in an external spectral analyser.
        e.composite=0.35*nNorm + 0.20*e.levelBalanced + 0.20*rNorm + 0.15*sNorm + 0.10*dNorm;
        return e;
    };

    Eval originalEval;
    originalEval.nmse=stats.originalNmse; originalEval.stimulusNmse=stats.originalStimulusNmse; originalEval.tailNmse=stats.originalTailNmse;
    originalEval.spectral=stats.originalSpectralError; originalEval.responseSpectral=stats.originalResponseSpectralError; originalEval.envelope=stats.originalEnvelopeError;
    originalEval.levelNmse=metricRef.originalLevelNmse; originalEval.levelBalanced=1.0; originalEval.composite=1.0;

    // Keep two fronts: an unconstrained search path (useful for traversing the
    // coupled A/P-K space), and the best candidate that also preserves the
    // input-referenced spectral contour. _BEST and _REFINE are taken only from
    // the spectrally guarded front.
    Eval best=originalEval;
    std::array<double,kABandCount> bestABands=originalABands;
    std::array<float,4> bestPk={m.pp,m.pn,m.kp,m.kn};
    Eval safeBest=originalEval;
    std::array<double,kABandCount> safeBestABands=originalABands;
    std::array<float,4> safeBestPk={m.pp,m.pn,m.kp,m.kn};
    auto considerSpectrallySafe=[&](const Eval& e,const std::array<double,kABandCount>& bands,const std::array<float,4>& pk){
        const double limit=std::max(originalEval.responseSpectral*1.001,originalEval.responseSpectral+1.0e-8);
        if(e.responseSpectral<=limit && compositeBetter(e,safeBest)){
            safeBest=e; safeBestABands=bands; safeBestPk=pk;
        }
    };

    // Stage 1: optimize the smooth Block-A spectral envelope with P/K fixed.
    // Stage 2: alternate A and P/K so drive shaping and nonlinearity can settle
    // jointly instead of forcing P/K to compensate an incorrect A.
    double aStepDb=0.75;
    double pkStepLog=0.055; // ~5.7 % multiplicative step
    const int passes=std::clamp(config.passes,2,6);
    for(int pass=0;pass<passes;++pass){
        if(status) status(L"v2.1 A+P/K full 70 s: pass "+std::to_wstring(pass+1)+L"/"+std::to_wstring(passes)+L" (Block A)...");
        bool improvedA=false;
        for(std::size_t band=0;band<kABandCount;++band){
            Eval local=best; auto localBands=bestABands;
            for(int dir : {-1,1}){
                auto test=bestABands;
                test[band]=std::clamp(test[band]+dir*aStepDb,-4.0,4.0);
                const Eval e=evalCandidate(test,bestPk);
                considerSpectrallySafe(e,test,bestPk);
                if(compositeBetter(e,local)){ local=e; localBands=test; }
            }
            if(compositeBetter(local,best)){ best=local; bestABands=localBands; improvedA=true; }
        }

        if(status) status(L"v2.1 A+P/K full 70 s: pass "+std::to_wstring(pass+1)+L"/"+std::to_wstring(passes)+L" (P/K)...");
        bool improvedPk=false;
        for(int j=0;j<4;++j){
            Eval local=best; auto localPk=bestPk;
            for(int dir : {-1,1}){
                auto test=bestPk;
                test[j]=std::max(1e-7f,float(double(bestPk[j])*std::exp(dir*pkStepLog)));
                const Eval e=evalCandidate(bestABands,test);
                considerSpectrallySafe(e,bestABands,test);
                if(compositeBetter(e,local)){ local=e; localPk=test; }
            }
            if(compositeBetter(local,best)){ best=local; bestPk=localPk; improvedPk=true; }
        }
        aStepDb *= (improvedA?0.58:0.40);
        pkStepLog *= (improvedPk?0.60:0.42);
    }

    // Publish only the best point that preserved the dedicated response curve.
    // The unconstrained path above may temporarily move through worse spectral
    // contours, but those points are never written to _BEST/_REFINE.
    best=safeBest;
    bestABands=safeBestABands;
    bestPk=safeBestPk;

    // Final safety gate is deliberately less rigid than v1.9 because A and P/K
    // are coupled. It still rejects candidates that obtain a nicer spectrum by
    // materially degrading waveform fidelity or excitation-level behaviour.
    std::vector<std::string> failures;
    if(!(best.composite < originalEval.composite*0.9995)) failures.emplace_back("combined A+P/K loss did not improve by at least 0.05%");
    if(best.nmse > originalEval.nmse*1.0025) failures.emplace_back("global NMSE regressed by more than 0.25%");
    static constexpr const char* levelNames[3]={"low-level NMSE","mid-level NMSE","high-level NMSE"};
    for(std::size_t i=0;i<3;++i){
        if(originalEval.levelNmse[i]>kMetricEpsilon && best.levelNmse[i]>originalEval.levelNmse[i]*1.01)
            failures.emplace_back(std::string(levelNames[i])+" regressed by more than 1.0%");
    }
    if(best.spectral > originalEval.spectral*1.02) failures.emplace_back("MR-STFT regressed by more than 2%");
    if(best.responseSpectral > originalEval.responseSpectral*1.001) failures.emplace_back("input-referenced spectral profile regressed by more than 0.10%");
    if(best.envelope > originalEval.envelope*1.03) failures.emplace_back("envelope RMS regressed by more than 3%");
    const bool accepted=failures.empty();
    std::string decision=accepted?"accepted by v2.1 A+P/K spectral-response safety gate":"";
    if(!accepted){ for(std::size_t i=0;i<failures.size();++i){ if(i)decision+="; "; decision+=failures[i]; } }

    stats.searchedCandidateAccepted=accepted;
    stats.searchedComposite=best.composite;
    stats.searchedCompositeImprovementPercent=100.0*(1.0-best.composite);
    stats.searchedNmse=best.nmse;
    stats.searchedNmseImprovementPercent=stats.originalNmse>0?100.0*(stats.originalNmse-best.nmse)/stats.originalNmse:0.0;
    stats.searchedStimulusNmse=best.stimulusNmse;
    stats.searchedStimulusImprovementPercent=stats.originalStimulusNmse>0?100.0*(stats.originalStimulusNmse-best.stimulusNmse)/stats.originalStimulusNmse:0.0;
    stats.searchedTailNmse=best.tailNmse;
    stats.searchedTailImprovementPercent=stats.originalTailNmse>0?100.0*(stats.originalTailNmse-best.tailNmse)/stats.originalTailNmse:0.0;
    stats.searchedSpectralError=best.spectral;
    stats.searchedSpectralImprovementPercent=stats.originalSpectralError>0?100.0*(stats.originalSpectralError-best.spectral)/stats.originalSpectralError:0.0;
    stats.searchedEnvelopeError=best.envelope;
    stats.searchedEnvelopeImprovementPercent=stats.originalEnvelopeError>0?100.0*(stats.originalEnvelopeError-best.envelope)/stats.originalEnvelopeError:0.0;
    stats.searchedResponseSpectralError=best.responseSpectral;
    stats.searchedResponseSpectralImprovementPercent=stats.originalResponseSpectralError>0?100.0*(stats.originalResponseSpectralError-best.responseSpectral)/stats.originalResponseSpectralError:0.0;
    stats.searchedLowLevelNmse=best.levelNmse[0]; stats.searchedMidLevelNmse=best.levelNmse[1]; stats.searchedHighLevelNmse=best.levelNmse[2];
    stats.searchedLowLevelImprovementPercent=stats.originalLowLevelNmse>0?100.0*(stats.originalLowLevelNmse-best.levelNmse[0])/stats.originalLowLevelNmse:0.0;
    stats.searchedMidLevelImprovementPercent=stats.originalMidLevelNmse>0?100.0*(stats.originalMidLevelNmse-best.levelNmse[1])/stats.originalMidLevelNmse:0.0;
    stats.searchedHighLevelImprovementPercent=stats.originalHighLevelNmse>0?100.0*(stats.originalHighLevelNmse-best.levelNmse[2])/stats.originalHighLevelNmse:0.0;
    stats.searchedLevelBalancedImprovementPercent=100.0*(1.0-best.levelBalanced);
    stats.searchedPPos=bestPk[0]; stats.searchedPNeg=bestPk[1]; stats.searchedKPos=bestPk[2]; stats.searchedKNeg=bestPk[3];
    stats.searchedDecisionReason=decision;

    const auto bestA=synthesizeA(m.A,bestABands);
    const auto sa=le32(bytes.data()+0x78);
    auto writeCandidate=[&](std::vector<std::uint8_t>& d,const std::vector<float>& A,const std::array<float,4>& pk){
        for(std::size_t i=0;i<A.size();++i) putf(d.data()+kCoeffBase+4ull*(sa+i),A[i]);
        putf(d.data()+0x68,pk[0]); putf(d.data()+0x6c,pk[1]); putf(d.data()+0x70,pk[2]); putf(d.data()+0x74,pk[3]);
    };

    if(!bestClo2048.empty()){
        auto bestBytes=bytes; writeCandidate(bestBytes,bestA,bestPk);
        if(!writeFileBytes(bestClo2048,bestBytes.data(),bestBytes.size(),error)) return false;
    }

    Eval finalEval=accepted?best:originalEval;
    std::array<float,4> finalPk=accepted?bestPk:std::array<float,4>{m.pp,m.pn,m.kp,m.kn};
    stats.refinedNmse=finalEval.nmse; stats.refinedStimulusNmse=finalEval.stimulusNmse; stats.refinedTailNmse=finalEval.tailNmse;
    stats.refinedSpectralError=finalEval.spectral; stats.refinedResponseSpectralError=finalEval.responseSpectral; stats.refinedEnvelopeError=finalEval.envelope;
    stats.improved=accepted;
    stats.improvementPercent=stats.originalNmse>0?100.0*(stats.originalNmse-finalEval.nmse)/stats.originalNmse:0.0;
    stats.stimulusImprovementPercent=stats.originalStimulusNmse>0?100.0*(stats.originalStimulusNmse-finalEval.stimulusNmse)/stats.originalStimulusNmse:0.0;
    stats.tailImprovementPercent=stats.originalTailNmse>0?100.0*(stats.originalTailNmse-finalEval.tailNmse)/stats.originalTailNmse:0.0;
    stats.spectralImprovementPercent=stats.originalSpectralError>0?100.0*(stats.originalSpectralError-finalEval.spectral)/stats.originalSpectralError:0.0;
    stats.responseSpectralImprovementPercent=stats.originalResponseSpectralError>0?100.0*(stats.originalResponseSpectralError-finalEval.responseSpectral)/stats.originalResponseSpectralError:0.0;
    stats.envelopeImprovementPercent=stats.originalEnvelopeError>0?100.0*(stats.originalEnvelopeError-finalEval.envelope)/stats.originalEnvelopeError:0.0;
    stats.pPosAfter=finalPk[0]; stats.pNegAfter=finalPk[1]; stats.kPosAfter=finalPk[2]; stats.kNegAfter=finalPk[3];

    if(accepted) writeCandidate(bytes,bestA,bestPk);
    if(!writeFileBytes(outputClo2048,bytes.data(),bytes.size(),error)) return false;

    if(status){
        status(L"v2.1 A+P/K complete. Combined research loss improvement: "+std::to_wstring(stats.searchedCompositeImprovementPercent)
               +L"%; final gate: "+(accepted?L"ACCEPTED":L"REJECTED")+L".");
    }
    return true;
}


namespace {
// v2.6.0: direct SOURCE_latest_19 CAB Tone Match replication on final 20 s.
// No band guards, no confidence masking, no artificial HF freeze and no global-level removal.
// The generated 2048-sample minimum-phase IR is exported as a WAV and then applied through
// the existing applyCorrectiveIrToClo() path, exactly like a manually selected Corrective IR.
constexpr std::size_t kV26Fft=16384;
constexpr std::size_t kV26Hop=4096;
constexpr int kV26Groups=11;
constexpr double kV26SilenceDb=-55.0;
constexpr double kV26Clip=0.999;
constexpr double kV26MinHz=30.0;
constexpr double kV26MaxHz=20000.0;
constexpr double kV26CmpMinHz=40.0;
constexpr double kV26CmpMaxHz=18000.0;
constexpr std::size_t kV26Points=512;
constexpr std::size_t kV26IrLength=2048;
constexpr double kV26Smooth=0.05; // same slider setting requested by the user
constexpr double kV26MadToSigma=1.4826;
constexpr double kV26NegInf=-160.0;
constexpr double kV26ConfRefDb=3.0;

struct V26Profile{std::vector<double> f,db,conf; double coverage=0; std::size_t frames=0; bool valid()const{return f.size()>1&&db.size()==f.size()&&conf.size()==f.size();}};
struct V26Comp{std::vector<double> f,raw,conf; bool valid()const{return f.size()>1&&raw.size()==f.size()&&conf.size()==f.size();}};
static double v26clamp(double x){return std::clamp(x,0.0,1.0);} 
static double v26median(std::vector<double> v){if(v.empty())return 0; auto m=v.begin()+static_cast<std::ptrdiff_t>(v.size()/2);std::nth_element(v.begin(),m,v.end());return *m;}
static double v26mad(const std::vector<double>& v,double med){std::vector<double>d;d.reserve(v.size());for(double x:v)d.push_back(std::abs(x-med));return v26median(std::move(d));}
static double v26interp(const std::vector<double>&f,const std::vector<double>&v,double hz){if(f.empty()||f.size()!=v.size())return 0;if(hz<=f.front())return v.front();if(hz>=f.back())return v.back();auto u=std::lower_bound(f.begin(),f.end(),hz);auto i1=static_cast<std::size_t>(std::distance(f.begin(),u));auto i0=i1-1;double a=(hz-f[i0])/(f[i1]-f[i0]);return v[i0]+std::clamp(a,0.0,1.0)*(v[i1]-v[i0]);}

static V26Profile v26analyse(const std::vector<float>& s,double scale,std::size_t start,std::size_t count){
    V26Profile p;if(start>=s.size())return p;std::size_t end=std::min(s.size(),start+count);if(end-start<kV26Fft)return p;
    auto win=hannWindow(kV26Fft);std::array<std::vector<long double>,kV26Groups> sums;for(auto&g:sums)g.assign(kV26Fft/2+1,0);std::array<std::size_t,kV26Groups> counts{};std::vector<std::complex<float>> b(kV26Fft);double silence=std::pow(10.0,kV26SilenceDb/20.0);std::size_t accepted=0;
    for(std::size_t pos=start;pos+kV26Fft<=end;pos+=kV26Hop){long double ss=0;bool clip=false;double mean=0;for(std::size_t i=0;i<kV26Fft;++i){double x=scale*s[pos+i];ss+=x*x;mean+=x;if(std::abs(x)>=kV26Clip)clip=true;}double rms=std::sqrt(static_cast<double>(ss/kV26Fft));if(rms<silence||clip)continue;mean/=kV26Fft;for(std::size_t i=0;i<kV26Fft;++i)b[i]={static_cast<float>((scale*s[pos+i]-mean)*win[i]),0};fft(b,false);auto gi=accepted%kV26Groups;for(std::size_t k=0;k<=kV26Fft/2;++k){double hz=double(k)*kSampleRate/kV26Fft;if(hz<kV26MinHz||hz>kV26MaxHz)continue;double mag=std::abs(b[k]);sums[gi][k]+=mag*mag;}++counts[gi];++accepted;}
    p.frames=accepted;if(!accepted)return p;std::size_t active=0;for(auto c:counts)if(c)++active;std::vector<double> spec(kV26Fft/2+1,kV26NegInf),dev(kV26Fft/2+1,0);double strongest=kV26NegInf;
    for(std::size_t k=0;k<=kV26Fft/2;++k){double hz=double(k)*kSampleRate/kV26Fft;if(hz<kV26MinHz||hz>kV26MaxHz)continue;std::vector<double> means;for(int g=0;g<kV26Groups;++g)if(counts[g]){double mp=static_cast<double>(sums[g][k]/counts[g]);means.push_back(10*std::log10(std::max(mp,1e-20)));}double med=v26median(means);spec[k]=med;dev[k]=kV26MadToSigma*v26mad(means,med);strongest=std::max(strongest,med);}
    std::size_t incl=0,confident=0;for(std::size_t k=0;k<=kV26Fft/2;++k){double hz=double(k)*kSampleRate/kV26Fft;if(hz<kV26MinHz||hz>kV26MaxHz)continue;double frameC=v26clamp(double(accepted)/32.0),groupC=v26clamp(double(active)/kV26Groups),energyC=v26clamp((spec[k]-strongest+60.0)/60.0),stable=1.0/(1.0+dev[k]/kV26ConfRefDb),c=v26clamp(frameC*groupC*energyC*stable);p.f.push_back(hz);p.db.push_back(spec[k]);p.conf.push_back(c);++incl;if(c>=.25)++confident;}if(incl)p.coverage=double(confident)/incl;return p;
}
static V26Comp v26compare(const V26Profile&s,const V26Profile&t){V26Comp c;if(!s.valid()||!t.valid())return c;double a=std::log(kV26CmpMinHz),b=std::log(kV26CmpMaxHz);for(std::size_t i=0;i<kV26Points;++i){double q=double(i)/(kV26Points-1),hz=std::exp(a+q*(b-a));c.f.push_back(hz);c.raw.push_back(v26interp(t.f,t.db,hz)-v26interp(s.f,s.db,hz));c.conf.push_back(v26clamp(std::min(v26interp(s.f,s.conf,hz),v26interp(t.f,t.conf,hz))));}return c;}
static double v26SmoothWidth(double amount){struct P{double a,w;};static constexpr P p[]={{0,0},{.25,1.0/24},{.5,1.0/12},{.75,1.0/6},{1,1.0/3}};if(amount<=0)return 0;for(int i=1;i<5;++i)if(amount<=p[i].a){double q=(amount-p[i-1].a)/(p[i].a-p[i-1].a);return p[i-1].w+q*(p[i].w-p[i-1].w);}return p[4].w;}
static std::vector<double> v26smooth(const V26Comp&c,double amount){std::vector<double> out(c.raw.size());if(amount<=0)return c.raw;double width=v26SmoothWidth(amount),sigma=std::max(width/2.354820045,1e-6),maxd=3*sigma;for(std::size_t i=0;i<c.raw.size();++i){double sw=0,sx=0;for(std::size_t j=0;j<c.raw.size();++j){double d=std::log2(c.f[j]/c.f[i]);if(std::abs(d)>maxd)continue;double w=std::exp(-.5*d*d/(sigma*sigma));sx+=w*c.raw[j];sw+=w;}out[i]=sw>1e-12?sx/sw:c.raw[i];}return out;}
static std::vector<float> v26minPhaseIr(const V26Comp&c,double smooth){auto curve=v26smooth(c,smooth);const std::size_t N=4096;std::vector<std::complex<float>> logsp(N),cep(N),mc(N),cls(N),mps(N),imp(N);for(std::size_t k=0;k<=N/2;++k){double hz=double(k)*kSampleRate/N,db=v26interp(c.f,curve,hz),lm=db*0.11512925464970229;logsp[k]={float(lm),0};if(k>0&&k<N/2)logsp[N-k]={float(lm),0};}fft(logsp,true);cep=logsp;mc[0]=cep[0];for(std::size_t i=1;i<N/2;++i)mc[i]=cep[i]*2.0f;mc[N/2]=cep[N/2];fft(mc,false);cls=mc;for(std::size_t i=0;i<N;++i)mps[i]=std::exp(cls[i]);fft(mps,true);imp=mps;std::vector<float> ir(kV26IrLength);for(std::size_t i=0;i<ir.size();++i)ir[i]=imp[i].real();return ir;}
static bool v26writeFloatWav(const fs::path&path,const std::vector<float>&x,std::string&error){std::ofstream f(path,std::ios::binary);if(!f){error="Cannot write automatic tone-match IR WAV: "+pathToUtf8(path);return false;}auto w16=[&](std::uint16_t v){char b[2]={char(v&255),char((v>>8)&255)};f.write(b,2);};auto w32=[&](std::uint32_t v){char b[4]={char(v&255),char((v>>8)&255),char((v>>16)&255),char((v>>24)&255)};f.write(b,4);};std::uint32_t data=std::uint32_t(x.size()*4),riff=36+data;f.write("RIFF",4);w32(riff);f.write("WAVEfmt ",8);w32(16);w16(3);w16(1);w32(kSampleRate);w32(kSampleRate*4);w16(4);w16(32);f.write("data",4);w32(data);f.write(reinterpret_cast<const char*>(x.data()),data);if(!f){error="Failed writing automatic tone-match IR WAV";return false;}return true;}
static void renderWithB(const std::vector<float>& preB,const std::vector<float>& B,std::vector<float>& out){ FirFftPlan plan(B); plan.process(preB,out);}
static double v26toneError(const V26Comp&c){long double ss=0,w=0;for(std::size_t i=0;i<c.raw.size();++i){double q=std::max(c.conf[i],0.05);ss+=q*c.raw[i]*c.raw[i];w+=q;}return w>0?std::sqrt(double(ss/w)):0;}
}

bool refineCloBOnly(const fs::path& inputClo2048,const fs::path& stimulusWav,const fs::path& targetWav,const fs::path& outputClo2048,const fs::path& bestClo2048,const CloRefineConfig& config,CloRefineStats& stats,std::string& error,const RefineStatusCallback& status){
    (void)config;std::vector<std::uint8_t> bytes;if(!readFileBytes(inputClo2048,bytes,error))return false;Model m;if(!parseModel(bytes,m,error))return false;std::vector<float> in,target;if(!readMono44100(stimulusWav,in,error)||!readMono44100(targetWav,target,error))return false;std::size_t n=std::min(in.size(),target.size()),tailFrames=20u*kSampleRate;if(n<tailFrames+kV26Fft){error="Not enough audio for v2.6 final-20-s Tone Match";return false;}in.resize(n);target.resize(n);std::size_t tailStart=n-tailFrames;
    if(status)status(L"v2.6: rendering official CLO for exact VST-style final-20-s Tone Match...");auto aout=precomputeA(m,in,n);std::vector<float>preB,orig;renderPreB(m,aout,m.pp,m.pn,m.kp,m.kn,preB);renderWithB(preB,m.B,orig);double fixedScale=fitScale(orig,target);stats.outputScale=fixedScale;std::size_t split=std::min<std::size_t>(n,50u*kSampleRate);stats.originalNmse=nmseRange(orig,target,fixedScale,0,n);stats.originalStimulusNmse=nmseRange(orig,target,fixedScale,0,split);stats.originalTailNmse=nmseRange(orig,target,fixedScale,split,n);auto mr=buildMultiStftReference(target);auto env=buildMultiEnvelopeReference(target);auto levels=buildLevelReference(in);stats.originalSpectralError=multiResolutionStftLoss(orig,fixedScale,mr);stats.originalEnvelopeError=multiScaleEnvelopeError(orig,fixedScale,env);auto ol=levelNmse(orig,target,fixedScale,levels);stats.originalLowLevelNmse=ol[0];stats.originalMidLevelNmse=ol[1];stats.originalHighLevelNmse=ol[2];
    auto sp=v26analyse(orig,fixedScale,tailStart,tailFrames),tp=v26analyse(target,1.0,tailStart,tailFrames);auto cmp=v26compare(sp,tp);if(!cmp.valid()){error="v2.6 Tone Match comparison invalid";return false;}double before=v26toneError(cmp);stats.originalResponseSpectralError=before;
    auto ir=v26minPhaseIr(cmp,kV26Smooth);fs::path irPath=bestClo2048.parent_path()/L"auto_tonematch_ir.wav";if(!v26writeFloatWav(irPath,ir,error))return false;
    if(status)status(L"v2.6: applying generated auto_tonematch_ir.wav through the existing Corrective IR path...");CorrectiveIrStats cir;fs::path applied=bestClo2048.parent_path()/L"v26_applied.clo";if(!applyCorrectiveIrToClo(inputClo2048,irPath,applied,cir,error))return false;std::vector<std::uint8_t>ab;if(!readFileBytes(applied,ab,error))return false;Model am;if(!parseModel(ab,am,error))return false;std::vector<float>cand;renderWithB(preB,am.B,cand);
    auto ap=v26analyse(cand,fixedScale,tailStart,tailFrames);auto ac=v26compare(ap,tp);double after=v26toneError(ac);auto imp=[](double a,double b){return a>0?100*(a-b)/a:0.0;};stats.searchedResponseSpectralError=after;stats.searchedResponseSpectralImprovementPercent=imp(before,after);stats.searchedNmse=nmseRange(cand,target,fixedScale,0,n);stats.searchedStimulusNmse=nmseRange(cand,target,fixedScale,0,split);stats.searchedTailNmse=nmseRange(cand,target,fixedScale,split,n);stats.searchedSpectralError=multiResolutionStftLoss(cand,fixedScale,mr);stats.searchedEnvelopeError=multiScaleEnvelopeError(cand,fixedScale,env);auto bl=levelNmse(cand,target,fixedScale,levels);stats.searchedLowLevelNmse=bl[0];stats.searchedMidLevelNmse=bl[1];stats.searchedHighLevelNmse=bl[2];stats.searchedNmseImprovementPercent=imp(stats.originalNmse,stats.searchedNmse);stats.searchedStimulusImprovementPercent=imp(stats.originalStimulusNmse,stats.searchedStimulusNmse);stats.searchedTailImprovementPercent=imp(stats.originalTailNmse,stats.searchedTailNmse);stats.searchedSpectralImprovementPercent=imp(stats.originalSpectralError,stats.searchedSpectralError);stats.searchedEnvelopeImprovementPercent=imp(stats.originalEnvelopeError,stats.searchedEnvelopeError);stats.searchedLowLevelImprovementPercent=imp(stats.originalLowLevelNmse,bl[0]);stats.searchedMidLevelImprovementPercent=imp(stats.originalMidLevelNmse,bl[1]);stats.searchedHighLevelImprovementPercent=imp(stats.originalHighLevelNmse,bl[2]);stats.searchedCompositeImprovementPercent=stats.searchedResponseSpectralImprovementPercent;stats.searchedCandidateAccepted=true;stats.searchedDecisionReason="v2.6 diagnostic: exact VST-style raw final-20-s Tone Match IR exported and applied through existing Corrective IR path";
    if(!copyFileCreatingParents(applied,bestClo2048,error))return false;bool accepted=after<before;stats.searchedCandidateAccepted=accepted;if(accepted){if(!copyFileCreatingParents(applied,outputClo2048,error))return false;}else{if(!copyFileCreatingParents(inputClo2048,outputClo2048,error))return false;stats.searchedDecisionReason="v2.6 rejected: final-20-s VST Tone Match error did not improve after application through Corrective IR path";}
    stats.refinedNmse=accepted?stats.searchedNmse:stats.originalNmse;stats.refinedStimulusNmse=accepted?stats.searchedStimulusNmse:stats.originalStimulusNmse;stats.refinedTailNmse=accepted?stats.searchedTailNmse:stats.originalTailNmse;stats.refinedSpectralError=accepted?stats.searchedSpectralError:stats.originalSpectralError;stats.refinedEnvelopeError=accepted?stats.searchedEnvelopeError:stats.originalEnvelopeError;stats.refinedResponseSpectralError=accepted?after:before;stats.improved=accepted;stats.improvementPercent=imp(stats.originalNmse,stats.refinedNmse);stats.stimulusImprovementPercent=imp(stats.originalStimulusNmse,stats.refinedStimulusNmse);stats.tailImprovementPercent=imp(stats.originalTailNmse,stats.refinedTailNmse);stats.spectralImprovementPercent=imp(stats.originalSpectralError,stats.refinedSpectralError);stats.envelopeImprovementPercent=imp(stats.originalEnvelopeError,stats.refinedEnvelopeError);stats.responseSpectralImprovementPercent=imp(before,stats.refinedResponseSpectralError);
    if(status)status(L"v2.6 exact VST-style Tone Match replication complete; auto_tonematch_ir.wav generated and passed through the existing Corrective IR pipeline.");return true;
}

} // namespace ntc
