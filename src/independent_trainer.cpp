#include "independent_trainer.hpp"
#include "common.hpp"
#include "stimulus.hpp"

#include <NAM/get_dsp.h>
#include <CDSPResampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <vector>

namespace ntc {
namespace {

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kEps = 1.1920928955078125e-7;
constexpr std::uint32_t kCloRate = 44100;
constexpr std::size_t kA = 128;
constexpr std::size_t kB = 2048;
constexpr std::size_t kFft = 2048;
constexpr std::size_t kBins = kFft / 2 + 1;
constexpr std::size_t kCloBytes = 0x2288;

void report(const StatusCallback& cb, const std::wstring& s) { if (cb) cb(s); }

std::uint16_t le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
void put32(std::vector<std::uint8_t>& d, std::size_t o, std::uint32_t v) {
    d[o] = static_cast<std::uint8_t>(v); d[o+1] = static_cast<std::uint8_t>(v >> 8);
    d[o+2] = static_cast<std::uint8_t>(v >> 16); d[o+3] = static_cast<std::uint8_t>(v >> 24);
}
void putFloat(std::vector<std::uint8_t>& d, std::size_t o, float v) {
    std::uint32_t u{}; std::memcpy(&u, &v, 4); put32(d,o,u);
}
void putDouble(std::vector<std::uint8_t>& d, std::size_t o, double v) {
    std::uint64_t u{}; std::memcpy(&u, &v, 8);
    for (int i=0;i<8;++i) d[o+i] = static_cast<std::uint8_t>(u >> (8*i));
}
std::uint16_t crc16Modbus(const std::uint8_t* p, std::size_t n) {
    std::uint16_t crc=0xffff;
    for(std::size_t i=0;i<n;++i){ crc^=p[i]; for(int b=0;b<8;++b) crc=(crc&1)?static_cast<std::uint16_t>((crc>>1)^0xa001):static_cast<std::uint16_t>(crc>>1); }
    return crc;
}

bool readPcm16Mono(const fs::path& path, std::vector<float>& x, std::uint32_t& sr, std::string& error) {
    std::ifstream f(path, std::ios::binary); if(!f){error="Cannot open stimulus WAV: "+pathToUtf8(path);return false;}
    std::array<std::uint8_t,12> h{}; f.read(reinterpret_cast<char*>(h.data()),12);
    if(f.gcount()!=12 || std::memcmp(h.data(),"RIFF",4)||std::memcmp(h.data()+8,"WAVE",4)){error="Invalid stimulus WAV.";return false;}
    std::uint16_t fmt=0,ch=0,bits=0,align=0; std::vector<std::uint8_t> data;
    while(f){std::array<std::uint8_t,8> c{};f.read(reinterpret_cast<char*>(c.data()),8);if(f.gcount()!=8)break;const auto n=le32(c.data()+4);std::vector<std::uint8_t>b(n);if(n){f.read(reinterpret_cast<char*>(b.data()),n);if(static_cast<std::uint32_t>(f.gcount())!=n){error="Truncated stimulus WAV.";return false;}}if(n&1)f.seekg(1,std::ios::cur);
        if(!std::memcmp(c.data(),"fmt ",4)&&n>=16){fmt=le16(b.data());ch=le16(b.data()+2);sr=le32(b.data()+4);align=le16(b.data()+12);bits=le16(b.data()+14);}
        else if(!std::memcmp(c.data(),"data",4))data=std::move(b);
    }
    if(fmt!=1||ch!=1||bits!=16||align!=2||sr==0||data.empty()){error="Independent trainer expects the StimulusBuilder mono PCM16 WAV.";return false;}
    x.resize(data.size()/2); for(std::size_t i=0;i<x.size();++i)x[i]=static_cast<std::int16_t>(le16(data.data()+2*i))/32768.0f;
    return true;
}

// The official converter instantiates r8b::CDSPResampler24.  Use the same
// r8brain class in-process (header-only/static source dependency), so the
// native path has no Valeton/Hotone runtime DLL dependency.
std::vector<float> resampleR8Brain24(const std::vector<float>& in, double inRate, double outRate) {
    if (in.empty() || std::abs(inRate - outRate) < 1e-9) return in;
    const std::size_t outN = static_cast<std::size_t>(
        std::llround(static_cast<double>(in.size()) * outRate / inRate));
    std::vector<float> out(outN, 0.0f);
    const int maxIn = static_cast<int>(std::min<std::size_t>(
        in.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    r8b::CDSPResampler24 rs(inRate, outRate, std::max(1, maxIn));

    // Some r8brain revisions expose oneshot() through an implementation path
    // whose input helper requires a non-const float pointer.  `in` is const here,
    // so passing in.data() directly fails to compile on MSVC with
    // "Conversion loses qualifiers".  Keep a mutable working copy.  r8brain
    // still performs its internal processing in double precision.
    std::vector<float> inputWork(in.begin(), in.end());
    rs.oneshot(inputWork.data(), static_cast<int>(inputWork.size()),
               out.data(), static_cast<int>(out.size()));
    return out;
}

// Exact 50-float tables reconstructed from GP-200.exe.  The branch tests in
// the official converter are against 44100.0f, 48000.0f and 96000.0f and the
// selected table is passed to the 50-tap conditioning FIR used by the initial
// 23-28 s identification stage.  These tables are trainer-only; they are not
// serialized into Block A or Block B.
constexpr std::array<float,50> kInitialFir44100 = {
 2.368210077f, 3.574280024f, 3.095710039f, -0.392399013f, -3.150949955f,
 -3.830709934f, -2.215640068f, -0.8829950094f, -0.212944001f, -0.1931400001f,
 0.2239619941f, 0.4040279984f, 0.3783220053f, 0.08940640092f, 0.1935559958f,
 0.283547014f, 0.3439449966f, 0.05293060094f, -0.03797249869f, -0.06761389971f,
 0.07335829735f, -0.02648900077f, -0.02291630022f, 0.02360440046f, 0.08525899798f,
 -0.009279790334f, 0.04381980002f, 0.0233258009f, 0.1348949969f, 0.006286839955f,
 -0.02591219917f, -0.01955270022f, 0.08531299978f, -0.01847779937f, -0.07664210349f,
 -0.0785638988f, 0.03350910172f, 0.02729599923f, -0.09931690246f, -0.09534750134f,
 0.05328249931f, 0.03427400067f, -0.09364210069f, -0.06697729975f, -0.01135309972f,
 0.03991980106f, -0.0771979019f, -0.09798060358f, 0.002032200107f, 0.04304929823f
};
constexpr std::array<float,50> kInitialFir48000 = {
 2.369808912f, 3.532199383f, 3.386006355f, 0.5711528063f, -2.494512081f,
 -3.850381851f, -3.163845062f, -1.511753678f, -0.5816931129f, -0.1761655957f,
 -0.1537622064f, 0.2662706077f, 0.4051620066f, 0.3895485103f, 0.1137828976f,
 0.1536727995f, 0.2604695857f, 0.3561989963f, 0.202282995f, -0.01788109913f,
 -0.05925950035f, -0.03052599914f, 0.07328040153f, -0.0387939997f, -0.0205725003f,
 0.02094990015f, 0.08917230368f, -0.0006698999787f, 0.03298040107f, 0.01907679997f,
 0.095199503f, 0.09397300333f, -0.02691840008f, -0.02818600088f, 0.003533599898f,
 0.08518820256f, -0.02848079987f, -0.07661850005f, -0.08217039704f, 0.0129757002f,
 0.05085289851f, -0.06227429956f, -0.1223426983f, -0.02208109945f, 0.08036129922f,
 -0.02014300041f, -0.09813290089f, -0.05815440044f, -0.003250899957f, 0.03926600143f
};
constexpr std::array<float,50> kInitialFir96000 = {
 2.369808912f, 3.144882202f, 3.532199383f, 3.684798717f, 3.386006594f,
 2.297138929f, 0.5711528063f, -1.175742626f, -2.494512081f, -3.369890928f,
 -3.85038209f, -3.795108557f, -3.163845062f, -2.270354748f, -1.511753798f,
 -0.987195015f, -0.5816931725f, -0.2824920118f, -0.1761655957f, -0.2019508034f,
 -0.1537622064f, 0.04795689881f, 0.2662706077f, 0.3740029931f, 0.4051620066f,
 0.4213505089f, 0.3895485103f, 0.2569816113f, 0.1137828976f, 0.08147999644f,
 0.1536727995f, 0.2281782031f, 0.2604695857f, 0.3036418855f, 0.3561989963f,
 0.3322631121f, 0.202282995f, 0.05376290157f, -0.01788109913f, -0.0349936001f,
 -0.05925950035f, -0.07576920092f, -0.03052599914f, 0.04923079908f, 0.07328040153f,
 0.01946049929f, -0.0387939997f, -0.04402400181f, -0.0205725003f, -0.004320300184f
};

const std::array<float,50>& initialFirForRate(double sr) {
    if (std::abs(sr - 44100.0) < 1.0) return kInitialFir44100;
    if (std::abs(sr - 48000.0) < 1.0) return kInitialFir48000;
    if (std::abs(sr - 96000.0) < 1.0) return kInitialFir96000;
    // Official tables exist only for the three reconstructed paths.  The NAM
    // path normally resolves to one of them; 48 kHz is the official fallback.
    return kInitialFir48000;
}

std::vector<float> applyInitialConditioningFir(const std::vector<float>& in, double sr) {
    const auto& h = initialFirForRate(sr);
    std::vector<float> out(in.size(), 0.0f);
    for (std::size_t n = 0; n < in.size(); ++n) {
        long double acc = 0.0;
        const std::size_t kmax = std::min<std::size_t>(h.size() - 1, n);
        for (std::size_t k = 0; k <= kmax; ++k)
            acc += static_cast<long double>(h[k]) * in[n - k];
        out[n] = static_cast<float>(acc);
    }
    return out;
}

std::optional<std::size_t> objectEnd(const std::string& s,std::size_t start){
    if(start>=s.size()||s[start]!='{')return std::nullopt;int depth=0;bool str=false,esc=false;
    for(std::size_t i=start;i<s.size();++i){char c=s[i];if(str){if(esc)esc=false;else if(c=='\\')esc=true;else if(c=='"')str=false;continue;}if(c=='"'){str=true;continue;}if(c=='{')++depth;else if(c=='}'&&--depth==0)return i+1;}return std::nullopt;
}
bool prepareFullA2(const fs::path& namPath,const fs::path& work,fs::path& result,std::string& error){
    result=namPath; std::ifstream f(namPath,std::ios::binary);if(!f)return true;std::string s((std::istreambuf_iterator<char>(f)),{});
    if(s.find("\"SlimmableContainer\"")==std::string::npos)return true;
    const auto sk=s.find("\"submodels\""); if(sk==std::string::npos)return true; const auto ao=s.find('[',sk);if(ao==std::string::npos)return true;
    double best=-std::numeric_limits<double>::infinity();std::string bestModel;std::size_t p=ao+1;
    while(p<s.size()){while(p<s.size()&&(std::isspace(static_cast<unsigned char>(s[p]))||s[p]==','))++p;if(p>=s.size()||s[p]==']')break;if(s[p]!='{')break;auto e=objectEnd(s,p);if(!e)break;
        const auto mk=s.find("\"max_value\"",p),mod=s.find("\"model\"",p); if(mk<*e&&mod<*e){const auto col=s.find(':',mk);if(col==std::string::npos||col>=*e){p=*e;continue;}char* ep=nullptr;const double v=std::strtod(s.c_str()+col+1,&ep);const auto mc=s.find(':',mod);if(mc==std::string::npos||mc>=*e){p=*e;continue;}const auto mo=s.find('{',mc);auto me=mo==std::string::npos?std::nullopt:objectEnd(s,mo);if(me&&*me<=*e&&v>best){best=v;bestModel=s.substr(mo,*me-mo);}}
        p=*e;
    }
    if(bestModel.empty()){error="Could not extract the Full submodel from A2 SlimmableContainer.";return false;}
    result=work/L"independent_a2_full.nam";std::ofstream o(result,std::ios::binary|std::ios::trunc);if(!o){error="Cannot create temporary A2 Full NAM.";return false;}o.write(bestModel.data(),static_cast<std::streamsize>(bestModel.size()));return o.good();
}

bool renderNam(const fs::path& path,const std::vector<float>& stimulus44100,int blockSize,double targetScale,
               std::vector<float>& input,std::vector<float>& target,double& rate,std::string& error,const StatusCallback& status){
    try{
        auto dsp=nam::get_dsp(path); if(!dsp){error="NeuralAmpModelerCore could not load the NAM.";return false;}
        rate=dsp->GetExpectedSampleRate();if(!(rate>1000.0&&rate<384000.0))rate=48000.0;
        report(status,L"Independent: rendering NAM at "+std::to_wstring(static_cast<int>(std::llround(rate)))+L" Hz...");
        input=resampleR8Brain24(stimulus44100,44100.0,rate);
        target.assign(input.size(),0.0f);
        dsp->Reset(rate,blockSize);
        std::vector<NAM_SAMPLE> ib(static_cast<std::size_t>(blockSize)),ob(static_cast<std::size_t>(blockSize));
        NAM_SAMPLE* ip[1]={ib.data()}; NAM_SAMPLE* op[1]={ob.data()};
        for(std::size_t pos=0;pos<input.size();pos+=static_cast<std::size_t>(blockSize)){
            const int n=static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize),input.size()-pos));
            for(int i=0;i<n;++i)ib[static_cast<std::size_t>(i)]=static_cast<NAM_SAMPLE>(input[pos+static_cast<std::size_t>(i)]);
            dsp->process(ip,op,n);
            for(int i=0;i<n;++i)target[pos+static_cast<std::size_t>(i)]=static_cast<float>(static_cast<double>(ob[static_cast<std::size_t>(i)])*targetScale);
        }
        return true;
    }catch(const std::exception& e){error=std::string("NAM renderer: ")+e.what();return false;}
}

void detrend(std::vector<float>& y){
    if(y.empty())return; const long double n=static_cast<long double>(y.size());long double sx=0,sy=0,sxx=0,sxy=0;
    for(std::size_t i=0;i<y.size();++i){const long double x=static_cast<long double>(i);sx+=x;sy+=y[i];sxx+=x*x;sxy+=x*y[i];}
    const long double den=n*sxx-sx*sx;const long double a=std::abs(static_cast<double>(den))>1e-30? (n*sxy-sx*sy)/den:0;const long double b=(sy-a*sx)/n;
    for(std::size_t i=0;i<y.size();++i)y[i]=static_cast<float>(static_cast<long double>(y[i])-(a*static_cast<long double>(i)+b));
}
std::size_t detectLatency(const std::vector<float>& y,double sr){const std::size_t base=static_cast<std::size_t>(std::llround(6.0*sr));const std::size_t lim=std::min(y.size(),base+600);for(std::size_t i=base;i<lim;++i)if(std::abs(y[i])>0.01f)return i-base;return 0;}
std::vector<float> alignLeft(const std::vector<float>& x,std::size_t n){std::vector<float> y(x.size(),0);if(n>=x.size())return y;std::copy(x.begin()+static_cast<std::ptrdiff_t>(n),x.end(),y.begin());return y;}

struct PK {float pp=.1f,pn=.1f,kp=1,kn=1;};

// P is fixed by the extrema of the 0-5 s level ramp.  K is then fitted
// independently for the positive/negative branches against the complete
// 100 ms measurement set.  NATIVE4 used a small-signal linear shortcut for
// the seed and only searched +/-20%; that shortcut is not part of the
// reconstructed objective and could pin K at 1.2 for high-gain models.
// Here we solve the actual exponential SSE directly while preserving the
// reconstructed fixed-P / independent-branch structure.
PK fitPk(const std::vector<float>& in,const std::vector<float>& out,double sr){
    const std::size_t win=static_cast<std::size_t>(std::llround(0.1*sr));
    const std::size_t end=std::min(in.size(),static_cast<std::size_t>(std::llround(5.0*sr)));
    struct M{double x,yp,yn;}; std::vector<M> m;
    for(std::size_t p=0;p+win<=end;p+=win){
        double x=0,yp=-1e30,yn=1e30;
        for(std::size_t i=p;i<p+win;++i){
            x=std::max(x,std::abs(static_cast<double>(in[i])));
            yp=std::max(yp,static_cast<double>(out[i]));
            yn=std::min(yn,static_cast<double>(out[i]));
        }
        m.push_back({x,std::max(0.0,yp),std::max(0.0,-yn)});
    }
    PK r; double pp=0,pn=0;
    for(const auto&v:m){pp=std::max(pp,v.yp);pn=std::max(pn,v.yn);}
    pp=std::max(pp,1e-9); pn=std::max(pn,1e-9);
    r.pp=static_cast<float>(pp); r.pn=static_cast<float>(pn);

    auto solveK=[&](bool pos,double P){
        auto err=[&](double K){
            long double e=0.0L;
            for(const auto&v:m){
                if(v.x<=0.0) continue;
                const double y=pos?v.yp:v.yn;
                const double pred=P*(1.0-std::exp(-K*v.x));
                const double d=pred-y; e+=static_cast<long double>(d)*d;
            }
            return static_cast<double>(e);
        };
        // Log-domain bracket covers clean through very high-gain NAMs while
        // remaining deterministic.  Follow with golden-section refinement.
        double bestK=1.0,bestE=std::numeric_limits<double>::infinity();
        for(int i=0;i<=320;++i){
            const double t=static_cast<double>(i)/320.0;
            const double K=std::exp(std::log(1.0e-3)+(std::log(1.0e5)-std::log(1.0e-3))*t);
            const double e=err(K); if(e<bestE){bestE=e;bestK=K;}
        }
        double lo=std::max(1.0e-6,bestK/1.35), hi=bestK*1.35;
        constexpr double gr=0.6180339887498948482;
        double c=hi-gr*(hi-lo), d=lo+gr*(hi-lo), ec=err(c), ed=err(d);
        for(int it=0;it<64;++it){
            if(ec<ed){hi=d;d=c;ed=ec;c=hi-gr*(hi-lo);ec=err(c);}else{lo=c;c=d;ec=ed;d=lo+gr*(hi-lo);ed=err(d);}
        }
        return 0.5*(lo+hi);
    };
    r.kp=static_cast<float>(solveK(true,pp));
    r.kn=static_cast<float>(solveK(false,pn));
    return r;
}

struct Biquad{double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;float p(float x){double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return static_cast<float>(y);}};
Biquad postForRate(double fs){constexpr double c=177.7158051,w2=15791.45215;const double f2=fs*fs,D=f2+c*fs+w2;Biquad q;q.b0=f2/D;q.b1=-2*q.b0;q.b2=q.b0;q.a1=-(2*f2-2*w2)/D;q.a2=(f2-c*fs+w2)/D;return q;}
struct AP{float a=0,s=0;float p(float x){const float y=s+a*x;s=x-a*y;return y;}};
struct Poly{std::vector<AP>a,b;float d=0;Poly(std::initializer_list<float>x,std::initializer_list<float>y){for(float v:x)a.push_back({v,0});for(float v:y)b.push_back({v,0});}float r(std::vector<AP>&v,float x){for(auto&s:v)x=s.p(x);return x;}void up(float x,float&e,float&o){e=r(a,x);o=r(b,x);}float down(float e,float o){const float x=r(a,e),y=r(b,o),z=.5f*(x+d);d=y;return z;}};

void fft(std::vector<std::complex<double>>& a,bool inv){const std::size_t n=a.size();for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}for(std::size_t len=2;len<=n;len<<=1){const double ang=(inv?2:-2)*kPi/static_cast<double>(len);const std::complex<double> wl(std::cos(ang),std::sin(ang));for(std::size_t i=0;i<n;i+=len){std::complex<double>w(1,0);for(std::size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wl;}}}if(inv)for(auto&v:a)v/=static_cast<double>(n);}

struct FirPlan{std::size_t n=0,hop=0,len=0;std::vector<std::complex<double>>H;FirPlan()=default;explicit FirPlan(const std::vector<float>&h){len=h.size();n=1;while(n<std::max<std::size_t>(8192,2*len))n<<=1;hop=n-len+1;H.assign(n,{});for(std::size_t i=0;i<len;++i)H[i]=h[i];fft(H,false);}void run(const std::vector<float>&x,std::vector<float>&y)const{if(len==0){y=x;return;}y.assign(x.size(),0);std::vector<std::complex<double>>b(n);const std::size_t ov=len-1;for(std::size_t pos=0;pos<x.size();pos+=hop){std::fill(b.begin(),b.end(),std::complex<double>{});for(std::size_t j=0;j<ov;++j)if(pos+j>=ov)b[j]=x[pos+j-ov];const auto c=std::min(hop,x.size()-pos);for(std::size_t j=0;j<c;++j)b[ov+j]=x[pos+j];fft(b,false);for(std::size_t k=0;k<n;++k)b[k]*=H[k];fft(b,true);for(std::size_t j=0;j<c;++j)y[pos+j]=static_cast<float>(b[ov+j].real());}}};

struct Model{std::vector<float>A,B;PK pk;Biquad post;};
void renderModel(const Model& m,const std::vector<float>& in,std::vector<float>& out,bool includeB=true){
    FirPlan ap(m.A);std::vector<float>a;ap.run(in,a);Biquad post=m.post;
    Poly u1({.045728147029876709f,.3325011134147644f,.66320204734802246f,.93385583162307739f},{.16808754205703735f,.50448572635650635f,.80378085374832153f});
    Poly u2({.054230779409408569f,.39879697561264038f,.86291784048080444f},{.19969958066940308f,.62109684944152832f});
    Poly d1({.070765949785709381f,.51316756010055542f},{.25785309076309204f,.81731736660003662f});
    Poly d2({.054217524826526642f,.38308733701705933f,.74872094392776489f},{.19679796695709229f,.57313638925552368f,.91429370641708374f});
    std::vector<float>pre(a.size());auto shape=[&](float x){return x>0?m.pk.pp*(1-std::exp(-m.pk.kp*x)):m.pk.pn*(std::exp(m.pk.kn*x)-1);};
    for(std::size_t i=0;i<a.size();++i){float e,o,q0,q1;u1.up(a[i],e,o);u2.up(e,q0,q1);q0=shape(q0);q1=shape(q1);const float z0=d1.down(q0,q1);u2.up(o,q0,q1);q0=shape(q0);q1=shape(q1);const float z1=d1.down(q0,q1);pre[i]=post.p(d2.down(z0,z1));}
    if(includeB){FirPlan bp(m.B);bp.run(pre,out);}else out=std::move(pre);
}

std::vector<double> hamming(std::size_t n){
    std::vector<double>w(n,1.0); if(n<=1)return w;
    for(std::size_t i=0;i<n;++i)w[i]=.54-.46*std::cos(2*kPi*static_cast<double>(i)/static_cast<double>(n-1));
    return w;
}

// Reconstructed spectral estimator: ~125 ms Hamming windows, 50% overlap,
// each long window folded modulo 2048 before the 2048-point FFT.  The official
// loss uses only the magnitude of the accumulated cross-spectrum.
std::vector<double> ratioSpectrum(const std::vector<float>& model,const std::vector<float>& target,
                                  std::size_t begin,std::size_t end,double sr){
    const std::size_t L=std::max<std::size_t>(kFft,static_cast<std::size_t>(std::ceil(0.125*sr)));
    const std::size_t hop=std::max<std::size_t>(1,L/2);
    const auto w=hamming(L);
    std::vector<long double>sxx(kBins,0.0L);
    std::vector<std::complex<long double>>sxy(kBins,{0.0L,0.0L});
    begin=std::min(begin,std::min(model.size(),target.size()));
    end=std::min(end,std::min(model.size(),target.size()));
    if(end<=begin+L)return std::vector<double>(kBins,1.0);
    std::size_t frames=0;
    for(std::size_t p=begin;p+L<=end;p+=hop){
        long double mx=0,my=0;
        for(std::size_t i=0;i<L;++i){mx+=model[p+i];my+=target[p+i];}
        mx/=static_cast<long double>(L); my/=static_cast<long double>(L);
        std::vector<std::complex<double>>X(kFft),Y(kFft);
        for(std::size_t i=0;i<L;++i){
            const std::size_t j=i&(kFft-1); // kFft=2048
            X[j]+=static_cast<double>((static_cast<long double>(model[p+i])-mx)*w[i]);
            Y[j]+=static_cast<double>((static_cast<long double>(target[p+i])-my)*w[i]);
        }
        fft(X,false); fft(Y,false); ++frames;
        for(std::size_t k=0;k<kBins;++k){
            sxx[k]+=static_cast<long double>(std::norm(X[k]));
            const std::complex<long double> xl(X[k].real(),X[k].imag());
            const std::complex<long double> yl(Y[k].real(),Y[k].imag());
            sxy[k]+=std::conj(xl)*yl;
        }
    }
    if(frames==0)return std::vector<double>(kBins,1.0);
    std::vector<double>r(kBins,1.0);
    for(std::size_t k=0;k<kBins;++k)r[k]=static_cast<double>(std::abs(sxy[k])/(sxx[k]+kEps));
    return r;
}

double hzToMel(double hz){return 2595.0*std::log10(1.0+std::max(0.0,hz)/700.0);}
double melToHz(double mel){return 700.0*(std::pow(10.0,mel/2595.0)-1.0);}

double interpLinear(const std::vector<double>&x,const std::vector<double>&y,double q){
    if(x.empty())return 0.0; if(q<=x.front())return y.front(); if(q>=x.back())return y.back();
    const auto it=std::lower_bound(x.begin(),x.end(),q); const std::size_t b=static_cast<std::size_t>(it-x.begin()),a=b-1;
    const double f=(q-x[a])/(x[b]-x[a]); return y[a]+(y[b]-y[a])*f;
}

std::vector<double> gaussianKernel(std::size_t n){
    n=std::max<std::size_t>(1,n); if((n&1u)==0u)++n;
    std::vector<double>g(n); const double c=std::ceil(static_cast<double>(n)/2.0);
    long double sum=0;
    for(std::size_t i=0;i<n;++i){const double x=(static_cast<double>(i+1)-c)*5.0/static_cast<double>(n);g[i]=std::exp(-0.5*x*x);sum+=g[i];}
    for(auto&v:g)v/=static_cast<double>(sum); return g;
}
std::vector<double> gaussianSmooth(const std::vector<double>&v,std::size_t n){
    if(v.empty())return {}; const auto g=gaussianKernel(n); const int r=static_cast<int>(g.size()/2); std::vector<double>o(v.size());
    for(std::size_t i=0;i<v.size();++i){long double s=0,w=0;for(int q=-r;q<=r;++q){const long j=static_cast<long>(i)+q;if(j<0||j>=static_cast<long>(v.size()))continue;const double ww=g[static_cast<std::size_t>(q+r)];s+=v[static_cast<std::size_t>(j)]*ww;w+=ww;}o[i]=w>0?static_cast<double>(s/w):v[i];}
    return o;
}

// Reconstructed magnitude conditioning: dB interpolation/smoothing in a
// uniformly sampled Mel domain, then interpolation back to the linear-Hz FFT
// grid.  Two Gaussian passes match the observed converter flow.
std::vector<double> melSmoothMagnitude(const std::vector<double>&mag,double sr,std::size_t kernel1,std::size_t kernel2){
    if(mag.empty())return {};
    std::vector<double>hz(mag.size()),mel(mag.size()),db(mag.size());
    for(std::size_t k=0;k<mag.size();++k){hz[k]=static_cast<double>(k)*sr/static_cast<double>(kFft);mel[k]=hzToMel(hz[k]);db[k]=20.0*std::log10(std::max(1e-20,mag[k]));}
    const double melMax=hzToMel(sr*0.5); std::vector<double>um(mag.size()),udb(mag.size());
    for(std::size_t i=0;i<mag.size();++i){um[i]=melMax*static_cast<double>(i)/static_cast<double>(mag.size()-1);udb[i]=interpLinear(mel,db,um[i]);}
    udb=gaussianSmooth(udb,kernel1); udb=gaussianSmooth(udb,kernel2);
    std::vector<double>out(mag.size());
    for(std::size_t k=0;k<mag.size();++k){const double d=interpLinear(um,udb,mel[k]);out[k]=std::pow(10.0,d/20.0);}
    return out;
}

void lowSmoothA(std::vector<double>&m,double sr){
    if(m.size()<2)return;
    const std::size_t lim=std::min<std::size_t>(m.size()-1,static_cast<std::size_t>(std::ceil(60.0*kFft/sr)));
    const auto old=m;
    for(std::size_t i=0;i<=lim;++i){
        if(i==0)m[i]=std::sqrt(std::max(1e-20,old[0]*std::sqrt(std::max(1e-20,old[0]*old[1]))));
        else if(i+1<m.size())m[i]=std::sqrt(std::max(1e-20,old[i]*std::sqrt(std::max(1e-20,old[i-1]*old[i+1]))));
    }
}

void regularizeInitialCurve(std::vector<double>&v){
    if(v.empty())return; const double mx=*std::max_element(v.begin(),v.end()); const double f=std::max(1e-20,0.001*mx);
    for(auto&x:v)if(x<2.0*f)x=f+(x*x)/(4.0*f);
}

std::vector<float> minimumPhase(const std::vector<double>& positive,std::size_t taps){
    std::vector<double>m(kFft,1);for(std::size_t k=0;k<kBins&&k<positive.size();++k)m[k]=std::max(1e-20,positive[k]);for(std::size_t k=1;k<kFft/2;++k)m[kFft-k]=m[k];double mx=*std::max_element(m.begin(),m.end()),floor=mx*1e-5;std::vector<std::complex<double>>c(kFft);for(std::size_t i=0;i<kFft;++i)c[i]=std::log(std::max(floor,m[i])+kEps);fft(c,true);for(std::size_t i=1;i<kFft/2;++i)c[i]*=2;for(std::size_t i=kFft/2+1;i<kFft;++i)c[i]=0;fft(c,false);for(auto&v:c)v=std::exp(v);fft(c,true);long double full=0;for(auto&v:c)full+=v.real()*v.real();std::vector<float>h(taps,0);for(std::size_t i=0;i<std::min(taps,c.size());++i)h[i]=static_cast<float>(c[i].real());const double mean=std::accumulate(h.begin(),h.end(),0.0)/static_cast<double>(h.size());for(auto&v:h)v=static_cast<float>(v-mean);long double sh=0;for(float v:h)sh+=static_cast<long double>(v)*v;if(sh>1e-30&&full>0){const double g=std::sqrt(static_cast<double>(full/sh));for(auto&v:h)v=static_cast<float>(v*g);}return h;
}
std::vector<double> spectrumOfFir(const std::vector<float>&h){std::vector<std::complex<double>>x(kFft);for(std::size_t i=0;i<std::min(h.size(),x.size());++i)x[i]=h[i];fft(x,false);std::vector<double>m(kBins);for(std::size_t k=0;k<kBins;++k)m[k]=std::max(1e-12,std::abs(x[k]));return m;}
double lossFromRatio(const std::vector<double>&r){long double s=0;for(std::size_t i=0;i<512;++i){const double pos=static_cast<double>(i)*(r.size()-1)/511.0;const auto a=static_cast<std::size_t>(pos),b=std::min(a+1,r.size()-1);const double f=pos-a,v=r[a]+(r[b]-r[a])*f;s+=std::abs(std::log(v+kEps));}return static_cast<double>(s/512.0);}

void fitAB(Model& m,const std::vector<float>&input,const std::vector<float>&target,double sr,const StatusCallback&status){
    report(status,L"Independent: official 50-tap initial conditioning...");
    const auto conditionedInput=applyInitialConditioningFir(input,sr);
    std::vector<float> conditionedPrediction; renderModel(m,conditionedInput,conditionedPrediction,true);
    const std::size_t initB=static_cast<std::size_t>(std::llround(23.0*sr));
    const std::size_t initE=static_cast<std::size_t>(std::llround(28.0*sr));

    auto initial=ratioSpectrum(conditionedPrediction,target,initB,initE,sr);
    regularizeInitialCurve(initial);
    initial=melSmoothMagnitude(initial,sr,11,11);

    // Spectral factor states.  The optimizer redistributes a multiplicative
    // factor around the fixed P/K nonlinearity rather than solving A and B as
    // unrelated EQs.
    std::vector<double>aState(kBins,1.0),bState(kBins,1.0);
    for(std::size_t k=0;k<kBins;++k){
        const double c=std::clamp(initial[k],0.2,5.0);
        aState[k]/=c; bState[k]*=c;
    }
    lowSmoothA(aState,sr);
    m.A=minimumPhase(melSmoothMagnitude(aState,sr,7,7),kA);
    m.B=minimumPhase(melSmoothMagnitude(bState,sr,11,11),kB);
    bState=spectrumOfFir(m.B);

    double step=1.0,best=100.0; Model bestM=m; auto bestA=aState,bestB=bState;
    struct Phase{double t0,t1;int n;const wchar_t*name;};
    const Phase phases[]={{23,28,3,L"sweep"},{6,21,2,L"low-level"},{30,50,5,L"multi-level"}};
    int iter=0;
    for(const auto&ph:phases){
        for(int q=0;q<ph.n;++q){
            ++iter; report(status,L"Independent: A/B fit "+std::to_wstring(iter)+L"/10 ("+ph.name+L")...");
            const std::size_t b=static_cast<std::size_t>(std::llround(ph.t0*sr));
            const std::size_t e=static_cast<std::size_t>(std::llround(ph.t1*sr));
            std::vector<float> pred; renderModel(m,input,pred,true);
            auto corr=melSmoothMagnitude(ratioSpectrum(pred,target,b,e,sr),sr,11,11);
            for(auto&v:corr)v=std::clamp(std::pow(std::max(1e-20,v),step),0.2,5.0);

            auto trialA=aState,trialB=bState;
            for(std::size_t k=0;k<kBins;++k){trialA[k]/=corr[k];trialB[k]*=corr[k];}
            lowSmoothA(trialA,sr);

            Model candidate=m;
            candidate.A=minimumPhase(melSmoothMagnitude(trialA,sr,7,7),kA);

            // Measure the fresh post-nonlinearity residual with B bypassed,
            // divide by the accumulated B spectral state, then reconstruct B.
            std::vector<float> pre; renderModel(candidate,input,pre,false);
            auto fresh=melSmoothMagnitude(ratioSpectrum(pre,target,b,e,sr),sr,11,11);
            std::vector<double> rb(kBins,1.0);
            for(std::size_t k=0;k<kBins;++k){
                rb[k]=(1.0e6*fresh[k])/(1.0e6*std::max(1e-20,trialB[k])+kEps);
                rb[k]=std::clamp(rb[k],0.2,5.0);
            }
            rb=melSmoothMagnitude(rb,sr,11,11);
            candidate.B=minimumPhase(rb,kB);

            std::vector<float> final; renderModel(candidate,input,final,true);
            const double L=lossFromRatio(ratioSpectrum(final,target,b,e,sr));
            if(L<best){
                best=L; bestM=candidate; bestA=trialA; bestB=spectrumOfFir(candidate.B);
                m=candidate; aState=bestA; bState=bestB;
            }else if(L>1.2*best){
                m=bestM; aState=bestA; bState=bestB; step*=0.5;
            }else{
                m=candidate; aState=trialA; bState=spectrumOfFir(candidate.B);
            }
            step*=0.9;
        }
    }
    m=bestM;
}

std::vector<double> finalTailRatio(const std::vector<float>&model,const std::vector<float>&target,double sr){
    const std::size_t begin=static_cast<std::size_t>(std::llround(50.0*sr));
    const std::size_t end=std::min({model.size(),target.size(),static_cast<std::size_t>(std::llround(70.0*sr))});
    const std::size_t L=std::max<std::size_t>(32,static_cast<std::size_t>(std::ceil(0.1*sr)));
    if(end<=begin+L)return std::vector<double>(kBins,1.0);
    std::vector<double> xm(L,0.0),yt(L,0.0); std::vector<std::size_t>cnt(L,0);
    for(std::size_t i=begin;i<end;++i){const std::size_t j=(i-begin)%L;xm[j]+=model[i];yt[j]+=target[i];++cnt[j];}
    for(std::size_t i=0;i<L;++i)if(cnt[i]){xm[i]/=cnt[i];yt[i]/=cnt[i];}
    const double mm=std::accumulate(xm.begin(),xm.end(),0.0)/static_cast<double>(L);
    const double tm=std::accumulate(yt.begin(),yt.end(),0.0)/static_cast<double>(L);
    const auto win=hamming(L); for(std::size_t i=0;i<L;++i){xm[i]=(xm[i]-mm)*win[i];yt[i]=(yt[i]-tm)*win[i];}

    const std::size_t posBins=L/2+1; std::vector<double>freq(posBins),rat(posBins,1.0);
    for(std::size_t k=0;k<posBins;++k){
        long double xre=0,xim=0,yre=0,yim=0; const double a=-2.0*kPi*static_cast<double>(k)/static_cast<double>(L);
        for(std::size_t n=0;n<L;++n){const double ph=a*static_cast<double>(n),c=std::cos(ph),si=std::sin(ph);xre+=xm[n]*c;xim+=xm[n]*si;yre+=yt[n]*c;yim+=yt[n]*si;}
        const double ax=std::hypot(static_cast<double>(xre),static_cast<double>(xim));
        const double ay=std::hypot(static_cast<double>(yre),static_cast<double>(yim));
        freq[k]=static_cast<double>(k)*sr/static_cast<double>(L); rat[k]=ay/(ax+kEps);
    }
    std::vector<double>out(kBins,1.0);
    for(std::size_t k=0;k<kBins;++k){const double hz=static_cast<double>(k)*sr/static_cast<double>(kFft);out[k]=interpLinear(freq,rat,hz);}
    out=melSmoothMagnitude(out,sr,37,37);
    for(auto&v:out)v=std::clamp(v,0.1,10.0);
    return out;
}

std::vector<float> convolveTruncate(const std::vector<float>&a,const std::vector<float>&b,std::size_t n){
    std::vector<float>o(n,0.0f); for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<b.size()&&i+j<n;++j)o[i+j]+=a[i]*b[j]; return o;
}
void refineB(Model&m,const std::vector<float>&input,const std::vector<float>&target,double sr,const StatusCallback&status){
    report(status,L"Independent: final Block B tail refinement...");
    std::vector<float>pred; renderModel(m,input,pred,true);
    auto r=finalTailRatio(pred,target,sr);
    auto c=minimumPhase(r,256);
    m.B=convolveTruncate(m.B,c,kB);
    const double mean=std::accumulate(m.B.begin(),m.B.end(),0.0)/static_cast<double>(m.B.size());
    for(auto&v:m.B)v=static_cast<float>(v-mean);
    renderModel(m,input,pred,true);
    const std::size_t b=static_cast<std::size_t>(std::llround(50.0*sr));
    const std::size_t e=std::min(pred.size(),static_cast<std::size_t>(std::llround(70.0*sr)));
    long double et=0,ep=0; for(std::size_t i=b;i<e;++i){et+=static_cast<long double>(target[i])*target[i];ep+=static_cast<long double>(pred[i])*pred[i];}
    if(ep>1e-30){const double g=std::sqrt(static_cast<double>(et/ep));for(auto&v:m.B)v=static_cast<float>(v*g);}
}

std::vector<float> resampleFirFixed(const std::vector<float>&h,double sr,std::size_t outLen){
    auto r=resampleR8Brain24(h,sr,44100.0);r.resize(outLen,0);return r;
}
bool serialize2048(const fs::path&path,const Model&m,double trainerRate,std::string&error){std::vector<std::uint8_t>d(kCloBytes,0);std::memcpy(d.data(),"VTSI",4);put32(d,0x04,0x2288);put32(d,0x14,0x2200);putDouble(d,0x18,1);putDouble(d,0x20,0);putDouble(d,0x28,0);putDouble(d,0x30,0);putDouble(d,0x38,0);
    putDouble(d,0x40,m.post.b0);putDouble(d,0x48,m.post.b1);putDouble(d,0x50,m.post.b2);putDouble(d,0x58,m.post.a1);putDouble(d,0x60,m.post.a2);putFloat(d,0x68,m.pk.pp);putFloat(d,0x6c,m.pk.pn);putFloat(d,0x70,m.pk.kp);putFloat(d,0x74,m.pk.kn);put32(d,0x78,0);put32(d,0x7c,128);put32(d,0x80,128);put32(d,0x84,2048);
    auto A44=resampleFirFixed(m.A,trainerRate,128),B44=resampleFirFixed(m.B,trainerRate,2048);for(auto&v:B44)v*=4.0f;for(std::size_t i=0;i<A44.size();++i)putFloat(d,0x88+4*i,A44[i]);for(std::size_t i=0;i<B44.size();++i)putFloat(d,0x88+4*(128+i),B44[i]);const auto crc=crc16Modbus(d.data()+0x0c,d.size()-0x0c);d[8]=static_cast<std::uint8_t>(crc>>8);d[9]=static_cast<std::uint8_t>(crc);return writeFileBytes(path,d.data(),d.size(),error);}

fs::path uniqueOutput(const fs::path&dir,const std::wstring&stem,const wchar_t*suffix){fs::path p=dir/(stem+suffix);int i=2;while(fs::exists(p))p=dir/(stem+L"_"+std::to_wstring(i++)+suffix);return p;}

} // namespace

ConversionResult convertNamToBothIndependent(const fs::path& inputNam,const fs::path& outputDirectory,
                                             StimulusConfig stimulus,IndependentTrainerConfig trainer,
                                             const StatusCallback& status){
    ConversionResult r;r.inputNam=inputNam;std::string error;std::error_code ec;if(!fs::exists(inputNam,ec)||ec){r.error="Input NAM does not exist.";return r;}fs::create_directories(outputDirectory,ec);if(ec){r.error="Cannot create output directory: "+ec.message();return r;}
    const auto runtime=resolveDefaultRuntime();const fs::path work=outputDirectory/(L".native_work_"+inputNam.stem().wstring());fs::remove_all(work,ec);fs::create_directories(work,ec);if(ec){r.error="Cannot create independent work directory.";return r;}
    const fs::path stim=work/L"stimulus_70s.wav";report(status,L"Independent: building stimulus...");if(!buildStimulus(runtime,stimulus,stim,error)){r.error=error;fs::remove_all(work,ec);return r;}
    std::vector<float>s44;std::uint32_t ssr=0;if(!readPcm16Mono(stim,s44,ssr,error)){r.error=error;fs::remove_all(work,ec);return r;}
    fs::path modelPath;if(!prepareFullA2(inputNam,work,modelPath,error)){r.error=error;fs::remove_all(work,ec);return r;}
    std::vector<float>input,target;double sr=48000;if(!renderNam(modelPath,s44,trainer.blockSize,trainer.namTargetScale,input,target,sr,error,status)){r.error=error;fs::remove_all(work,ec);return r;}
    detrend(target);const auto latency=detectLatency(target,sr);target=alignLeft(target,latency);report(status,L"Independent: detected NAM latency "+std::to_wstring(latency)+L" samples.");
    Model m;m.A.assign(kA,0);m.A[0]=1;m.B.assign(kB,0);m.B[0]=1;m.pk=fitPk(input,target,sr);m.post=postForRate(sr);{std::wostringstream os;os<<L"Independent: P/K = "<<m.pk.pp<<L" / "<<m.pk.pn<<L" / "<<m.pk.kp<<L" / "<<m.pk.kn;report(status,os.str());}
    fitAB(m,input,target,sr,status);refineB(m,input,target,sr,status);
    r.ampero2048=uniqueOutput(outputDirectory,inputNam.stem().wstring(),L"_NATIVE_2048.clo");if(!serialize2048(r.ampero2048,m,sr,error)){r.error=error;fs::remove_all(work,ec);return r;}
    r.gp2001024=uniqueOutput(outputDirectory,inputNam.stem().wstring(),L"_NATIVE_GP200_1024.clo");if(!makeGp200CompactClo(r.ampero2048,r.gp2001024,error)){r.error=error;fs::remove_all(work,ec);return r;}
    fs::remove_all(work,ec);r.ok=true;report(status,L"Independent conversion complete.");return r;
}

BatchConversionResult convertNamFolderIndependent(const fs::path& inputDirectory,const fs::path& outputDirectory,
                                                   StimulusConfig stimulus,IndependentTrainerConfig trainer,
                                                   const StatusCallback& status){BatchConversionResult b;std::error_code ec;std::vector<fs::path>files;for(const auto&e:fs::directory_iterator(inputDirectory,ec)){if(ec)break;if(!e.is_regular_file(ec)||ec)continue;auto ext=e.path().extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});if(ext==L".nam")files.push_back(e.path());}std::sort(files.begin(),files.end());b.total=files.size();for(std::size_t i=0;i<files.size();++i){report(status,L"Independent batch "+std::to_wstring(i+1)+L"/"+std::to_wstring(files.size())+L": "+files[i].filename().wstring());auto r=convertNamToBothIndependent(files[i],outputDirectory,stimulus,trainer,status);if(r.ok)++b.succeeded;else ++b.failed;b.items.push_back(std::move(r));}b.ok=b.total>0&&b.failed==0;return b;}

} // namespace ntc
