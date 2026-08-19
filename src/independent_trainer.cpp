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
constexpr std::size_t kA = 128;
constexpr std::size_t kB = 2048;
constexpr std::size_t kFft = 2048;
constexpr std::size_t kBins = kFft / 2 + 1;
constexpr std::size_t kCloBytes = 0x2288;

// GP-200.exe uses the MSVC "precise" double libm entry points for several
// float helpers: the float operand is promoted to double, libm is evaluated,
// then the result is rounded back to float. Keep those explicit boundaries.
inline float preciseLog10F(float x){return static_cast<float>(std::log10(static_cast<double>(x)));}
inline float preciseLogF(float x){return static_cast<float>(std::log(static_cast<double>(x)));}
inline float preciseExpF(float x){return static_cast<float>(std::exp(static_cast<double>(x)));}
inline float precisePowF(float a,float b){return static_cast<float>(std::pow(static_cast<double>(a),static_cast<double>(b)));}
inline float preciseSinF(float x){return static_cast<float>(std::sin(static_cast<double>(x)));}
inline float preciseCosF(float x){return static_cast<float>(std::cos(static_cast<double>(x)));}
inline float preciseSqrtF(float x){return static_cast<float>(std::sqrt(static_cast<double>(x)));}

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

// The official GP-200.exe RTTI contains CDSPResampler24 together with the
// pre-v4 templated r8brain type CDSPResampler<CDSPFracInterpolator<24,673>>.
// The project therefore pins the latest pre-v4 release (version-3.7) instead
// of following current r8brain master. The historical oneshot() overload
// requires a mutable float* input; resampleR8Brain24() supplies a working copy
// so MSVC does not instantiate Tin=const float.
std::vector<float> resampleR8Brain24(const std::vector<float>& in, double inRate, double outRate) {
    if (in.empty() || std::abs(inRate - outRate) < 1e-9) return in;

    // r8brain's pre-v4 oneshot() overload takes a mutable float* input even
    // though it only uses the samples as source data. Passing in.data() from a
    // const std::vector therefore makes MSVC instantiate Tin=const float and
    // fails in getOneshotBuf(float*). Keep the public input const and make the
    // mutable working copy required by the historical API.
    std::vector<float> mutableInput(in.begin(), in.end());

    const std::size_t outN = static_cast<std::size_t>(
        std::llround(static_cast<double>(mutableInput.size()) * outRate / inRate));
    std::vector<float> out(outN, 0.0f);
    if (outN == 0) return out;

    // oneshot() reproduces r8brain's own latency/tail draining and avoids the
    // hand-written process()/zero-flush loop used by the previous FINAL tree.
    // MaxInLen only controls the internal allocation limit for this call.
    const int maxInLen = static_cast<int>(mutableInput.size());
    r8b::CDSPResampler24 rs(inRate, outRate, maxInLen);
    rs.oneshot(mutableInput.data(),
               static_cast<int>(mutableInput.size()),
               out.data(),
               static_cast<int>(out.size()));
    rs.clear();
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
        float acc = 0.0f;
        const std::size_t kmax = std::min<std::size_t>(h.size() - 1, n);
        for (std::size_t k = 0; k <= kmax; ++k)
            acc += h[k] * in[n - k];
        out[n] = acc;
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

        // GP-200.exe does NOT feed the trainer's 600-sample guard area through
        // the NAM or through the sample-rate converter.  The source WAV is
        // exactly 70 seconds.  When the trainer loads input/output it allocates
        // Fs*70 + 600 floats, copies only the first Fs*70 samples and leaves the
        // final 600 floats zero.  StimulusBuilder also appends 600 samples for
        // the legacy HTUSBTools path, so strip them here before the independent
        // render and recreate the trainer buffers after rendering.
        constexpr std::size_t kSourceRate=44100;
        constexpr std::size_t kSource70=70*kSourceRate;
        std::vector<float> source70(kSource70,0.0f);
        const std::size_t sourceCopy=std::min(source70.size(),stimulus44100.size());
        std::copy_n(stimulus44100.begin(),sourceCopy,source70.begin());

        std::vector<float> renderedInput=resampleR8Brain24(source70,44100.0,rate);
        const float fs=static_cast<float>(rate);
        const float n70f=fs*70.0f; // same single-precision product used by the loader
        const std::size_t n70=static_cast<std::size_t>(std::max(0.0f,n70f));
        renderedInput.resize(n70,0.0f);
        std::vector<float> renderedTarget(n70,0.0f);

        dsp->Reset(rate,blockSize);
        std::vector<NAM_SAMPLE> ib(static_cast<std::size_t>(blockSize),NAM_SAMPLE{}),ob(static_cast<std::size_t>(blockSize),NAM_SAMPLE{});
        NAM_SAMPLE* ip[1]={ib.data()}; NAM_SAMPLE* op[1]={ob.data()};
        const float targetScaleF=static_cast<float>(targetScale);
        for(std::size_t pos=0;pos<n70;pos+=static_cast<std::size_t>(blockSize)){
            const int n=static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize),n70-pos));
            for(int i=0;i<n;++i)ib[static_cast<std::size_t>(i)]=static_cast<NAM_SAMPLE>(renderedInput[pos+static_cast<std::size_t>(i)]);
            // Zero the unused part of the fixed 1024-sample work block.  It is
            // not processed when n<blockSize, but keeping it deterministic also
            // mirrors the official renderer's zero-initialized work area.
            for(int i=n;i<blockSize;++i)ib[static_cast<std::size_t>(i)]=NAM_SAMPLE{};
            dsp->process(ip,op,n);
            for(int i=0;i<n;++i){
                const float y=static_cast<float>(ob[static_cast<std::size_t>(i)]);
                renderedTarget[pos+static_cast<std::size_t>(i)]=y*targetScaleF;
            }
        }

        input=std::move(renderedInput);
        target=std::move(renderedTarget);
        input.resize(n70+600,0.0f);
        target.resize(n70+600,0.0f);
        return true;
    }catch(const std::exception& e){error=std::string("NAM renderer: ")+e.what();return false;}
}

// Exact preprocessing reconstructed at GP-200.exe 0x559d80.
// The target buffer length is Fs*70+600.  All accumulation and arithmetic is
// float32.  First remove the mean, then fit/subtract a line using x=1..N.
void detrend(std::vector<float>& y){
    if(y.empty())return;
    const float n=static_cast<float>(y.size());

    float sum=0.0f;
    for(float v:y)sum+=v;
    const float mean=sum/n;
    for(float&v:y)v-=mean;

    float sumX=0.0f,sumY=0.0f,sumXY=0.0f,sumXX=0.0f;
    for(std::size_t i=0;i<y.size();++i){
        const float x=static_cast<float>(i+1);
        const float v=y[i];
        sumX+=x;
        sumY+=v;
        sumXY+=v*x;
        sumXX+=x*x;
    }

    const float numerator=n*sumXY-sumY*sumX;
    const float denominator=n*sumXX-sumX*sumX;
    const float slope=(denominator!=0.0f)?numerator/denominator:0.0f;
    const float intercept=(sumY-slope*sumX)/n;
    for(std::size_t i=0;i<y.size();++i){
        const float x=static_cast<float>(i+1);
        y[i]-=slope*x+intercept;
    }
}

std::size_t detectLatency(const std::vector<float>& y,double sr){
    // Official code truncates the trainer sample-rate float to int, searches
    // exactly 600 samples from 6*Fs and returns 600 if no threshold crossing is
    // found.  fabs(sample) is then compared as double against 0.01.
    const int fs=static_cast<int>(static_cast<float>(sr));
    if(fs<=0)return 600;
    const std::size_t base=static_cast<std::size_t>(6*fs);
    const std::size_t lim=std::min(y.size(),base+600);
    for(std::size_t i=base;i<lim;++i){
        const float av=std::fabs(y[i]);
        if(static_cast<double>(av)>0.01)return i-base;
    }
    return 600;
}
std::vector<float> alignLeft(const std::vector<float>& x,std::size_t n){std::vector<float> y(x.size(),0);if(n>=x.size())return y;std::copy(x.begin()+static_cast<std::ptrdiff_t>(n),x.end(),y.begin());return y;}

struct PK {float pp=.1f,pn=.1f,kp=1,kn=1;};

// Exact reconstruction of 0x558c30: 100 ms extrema over the first 5 s,
// P fixed by branch extrema, K seeded from the small-signal slope up to
// 0.5*P, then the seed is searched with the official multipliers
// 0.80, 0.85, ... 1.20. Positive K is selected first, then negative K.
PK fitPk(const std::vector<float>& in,const std::vector<float>& out,double sr){
    // 0x558c30 keeps the measurement vectors as float32.  Window length is
    // obtained by promoting the trainer sample-rate float to double, multiplying
    // by 0.1, then rounding back to float; window boundaries are float products
    // truncated to integer sample indices.
    const float fs=static_cast<float>(sr);
    const float winF=static_cast<float>(static_cast<double>(fs)*0.1);
    const std::size_t end=std::min(in.size(),std::min(out.size(),
        static_cast<std::size_t>(std::max(0.0,std::floor(5.0*static_cast<double>(fs))))));
    const int windows=(winF>0.0f)?static_cast<int>(std::floor(static_cast<double>(static_cast<float>(end)/winF))):0;
    struct M{float x,yp,yn;}; std::vector<M> m; m.reserve(std::max(0,windows));
    for(int wi=0;wi<windows;++wi){
        const int bi=static_cast<int>(static_cast<float>(wi)*winF);
        const int ei=static_cast<int>(static_cast<float>(wi+1)*winF);
        const std::size_t b=std::min<std::size_t>(end,std::max(0,bi));
        const std::size_t e=std::min<std::size_t>(end,std::max(0,ei));
        float x=0.0f,yp=0.0f,yn=0.0f;
        for(std::size_t i=b;i<e;++i){
            x=std::max(x,std::fabs(in[i]));
            yp=std::max(yp,out[i]);
            yn=std::max(yn,-out[i]);
        }
        m.push_back({x,std::max(0.0f,yp),std::max(0.0f,yn)});
    }
    PK r{}; if(m.empty())return r;
    float pp=0.0f,pn=0.0f;for(const auto&v:m){pp=std::max(pp,v.yp);pn=std::max(pn,v.yn);}
    if(!(pp>0.0f))pp=std::numeric_limits<float>::min();
    if(!(pn>0.0f))pn=std::numeric_limits<float>::min();
    r.pp=pp;r.pn=pn;

    auto crossingCount=[&](bool pos,float P){
        const double threshold=static_cast<double>(P)*0.5;
        int count=0;
        for(std::size_t i=0;i<m.size();++i){
            const float y=pos?m[i].yp:m[i].yn;
            if(static_cast<double>(y)>=threshold){count=static_cast<int>(i)+1;break;}
        }
        return count;
    };
    auto seedK=[&](bool pos,float P){
        int count=crossingCount(pos,P);
        if(count<=0)count=static_cast<int>(m.size());
        double sxy=0.0,sxx=0.0;
        for(int i=0;i<count;++i){
            const double x=static_cast<double>(m[static_cast<std::size_t>(i)].x);
            const double y=static_cast<double>(pos?m[static_cast<std::size_t>(i)].yp:m[static_cast<std::size_t>(i)].yn);
            sxy+=x*y;sxx+=x*x;
        }
        const float slope=static_cast<float>(sxx>0.0?sxy/sxx:0.0);
        return slope/P; // divss in the official routine
    };
    const float kp0=seedK(true,pp),kn0=seedK(false,pn);

    auto sse=[&](float kp,float kn){
        float err=0.0f;
        for(const auto&v:m){
            // The official loop evaluates the two signed transfer points using
            // float multiplies/subtracts around the precise exp helper and keeps
            // the squared-error accumulator in float32.
            const float ep=preciseExpF(-(kp*v.x));
            const float predP=pp*(1.0f-ep);
            const float dp=v.yp-predP; err+=dp*dp;
            const float en=preciseExpF(-(kn*v.x));
            const float predN=pn*(1.0f-en);
            const float dn=v.yn-predN; err+=dn*dn;
        }
        return err;
    };

    float bestKp=kp0,bestErr=std::numeric_limits<float>::max();
    float mul=0.800000011920929f;
    while(static_cast<double>(mul)<=1.2){
        const float cand=mul*kp0;const float e=sse(cand,kn0);
        if(e<=bestErr){bestErr=e;bestKp=cand;}
        mul=static_cast<float>(static_cast<double>(mul)+0.05);
    }
    float bestKn=kn0;bestErr=std::numeric_limits<float>::max();mul=0.800000011920929f;
    while(static_cast<double>(mul)<=1.2){
        const float cand=mul*kn0;const float e=sse(bestKp,cand);
        if(e<=bestErr){bestErr=e;bestKn=cand;}
        mul=static_cast<float>(static_cast<double>(mul)+0.05);
    }
    r.kp=bestKp;r.kn=bestKn;return r;
}
// Trainer biquad numerical path from GP-200.exe (0x557900 et al.).
// The official trainer uses direct-form II state in double, scales the input
// by 1000, rounds the internal output to float, then applies 0.001 and rounds
// to float again. The reciprocal scales cancel mathematically but the
// intermediate float conversion is observable and is therefore retained.
struct Biquad{
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    double w1=0,w2=0;
    float p(float x){
        const double w0=static_cast<double>(x)*1000.0-a1*w1-a2*w2;
        const double yd=b0*w0+b1*w1+b2*w2;
        w2=w1; w1=w0;
        const float rounded=static_cast<float>(yd);
        return static_cast<float>(static_cast<double>(rounded)*0.001);
    }
};
Biquad postForRate(double fs){
    // GP-200.exe computes this section in float, then stores/promotes the five
    // float32 results into the double CLO fields.  Keeping the arithmetic in
    // float reproduces the exact 48 kHz coefficients seen in official CLOs.
    constexpr float c=177.7158051f,w2=15791.45215f;
    const float f=static_cast<float>(fs),f2=f*f,D=f2+c*f+w2;
    const float b0=f2/D,b1=-2.0f*b0,b2=b0;
    const float a1=-(2.0f*f2-2.0f*w2)/D;
    const float a2=(f2-c*f+w2)/D;
    Biquad q;q.b0=static_cast<double>(b0);q.b1=static_cast<double>(b1);q.b2=static_cast<double>(b2);q.a1=static_cast<double>(a1);q.a2=static_cast<double>(a2);return q;
}
struct AP{float a=0,s=0;float p(float x){const float y=s+a*x;s=x-a*y;return y;}};
struct Poly{std::vector<AP>a,b;float d=0;Poly(std::initializer_list<float>x,std::initializer_list<float>y){for(float v:x)a.push_back({v,0});for(float v:y)b.push_back({v,0});}float r(std::vector<AP>&v,float x){for(auto&s:v)x=s.p(x);return x;}void up(float x,float&e,float&o){e=r(a,x);o=r(b,x);}float down(float e,float o){const float x=r(a,e),y=r(b,o),z=.5f*(x+d);d=y;return z;}};

bool powerOfTwo(std::size_t n){return n && !(n&(n-1));}
void fft(std::vector<std::complex<double>>& a,bool inv){
    const std::size_t n=a.size();
    for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){const double ang=(inv?2:-2)*kPi/static_cast<double>(len);const std::complex<double> wl(std::cos(ang),std::sin(ang));for(std::size_t i=0;i<n;i+=len){std::complex<double>w(1,0);for(std::size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wl;}}}
    if(inv)for(auto&v:a)v/=static_cast<double>(n);
}
void transformAny(std::vector<std::complex<double>>& a,bool inv){
    if(powerOfTwo(a.size())){fft(a,inv);return;}
    const std::size_t n=a.size(); std::vector<std::complex<double>> o(n);
    const double sign=inv?1.0:-1.0;
    for(std::size_t k=0;k<n;++k){std::complex<long double> sum(0,0);for(std::size_t j=0;j<n;++j){const long double ph=sign*2.0L*static_cast<long double>(kPi)*static_cast<long double>(j)*static_cast<long double>(k)/static_cast<long double>(n);const std::complex<long double>w(std::cos(ph),std::sin(ph));sum+=std::complex<long double>(a[j].real(),a[j].imag())*w;}if(inv)sum/=static_cast<long double>(n);o[k]={static_cast<double>(sum.real()),static_cast<double>(sum.imag())};}
    a.swap(o);
}

// 0x553aa0 / 0x55b2e0 / 0x55b460: the official trainer does not run
// one large FFT convolution.  It partitions every FIR into 64-sample blocks,
// transforms each partition with a 128-point FFT, processes input in 64-sample
// blocks, and overlap-adds the two 64-sample halves.  Reproduce that topology
// and float32 accumulation order here.  The internal FFT below is a conventional
// radix-2 complex form of the same 128-point transform.
void fft128PartitionF(std::array<std::complex<float>,128>& a,bool inv){
    constexpr std::size_t n=128;
    for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){
        const float ang=static_cast<float>((inv?2.0:-2.0)*kPi/static_cast<double>(len));
        const std::complex<float> wl(preciseCosF(ang),preciseSinF(ang));
        for(std::size_t i=0;i<n;i+=len){std::complex<float>w(1.0f,0.0f);for(std::size_t j=0;j<len/2;++j){const auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wl;}}
    }
    if(inv)for(auto&v:a)v*=0.0078125f; // 1/128
}
struct FirPlan{
    static constexpr std::size_t kPart=64,kN=128;
    std::size_t len=0,partitions=0;
    std::vector<std::array<std::complex<float>,kN>> H;
    FirPlan()=default;
    explicit FirPlan(const std::vector<float>&h){
        len=h.size();if(!len)return;
        partitions=(len+kPart-1)/kPart;H.resize(partitions);
        for(std::size_t p=0;p<partitions;++p){
            auto& z=H[p];for(auto&v:z)v={0.0f,0.0f};
            const std::size_t base=p*kPart;
            for(std::size_t i=0;i<kPart&&base+i<len;++i)z[i]={h[base+i],0.0f};
            fft128PartitionF(z,false);
        }
    }
    void run(const std::vector<float>&x,std::vector<float>&y)const{
        if(len==0||partitions==0){y=x;return;}
        y.assign(x.size(),0.0f);
        std::vector<std::array<std::complex<float>,kN>> ring(partitions);
        for(auto&r:ring)for(auto&v:r)v={0.0f,0.0f};
        std::array<float,kPart> overlap{};
        std::size_t current=0;
        for(std::size_t pos=0;pos<x.size();pos+=kPart){
            auto X=std::array<std::complex<float>,kN>{};
            const std::size_t count=std::min(kPart,x.size()-pos);
            for(std::size_t i=0;i<count;++i)X[i]={x[pos+i],0.0f};
            fft128PartitionF(X,false);ring[current]=X;
            std::array<std::complex<float>,kN> Y{};
            for(std::size_t p=0;p<partitions;++p){
                const std::size_t ri=(current+partitions-p)%partitions;
                for(std::size_t k=0;k<kN;++k)Y[k]+=ring[ri][k]*H[p][k];
            }
            fft128PartitionF(Y,true);
            for(std::size_t i=0;i<count;++i)y[pos+i]=Y[i].real()+overlap[i];
            for(std::size_t i=0;i<kPart;++i)overlap[i]=Y[i+kPart].real();
            current=(current+1)%partitions;
        }
    }
};

struct Model{std::vector<float>A,B;PK pk;Biquad post;};
void renderModel(const Model& m,const std::vector<float>& in,std::vector<float>& out,bool includeB=true){
    FirPlan ap(m.A);std::vector<float>a;ap.run(in,a);Biquad post=m.post;
    Poly u1({.045728147029876709f,.3325011134147644f,.66320204734802246f,.93385583162307739f},{.16808754205703735f,.50448572635650635f,.80378085374832153f});
    Poly u2({.054230779409408569f,.39879697561264038f,.86291784048080444f},{.19969958066940308f,.62109684944152832f});
    Poly d1({.070765949785709381f,.51316756010055542f},{.25785309076309204f,.81731736660003662f});
    Poly d2({.054217524826526642f,.38308733701705933f,.74872094392776489f},{.19679796695709229f,.57313638925552368f,.91429370641708374f});
    std::vector<float>pre(a.size());auto shape=[&](float x){if(x>0.0f){const float e=preciseExpF(-(m.pk.kp*x));return m.pk.pp*(1.0f-e);}const float e=preciseExpF(m.pk.kn*x);return m.pk.pn*(e-1.0f);};
    for(std::size_t i=0;i<a.size();++i){float e,o,q0,q1;u1.up(a[i],e,o);u2.up(e,q0,q1);q0=shape(q0);q1=shape(q1);const float z0=d1.down(q0,q1);u2.up(o,q0,q1);q0=shape(q0);q1=shape(q1);const float z1=d1.down(q0,q1);pre[i]=post.p(d2.down(z0,z1));}
    if(includeB){FirPlan bp(m.B);bp.run(pre,out);}else out=std::move(pre);
}
std::vector<float> sliceSignal(const std::vector<float>&x,std::size_t b,std::size_t e){b=std::min(b,x.size());e=std::min(e,x.size());if(e<=b)return {};return std::vector<float>(x.begin()+static_cast<std::ptrdiff_t>(b),x.begin()+static_cast<std::ptrdiff_t>(e));}

std::vector<float> hammingF(std::size_t n){std::vector<float>w(n,1.0f);if(n<=1)return w;for(std::size_t i=0;i<n;++i){const float ph=static_cast<float>(2.0*kPi*static_cast<double>(i)/static_cast<double>(n-1));w[i]=0.54f-0.46f*preciseCosF(ph);}return w;}
std::vector<double> fftFrequencyGrid(double sr){std::vector<double>f(kBins);for(std::size_t k=0;k<kBins;++k)f[k]=static_cast<double>(k)*(sr*0.5)/static_cast<double>(kBins-1);return f;}
std::vector<float> fftFrequencyGridF(double sr){std::vector<float>f(kBins);const float ny=static_cast<float>(sr*0.5);for(std::size_t k=0;k<kBins;++k)f[k]=static_cast<float>(k)*ny/static_cast<float>(kBins-1);return f;}
std::vector<double> linspace(double a,double b,std::size_t n){std::vector<double>v(n);if(!n)return v;if(n==1){v[0]=a;return v;}for(std::size_t i=0;i<n;++i)v[i]=a+(b-a)*static_cast<double>(i)/static_cast<double>(n-1);v.front()=a;v.back()=b;return v;}

void fftF(std::vector<std::complex<float>>& a,bool inv);

// 0x5557c0/related estimator: ceil(0.125*Fs) Hamming frames, 50% overlap,
// frame means removed, long frames folded modulo 2048, then float32 Sxx/Sxy.
std::vector<float> ratioSpectrumF(const std::vector<float>& model,const std::vector<float>& target,double sr){
    const std::size_t L=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(0.125*sr)));
    const std::size_t hop=std::max<std::size_t>(1,L/2); const auto w=hammingF(L);
    const std::size_t end=std::min(model.size(),target.size());
    if(end<L)return std::vector<float>(kBins,1.0f);
    std::vector<float>sxx(kBins,0.0f),sxyRe(kBins,0.0f),sxyIm(kBins,0.0f);std::size_t frames=0;
    for(std::size_t p=0;p+L<=end;p+=hop){
        float mx=0.0f,my=0.0f;for(std::size_t i=0;i<L;++i){mx+=model[p+i];my+=target[p+i];}mx/=static_cast<float>(L);my/=static_cast<float>(L);
        std::vector<std::complex<float>>X(kFft),Y(kFft);std::vector<float>xf(kFft,0.0f),yf(kFft,0.0f);
        for(std::size_t i=0;i<L;++i){const std::size_t j=i&(kFft-1);xf[j]+=(model[p+i]-mx)*w[i];yf[j]+=(target[p+i]-my)*w[i];}
        for(std::size_t i=0;i<kFft;++i){X[i]=xf[i];Y[i]=yf[i];}
        fftF(X,false);fftF(Y,false);++frames;
        for(std::size_t k=0;k<kBins;++k){const float xr=X[k].real(),xi=X[k].imag();const float yr=Y[k].real(),yi=Y[k].imag();sxx[k]+=xr*xr+xi*xi;sxyRe[k]+=xr*yr+xi*yi;sxyIm[k]+=xr*yi-xi*yr;}
    }
    if(!frames)return std::vector<float>(kBins,1.0f);
    std::vector<float>r(kBins,1.0f);for(std::size_t k=0;k<kBins;++k){const float mag=std::sqrt(sxyRe[k]*sxyRe[k]+sxyIm[k]*sxyIm[k]);r[k]=mag/(sxx[k]+static_cast<float>(kEps));}return r;
}
std::vector<double> ratioSpectrum(const std::vector<float>& model,const std::vector<float>& target,double sr){const auto r=ratioSpectrumF(model,target,sr);return std::vector<double>(r.begin(),r.end());}

float hzToMelF(float hz){float x=std::max(0.0f,hz);x/=700.0f;x+=1.0f;return preciseLog10F(x)*2595.0f;}
float melToHzF(float mel){const float e=mel/2595.0f;return (precisePowF(10.0f,e)-1.0f)*700.0f;}
double hzToMel(double hz){return static_cast<double>(hzToMelF(static_cast<float>(hz)));}double melToHz(double mel){return static_cast<double>(melToHzF(static_cast<float>(mel)));}
float interpLinearF(const std::vector<float>&x,const std::vector<float>&y,float q){if(x.empty()||y.empty())return 0.0f;if(q<=x.front())return y.front();if(q>=x.back())return y.back();const auto it=std::lower_bound(x.begin(),x.end(),q);const std::size_t b=static_cast<std::size_t>(it-x.begin()),a=b-1;const float dx=x[b]-x[a];if(std::abs(dx)<1e-30f)return y[a];const float t=(q-x[a])/dx;return y[a]+(y[b]-y[a])*t;}
double interpLinear(const std::vector<double>&x,const std::vector<double>&y,double q){if(x.empty()||y.empty())return 0.0;if(q<=x.front())return y.front();if(q>=x.back())return y.back();const auto it=std::lower_bound(x.begin(),x.end(),q);const std::size_t b=static_cast<std::size_t>(it-x.begin()),a=b-1;const double dx=x[b]-x[a];if(std::abs(dx)<1e-30)return y[a];const double t=(q-x[a])/dx;return y[a]+(y[b]-y[a])*t;}
std::vector<float> linspaceF(float a,float b,std::size_t n){std::vector<float>v(n);if(!n)return v;if(n==1){v[0]=a;return v;}const float den=static_cast<float>(n-1);for(std::size_t i=0;i<n;++i)v[i]=a+(b-a)*(static_cast<float>(i)/den);v.front()=a;v.back()=b;return v;}

// Exact 0x555460 Gaussian path. GP-200.exe constructs and normalizes
// the kernel in double precision, reverses it, allocates N+1 entries for an
// even N (the extra entry remains zero), and accumulates float samples times
// double weights in double precision. Border samples are renormalized using
// only the weights that actually participate in the convolution.
std::vector<double> gaussianKernelExactD(std::size_t n){
    n=std::max<std::size_t>(1,n);
    const std::size_t storage=(n&1u)?n:n+1;
    std::vector<double> raw(n),g(storage,0.0);
    const double step=5.0/static_cast<double>(n);
    const double center=std::ceil(static_cast<double>(n)*0.5);
    double sum=0.0;
    for(std::size_t i=0;i<n;++i){
        const double x=(static_cast<double>(i+1)-center)*step;
        raw[i]=std::exp(-0.5*x*x);
        sum+=raw[i];
    }
    if(sum!=0.0)for(auto&v:raw)v/=sum;
    for(std::size_t i=0;i<n;++i)g[i]=raw[n-1-i];
    return g;
}
std::vector<float> gaussianSmoothExactF(const std::vector<float>&v,std::size_t n){
    if(v.empty())return {};
    const auto g=gaussianKernelExactD(n);
    const long center=static_cast<long>(g.size()/2);
    std::vector<float>o(v.size());
    for(std::size_t i=0;i<v.size();++i){
        double s=0.0,w=0.0;
        for(std::size_t k=0;k<g.size();++k){
            const long j=static_cast<long>(i)+static_cast<long>(k)-center;
            if(j<0||j>=static_cast<long>(v.size())||g[k]==0.0)continue;
            s+=static_cast<double>(v[static_cast<std::size_t>(j)])*g[k];
            w+=g[k];
        }
        o[i]=w>0.0?static_cast<float>(s/w):v[i];
    }
    return o;
}
std::vector<double> gaussianSmoothExact(const std::vector<double>&v,std::size_t n){
    std::vector<float>x(v.size());
    for(std::size_t i=0;i<v.size();++i)x[i]=static_cast<float>(v[i]);
    const auto y=gaussianSmoothExactF(x,n);
    return std::vector<double>(y.begin(),y.end());
}

struct ConditionedMagnitude{std::vector<double>freq,mag;};
struct ConditionedMagnitudeF{std::vector<float>freq,mag;};
// 0x554f00, exact operation order recovered from GP-200.exe.
//
// Official order:
//   1. source magnitudes -> dB on the ORIGINAL source-frequency grid;
//   2. Gaussian #1 on that original grid (floor(N1 * 0.002));
//   3. interpolate original Hz -> N1 points uniformly spaced in Mel;
//   4. Gaussian #2 on that Mel-spaced N1 curve (2 * floor(N1/N2));
//   5. if N1 != N2, interpolate N1-Mel -> N2-Mel;
//   6. interpolate N2-Mel -> N2 points linearly spaced in Hz;
//   7. dB -> magnitude.
ConditionedMagnitudeF conditionMagnitudeF(const std::vector<float>&srcFreq,const std::vector<float>&srcMag,std::size_t destCount){
    const std::size_t n=std::min(srcFreq.size(),srcMag.size());
    ConditionedMagnitudeF out;
    if(!n||!destCount)return out;

    const float f0=srcFreq.front(),f1=srcFreq[n-1];
    std::vector<float> sf(srcFreq.begin(),srcFreq.begin()+static_cast<std::ptrdiff_t>(n));
    std::vector<float> db(n);
    for(std::size_t i=0;i<n;++i)
        db[i]=20.0f*preciseLog10F(std::max(1.0e-20f,srcMag[i]));

    const std::size_t k1=std::max<std::size_t>(
        1,static_cast<std::size_t>(static_cast<double>(n)*0.002));
    db=gaussianSmoothExactF(db,k1);

    const auto mel1=linspaceF(hzToMelF(f0),hzToMelF(f1),n);
    std::vector<float> melHz1(n);
    for(std::size_t i=0;i<n;++i)melHz1[i]=melToHzF(mel1[i]);
    melHz1.front()=f0; melHz1.back()=f1;

    std::vector<float> melDb1(n);
    for(std::size_t i=0;i<n;++i)melDb1[i]=interpLinearF(sf,db,melHz1[i]);

    const std::size_t k2=std::max<std::size_t>(1,2*(n/destCount));
    melDb1=gaussianSmoothExactF(melDb1,k2);

    const auto mel2=linspaceF(hzToMelF(f0),hzToMelF(f1),destCount);
    std::vector<float> melHz2(destCount),melDb2(destCount);
    for(std::size_t i=0;i<destCount;++i)melHz2[i]=melToHzF(mel2[i]);
    melHz2.front()=f0; melHz2.back()=f1;

    if(n==destCount){
        melDb2=melDb1;
    }else{
        for(std::size_t i=0;i<destCount;++i)
            melDb2[i]=interpLinearF(melHz1,melDb1,melHz2[i]);
    }

    out.freq=linspaceF(f0,f1,destCount);
    out.mag.resize(destCount);
    for(std::size_t i=0;i<destCount;++i){
        const float d=interpLinearF(melHz2,melDb2,out.freq[i]);
        out.mag[i]=precisePowF(10.0f,d*0.05f);
    }
    return out;
}
ConditionedMagnitude conditionMagnitude(const std::vector<double>&srcFreq,const std::vector<double>&srcMag,std::size_t destCount){const std::size_t n=std::min(srcFreq.size(),srcMag.size());std::vector<float>f(n),m(n);for(std::size_t i=0;i<n;++i){f[i]=static_cast<float>(srcFreq[i]);m[i]=static_cast<float>(srcMag[i]);}auto cf=conditionMagnitudeF(f,m,destCount);ConditionedMagnitude o;o.freq.assign(cf.freq.begin(),cf.freq.end());o.mag.assign(cf.mag.begin(),cf.mag.end());return o;}

void lowSmoothASequentialF(std::vector<float>&m,double sr){
    if(m.size()<2)return;const std::size_t n=m.size();const float fs=static_cast<float>(sr);const std::size_t lim=std::min<std::size_t>(n-1,static_cast<std::size_t>(std::floor(static_cast<double>((2.0f/fs)*60.0f*static_cast<float>(n)))));
    m[0]=preciseSqrtF(std::max(1.0e-30f,m[0]*preciseSqrtF(std::max(1.0e-30f,m[0]*m[1]))));
    for(std::size_t i=1;i<lim&&i+1<n;++i)m[i]=preciseSqrtF(std::max(1.0e-30f,m[i]*preciseSqrtF(std::max(1.0e-30f,m[i-1]*m[i+1]))));
}
void lowSmoothASequential(std::vector<double>&m,double sr){std::vector<float>x(m.begin(),m.end());lowSmoothASequentialF(x,sr);m.assign(x.begin(),x.end());}

void fftF(std::vector<std::complex<float>>& a,bool inv){
    const std::size_t n=a.size();for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){const float ang=static_cast<float>((inv?2.0:-2.0)*kPi/static_cast<double>(len));const std::complex<float>wl(preciseCosF(ang),preciseSinF(ang));for(std::size_t i=0;i<n;i+=len){std::complex<float>w(1,0);for(std::size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wl;}}}if(inv){const float d=static_cast<float>(n);for(auto&v:a)v/=d;}
}
void transformAnyF(std::vector<std::complex<float>>& a,bool inv){
    if(powerOfTwo(a.size())){fftF(a,inv);return;}const std::size_t n=a.size();std::vector<std::complex<float>>o(n);const float sign=inv?1.0f:-1.0f;
    for(std::size_t k=0;k<n;++k){std::complex<float>sum(0,0);for(std::size_t j=0;j<n;++j){const float ph=sign*static_cast<float>(2.0*kPi*static_cast<double>(j)*static_cast<double>(k)/static_cast<double>(n));sum+=a[j]*std::complex<float>(preciseCosF(ph),preciseSinF(ph));}if(inv)sum/=static_cast<float>(n);o[k]=sum;}a.swap(o);
}
std::vector<float> minimumPhaseF(const std::vector<float>&positive,std::size_t taps){
    if(positive.size()<2||!taps)return std::vector<float>(taps,0.0f);const std::size_t posN=positive.size(),fullN=2*posN-2;std::vector<float>m(fullN);
    for(std::size_t i=0;i<posN;++i)m[i]=std::max(1.0e-30f,positive[i]);for(std::size_t i=1;i+1<posN;++i)m[fullN-i]=m[i];
    const float mx=*std::max_element(m.begin(),m.end()),floor=mx*1.0e-5f;std::vector<std::complex<float>>c(fullN);for(std::size_t i=0;i<fullN;++i)c[i]=static_cast<float>(std::log(static_cast<double>(std::max(floor,m[i])+static_cast<float>(kEps))));
    transformAnyF(c,true);for(std::size_t i=1;i<fullN/2;++i)c[i]*=2.0f;for(std::size_t i=fullN/2+1;i<fullN;++i)c[i]=0.0f;transformAnyF(c,false);for(auto&v:c){const float amp=preciseExpF(v.real());const float ph=v.imag();v={amp*preciseCosF(ph),amp*preciseSinF(ph)};}transformAnyF(c,true);
    float fullNorm2=0.0f;for(const auto&v:c)fullNorm2+=v.real()*v.real();std::vector<float>h(taps,0.0f);for(std::size_t i=0;i<std::min(taps,c.size());++i)h[i]=c[i].real();
    float sum=0.0f;for(float v:h)sum+=v;const float mean=sum/static_cast<float>(h.size());for(auto&v:h)v-=mean;float shortNorm2=0.0f;for(float v:h)shortNorm2+=v*v;
    if(shortNorm2>1.0e-30f&&fullNorm2>0.0f){const float g=preciseSqrtF(fullNorm2/shortNorm2);for(auto&v:h)v*=g;}return h;
}
std::vector<float> minimumPhase(const std::vector<double>&positive,std::size_t taps){std::vector<float>x(positive.size());for(std::size_t i=0;i<positive.size();++i)x[i]=static_cast<float>(positive[i]);return minimumPhaseF(x,taps);}


// Main trainer setup (0x55a261..0x55a311) initializes the exponent
// vector to 1.0f, then overwrites it from floor((2/Fs)*80*N)-1 to the end
// with a float linspace from 1.0f to 0.5f.  This vector is multiplied by
// `step` at 0x5574e0 before pow().
std::vector<float> frequencyWeightsF(double sr){
    std::vector<float>w(kBins,1.0f);
    const float fs=static_cast<float>(sr);
    const float raw=(2.0f/fs)*80.0f*static_cast<float>(kBins);
    const int idx=static_cast<int>(std::floor(static_cast<double>(raw)));
    const std::size_t start=idx>0?std::min<std::size_t>(kBins-1,static_cast<std::size_t>(idx-1)):0;
    const std::size_t count=kBins-start;
    if(count==1){w[start]=1.0f;return w;}
    const float delta=(0.5f-1.0f)/static_cast<float>(count-1);
    for(std::size_t i=0;i<count;++i)w[start+i]=1.0f+delta*static_cast<float>(i);
    w[start]=1.0f; w.back()=0.5f;
    return w;
}

float lossFromRatioF(const std::vector<float>&r,double sr){
    const auto srcF=fftFrequencyGridF(sr);const float m0=hzToMelF(80.0f),m1=hzToMelF(10000.0f);float sum=0.0f;
    for(std::size_t i=0;i<512;++i){const float mel=m0+(m1-m0)*static_cast<float>(i)/511.0f;float f=melToHzF(mel);if(i==0)f=80.0f;if(i==511)f=10000.0f;const float v=interpLinearF(srcF,r,f);sum+=std::fabs(preciseLogF(v+static_cast<float>(kEps)));}
    return sum*(1.0f/512.0f);
}
double lossFromRatio(const std::vector<double>&r,double sr){std::vector<float>x(r.begin(),r.end());return static_cast<double>(lossFromRatioF(x,sr));}

void regularizeInitialCurveF(std::vector<float>&v,const std::vector<float>&reference){if(v.empty()||reference.empty())return;const float mx=*std::max_element(reference.begin(),reference.end());const float f=std::max(1.0e-30f,0.001f*mx);for(auto&x:v)if(x<2.0f*f)x=f+(x*x)/(4.0f*f);}
void regularizeInitialCurve(std::vector<double>&v,const std::vector<double>&reference){std::vector<float>x(v.begin(),v.end()),r(reference.begin(),reference.end());regularizeInitialCurveF(x,r);v.assign(x.begin(),x.end());}

struct FactorState{std::vector<float>a,b;};
FactorState initialFactorState(const Model&m,const std::vector<float>&input,const std::vector<float>&target,double sr){
    const std::size_t lb=static_cast<std::size_t>(std::llround(6.0*sr)),le=static_cast<std::size_t>(std::llround(21.0*sr));
    const std::size_t sb=static_cast<std::size_t>(std::llround(23.0*sr)),se=static_cast<std::size_t>(std::llround(28.0*sr));
    auto lowIn=sliceSignal(input,lb,le),lowTarget=sliceSignal(target,lb,le);std::vector<float>lowPred;renderModel(m,lowIn,lowPred,true);auto lowSpec=ratioSpectrumF(lowPred,lowTarget,sr);
    auto sweepIn=sliceSignal(input,sb,se),sweepTarget=sliceSignal(target,sb,se);sweepIn=applyInitialConditioningFir(sweepIn,sr);std::vector<float>sweepPred;renderModel(m,sweepIn,sweepPred,true);auto sweepSpec=ratioSpectrumF(sweepPred,sweepTarget,sr);
    const std::size_t n=sweepSpec.size();sweepSpec=gaussianSmoothExactF(sweepSpec,std::max<std::size_t>(1,static_cast<std::size_t>(static_cast<float>(n)*0.001f)));sweepSpec=gaussianSmoothExactF(sweepSpec,std::max<std::size_t>(1,static_cast<std::size_t>(static_cast<float>(n)*0.005f)));regularizeInitialCurveF(sweepSpec,lowSpec);
    FactorState st;st.a.resize(kBins,1.0f);st.b=sweepSpec;for(std::size_t k=0;k<kBins;++k){const double num=static_cast<double>(lowSpec[k])*1000000.0;const double den=static_cast<double>(sweepSpec[k])*1000000.0+kEps;st.a[k]=static_cast<float>(num/den);}return st;
}

struct Phase{double t0,t1;int iterations;const wchar_t*name;};
void optimizePhase(Model&m,FactorState&state,const std::vector<float>&input,const std::vector<float>&target,double sr,const Phase&ph,int&globalIter,const StatusCallback&status){
    const auto freq=fftFrequencyGridF(sr),weights=frequencyWeightsF(sr);const std::size_t b=static_cast<std::size_t>(std::llround(ph.t0*sr)),e=static_cast<std::size_t>(std::llround(ph.t1*sr));const auto phaseIn=sliceSignal(input,b,e),phaseTarget=sliceSignal(target,b,e);
    float step=1.0f,bestLoss=100.0f;Model bestM=m;FactorState bestState=state;std::vector<float>corr(kBins,1.0f),bestCorr=corr;
    for(int it=0;it<ph.iterations;++it){
        ++globalIter;report(status,L"Independent: A/B fit "+std::to_wstring(globalIter)+L"/10 ("+ph.name+L")...");
        // 0x5574e0: exponent = frequencyWeight[k] * step.  The weight vector
        // is prepared by the caller as 1.0 above ~80 Hz-index onset and a
        // 1.0 -> 0.5 ramp through the remainder of the spectrum.
        std::vector<float>stepped(kBins);for(std::size_t k=0;k<kBins;++k){const float base=std::max(1.0e-30f,corr[k]);const float exponent=weights[k]*step;stepped[k]=std::clamp(precisePowF(base,exponent),0.2f,5.0f);}
        // Confirmed at 0x557532..0x557563: the correction traverses 0x554f00
        // twice, back-to-back, before either factor state is updated.
        const auto conditioned1=conditionMagnitudeF(freq,stepped,kBins);
        const auto conditioned2=conditionMagnitudeF(conditioned1.freq,conditioned1.mag,kBins);
        const auto& conditioned=conditioned2.mag;
        FactorState trial=state;for(std::size_t k=0;k<kBins;++k){trial.b[k]*=conditioned[k];trial.a[k]/=conditioned[k];}lowSmoothASequentialF(trial.a,sr);
        Model candidate=m;const auto amag=conditionMagnitudeF(freq,trial.a,kA);candidate.A=minimumPhaseF(amag.mag,kA);
        std::vector<float>pre;renderModel(candidate,phaseIn,pre,false);auto fresh=ratioSpectrumF(pre,phaseTarget,sr);std::vector<float>rb(kBins);for(std::size_t k=0;k<kBins;++k){const double num=static_cast<double>(fresh[k])*1000000.0;const double den=static_cast<double>(trial.b[k])*1000000.0+kEps;rb[k]=static_cast<float>(num/den);}
        auto bmag=conditionMagnitudeF(freq,rb,kBins);candidate.B=minimumPhaseF(bmag.mag,kB);
        std::vector<float>final;renderModel(candidate,phaseIn,final,true);auto residual=ratioSpectrumF(final,phaseTarget,sr);const float loss=lossFromRatioF(residual,sr);
        if(loss<bestLoss){bestLoss=loss;bestM=candidate;bestState=trial;bestCorr=residual;m=candidate;state=trial;corr=residual;}
        else if(loss>1.2f*bestLoss){m=bestM;state=bestState;corr=bestCorr;step*=0.5f;}
        else{m=candidate;state=trial;corr=residual;}
        step*=0.8999999761581421f;
    }
    m=bestM;state=bestState;
}

void fitAB(Model&m,const std::vector<float>&input,const std::vector<float>&target,double sr,const StatusCallback&status){
    report(status,L"Independent: initial low-level / conditioned-sweep factorization...");FactorState state=initialFactorState(m,input,target,sr);int globalIter=0;
    const Phase phases[]={{23,28,3,L"sweep"},{6,21,2,L"low-level"},{30,50,5,L"multi-level"}};for(const auto&ph:phases)optimizePhase(m,state,input,target,sr,ph,globalIter,status);
}

std::vector<float> convolveTruncate(const std::vector<float>&a,const std::vector<float>&b,std::size_t n){std::vector<float>o(n,0.0f);for(std::size_t i=0;i<a.size();++i)for(std::size_t j=0;j<b.size()&&i+j<n;++j)o[i+j]+=a[i]*b[j];return o;}

std::vector<float> finalTailCorrection(const std::vector<float>&model,const std::vector<float>&target,double sr){
    const std::size_t L=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(0.1*sr)));const std::size_t end=std::min(model.size(),target.size());if(end<L)return std::vector<float>(256,1.0f);
    std::vector<float>xm(L,0.0f),yt(L,0.0f);for(std::size_t i=0;i<end;++i){const std::size_t j=i%L;xm[j]+=model[i];yt[j]+=target[i];}
    float mm=0.0f,tm=0.0f;for(std::size_t i=0;i<L;++i){mm+=xm[i];tm+=yt[i];}mm/=static_cast<float>(L);tm/=static_cast<float>(L);const auto win=hammingF(L);for(std::size_t i=0;i<L;++i){xm[i]=(xm[i]-mm)*win[i];yt[i]=(yt[i]-tm)*win[i];}
    const std::size_t posN=L/2+1;std::vector<float>freq(posN),modelMag(posN),targetMag(posN);
    // Official path explicitly DFTs the two folded 100 ms signals, keeping
    // float32 accumulators/magnitudes.
    for(std::size_t k=0;k<posN;++k){float xr=0.0f,xi=0.0f,yr=0.0f,yi=0.0f;const float w0=static_cast<float>(-2.0*kPi*static_cast<double>(k)/static_cast<double>(L));for(std::size_t n=0;n<L;++n){const float ph=w0*static_cast<float>(n),c=preciseCosF(ph),sn=preciseSinF(ph);xr+=xm[n]*c;xi+=xm[n]*sn;yr+=yt[n]*c;yi+=yt[n]*sn;}modelMag[k]=preciseSqrtF(xr*xr+xi*xi);targetMag[k]=preciseSqrtF(yr*yr+yi*yi);freq[k]=static_cast<float>(static_cast<double>(k)*sr/static_cast<double>(L));}

    // Critical ordering from 0x556670: condition target magnitude and model
    // magnitude independently with 0x554f00 BEFORE computing their ratio.
    const auto ct=conditionMagnitudeF(freq,targetMag,posN);
    const auto cm=conditionMagnitudeF(freq,modelMag,posN);
    std::vector<float>ratio(posN,1.0f);for(std::size_t k=0;k<posN;++k){const double num=static_cast<double>(ct.mag[k])*1000000.0;const double den=static_cast<double>(cm.mag[k])*1000000.0+kEps;ratio[k]=std::clamp(static_cast<float>(num/den),0.1f,10.0f);}
    const std::size_t smoothN=std::max<std::size_t>(1,static_cast<std::size_t>(static_cast<float>(posN)*0.1f));ratio=gaussianSmoothExactF(ratio,smoothN);for(auto&v:ratio)v=std::clamp(v,0.1f,10.0f);
    const auto final=conditionMagnitudeF(ct.freq,ratio,256);return final.mag;
}

void refineB(Model&m,const std::vector<float>&input,const std::vector<float>&target,double sr,const StatusCallback&status){
    report(status,L"Independent: final Block B tail refinement...");const std::size_t b=static_cast<std::size_t>(std::llround(50.0*sr)),e=static_cast<std::size_t>(std::llround(70.0*sr));const auto tailIn=sliceSignal(input,b,e),tailTarget=sliceSignal(target,b,e);std::vector<float>pred;renderModel(m,tailIn,pred,true);
    auto corrMag=finalTailCorrection(pred,tailTarget,sr);auto corr=minimumPhaseF(corrMag,256);m.B=convolveTruncate(m.B,corr,kB);
    float sum=0.0f;for(float v:m.B)sum+=v;const float mean=sum/static_cast<float>(m.B.size());for(auto&v:m.B)v-=mean;
    renderModel(m,tailIn,pred,true);float et=0.0f,ep=0.0f;for(std::size_t i=0;i<std::min(pred.size(),tailTarget.size());++i){et+=tailTarget[i]*tailTarget[i];ep+=pred[i]*pred[i];}if(ep>1.0e-30f){const float g=preciseSqrtF(et)/preciseSqrtF(ep);for(auto&v:m.B)v*=g;}
}

std::vector<float> resampleFirFixed(const std::vector<float>&h,double sr,std::size_t outLen){
    if(std::abs(sr-44100.0)<1e-9){auto r=h;r.resize(outLen,0.0f);return r;}
    auto r=resampleR8Brain24(h,sr,44100.0);
    // The official FIR wrapper's effective coefficient count is truncated,
    // not rounded.  For A128 at 48 kHz this yields 117 non-zero samples,
    // matching the official golden CLO (NATIVE7 produced 118).
    const std::size_t effective=std::min(outLen,static_cast<std::size_t>(std::floor(static_cast<double>(h.size())*44100.0/sr)));
    if(r.size()>effective)r.resize(effective);r.resize(outLen,0.0f);return r;
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
