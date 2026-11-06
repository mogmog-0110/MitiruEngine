// FMA3 の有効と無効で、UCRT の数学関数の結果が 1 台の中で変わるかを数える (docs/DETERMINISM.md の測定 1)。
// 組み方: cl /O2 /EHsc /MD /std:c++17 tools/fma3_probe.cpp。Debug の CRT は /Od /MDd。FMA3 を持つ CPU でだけ意味がある。
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include <intrin.h>
static uint64_t bitsd(double d){uint64_t u;memcpy(&u,&d,8);return u;}
static uint32_t bitsf(float f){uint32_t u;memcpy(&u,&f,4);return u;}
struct Fn{const char*name; std::function<uint64_t(double)> f;};
int main(){
  int cpu[4]; __cpuid(cpu,1); std::printf("cpu fma bit=%d, get_default=%d\n",(cpu[2]>>12)&1,_get_FMA3_enable());
  std::vector<Fn> fns={
   {"sinf",[](double x){return (uint64_t)bitsf(std::sin((float)x));}},
   {"cosf",[](double x){return (uint64_t)bitsf(std::cos((float)x));}},
   {"tanf",[](double x){return (uint64_t)bitsf(std::tan((float)x));}},
   {"atan2f",[](double x){return (uint64_t)bitsf(std::atan2((float)x,1.7f));}},
   {"expf",[](double x){return (uint64_t)bitsf(std::exp((float)x*0.01f));}},
   {"logf",[](double x){return (uint64_t)bitsf(std::log((float)std::fabs(x)+0.001f));}},
   {"powf",[](double x){return (uint64_t)bitsf(std::pow((float)std::fabs(x)+0.01f,1.37f));}},
   {"sin",[](double x){return bitsd(std::sin(x));}},
   {"cos",[](double x){return bitsd(std::cos(x));}},
   {"tan",[](double x){return bitsd(std::tan(x));}},
   {"atan2",[](double x){return bitsd(std::atan2(x,1.7));}},
   {"exp",[](double x){return bitsd(std::exp(x*0.01));}},
   {"log",[](double x){return bitsd(std::log(std::fabs(x)+0.001));}},
   {"pow",[](double x){return bitsd(std::pow(std::fabs(x)+0.01,1.37));}},
   {"sqrtf",[](double x){return (uint64_t)bitsf(std::sqrt((float)std::fabs(x)));}},
  };
  const int N=200000;
  std::vector<std::vector<uint64_t>> res[2];
  for(int mode=0;mode<2;mode++){
    int r=_set_FMA3_enable(mode);
    std::printf("set(%d)->%d get=%d\n",mode,r,_get_FMA3_enable());
    for(auto&fn:fns){std::vector<uint64_t> v;v.reserve(N);
      for(int i=0;i<N;i++){double x=((i*0.0137)-1300.0)+ (i%7)*0.00071; v.push_back(fn.f(x));}
      res[mode].push_back(std::move(v));}
  }
  for(size_t k=0;k<fns.size();k++){int diff=0;for(int i=0;i<N;i++) if(res[0][k][i]!=res[1][k][i]) diff++;
    std::printf("%-8s differ %d / %d\n",fns[k].name,diff,N);}
}
