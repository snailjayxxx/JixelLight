// Standalone arithmetic probe. It does not link or change the image pipeline.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

using Pixel=std::uint16_t;
volatile std::uint64_t ProbeSink=0;
Pixel original(float value) {
    return Pixel(std::lround(std::clamp(value,0.f,1.f)*65535.f));
}
Pixel candidate(float value) {
    const float scaled=std::clamp(value,0.f,1.f)*65535.f;
    // Preserve the original nonfinite path; no invalid float-to-int cast.
    if(!std::isfinite(scaled))return Pixel(std::lround(scaled));
    // Keep the original float product. The wider addition avoids rounding a
    // value immediately below a half-integer across its integer boundary.
    return Pixel(static_cast<unsigned>(static_cast<double>(scaled)+.5));
}
int main() {
    std::uint64_t cases=0;
    auto check=[&](float value){++cases;return original(value)==candidate(value);};
    bool equal=true;
    for(unsigned i=0;i<65535;++i) {
        const float value=(float(i)+.5f)/65535.f;
        equal=check(std::nextafter(value,-std::numeric_limits<float>::infinity()))&&equal;
        equal=check(value)&&equal;
        equal=check(std::nextafter(value,std::numeric_limits<float>::infinity()))&&equal;
    }
    for(float value:std::array<float,9>{-1e9f,-1.f,-0.f,0.f,1.f,2.f,1e9f,-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity()})equal=check(value)&&equal;
    constexpr std::size_t count=2*1024*1024;
    std::vector<float> input(count);std::vector<Pixel> oldOutput(count),newOutput(count);
    std::uint32_t state=0x4a495845;
    for(auto &value:input) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        value=float(state&0xffffffu)/float(0xffffffu);equal=check(value)&&equal;
    }
    auto oldRun=[&]{for(std::size_t i=0;i<count;++i)oldOutput[i]=original(input[i]);};
    auto newRun=[&]{for(std::size_t i=0;i<count;++i)newOutput[i]=candidate(input[i]);};
    auto consume=[](const auto &output){std::uint64_t sum=0;for(auto value:output)sum+=value;ProbeSink=sum;};
    oldRun();consume(oldOutput);newRun();consume(newOutput);newRun();consume(newOutput);oldRun();consume(oldOutput);
    std::array<double,7> oldMs{},newMs{};
    auto timed=[&](auto &&run,const auto &output){const auto start=std::chrono::steady_clock::now();run();const auto end=std::chrono::steady_clock::now();consume(output);return std::chrono::duration<double,std::milli>(end-start).count();};
    for(std::size_t i=0;i<oldMs.size();++i) {
        if(i%2){newMs[i]=timed(newRun,newOutput);oldMs[i]=timed(oldRun,oldOutput);}
        else{oldMs[i]=timed(oldRun,oldOutput);newMs[i]=timed(newRun,newOutput);}
    }
    equal=equal&&oldOutput==newOutput;
    std::uint64_t checksum=0;for(auto value:newOutput)checksum+=value;
    std::cout<<std::fixed<<std::setprecision(6)<<"{\n  \"scope\": \"standalone bounded arithmetic probe; no App, RAW, GPU or deadline acceptance\",\n";
#ifdef _MSC_FULL_VER
    std::cout<<"  \"MSVC_full_version\": "<<_MSC_FULL_VER<<",\n";
#endif
    std::cout<<"  \"exact_cases\": "<<cases<<",\n  \"outputs_equal\": "<<(equal?"true":"false")<<",\n  \"input_values\": "<<count<<",\n  \"checksum\": "<<checksum<<",\n";
    auto samples=[](const auto &values){std::cout<<'[';for(std::size_t i=0;i<values.size();++i){if(i)std::cout<<',';std::cout<<values[i];}std::cout<<']';};
    std::cout<<"  \"original_ms\": ";samples(oldMs);std::cout<<",\n  \"candidate_ms\": ";samples(newMs);
    std::sort(oldMs.begin(),oldMs.end());std::sort(newMs.begin(),newMs.end());
    std::cout<<",\n  \"original_median_ms\": "<<oldMs[3]<<",\n  \"candidate_median_ms\": "<<newMs[3]<<",\n  \"order\": \"two warmups each; seven alternating-order pairs; cache/thermal/host effects not isolated\"\n}\n";
    return equal?0:1;
}
