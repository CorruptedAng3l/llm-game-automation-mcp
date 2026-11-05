#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
namespace vi_motion {
inline bool Valid(const std::vector<std::uint8_t>& pixels,unsigned w,unsigned h){return w&&h&&w<=8192&&h<=8192&&(std::uint64_t)w*h*4==pixels.size();}
struct Difference {bool valid=false;double changedPct=0,meanPct=0;};
inline Difference Compare(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b,unsigned w,unsigned h,unsigned tolerance=8){
 Difference out;if(tolerance>255||!Valid(a,w,h)||!Valid(b,w,h))return out;
 std::uint64_t changed=0,sum=0;for(size_t i=0;i<a.size();i+=4){unsigned largest=0;for(unsigned c=0;c<3;++c){unsigned d=a[i+c]>b[i+c]?a[i+c]-b[i+c]:b[i+c]-a[i+c];sum+=d;largest=(std::max)(largest,d);}changed+=largest>tolerance;}
 out.valid=true;out.changedPct=100.0*(double)changed/((std::uint64_t)w*h);out.meanPct=100.0*(double)sum/(255.0*3*w*h);return out;
}
inline double BlackPct(const std::vector<std::uint8_t>& pixels,unsigned w,unsigned h,unsigned tolerance=8){
 if(!Valid(pixels,w,h)||tolerance>255)return -1;std::uint64_t black=0;
 for(size_t i=0;i<pixels.size();i+=4)black+=pixels[i]<=tolerance&&pixels[i+1]<=tolerance&&pixels[i+2]<=tolerance;
 return 100.0*(double)black/((std::uint64_t)w*h);
}
// Area-averaged RGB cells, deliberately coarse; not optical flow or object motion.
inline std::string Sample(const std::vector<std::uint8_t>& pixels,unsigned w,unsigned h,unsigned& sw,unsigned& sh){
 sw=sh=0;if(!Valid(pixels,w,h))return {};sw=(std::min)(16u,w);sh=(std::min)(16u,h);std::string hex;hex.reserve(sw*sh*6);const char* digits="0123456789ABCDEF";
 for(unsigned gy=0;gy<sh;++gy)for(unsigned gx=0;gx<sw;++gx){
  const unsigned x0=gx*w/sw,x1=(gx+1)*w/sw,y0=gy*h/sh,y1=(gy+1)*h/sh;std::uint64_t sums[3]={};
  for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x){size_t at=((size_t)y*w+x)*4;for(unsigned c=0;c<3;++c)sums[c]+=pixels[at+2-c];}
  const auto n=(std::uint64_t)(x1-x0)*(y1-y0);for(auto sum:sums){const auto value=(unsigned)(sum/n);hex+=digits[value>>4];hex+=digits[value&15];}
 }return hex;
}
}
