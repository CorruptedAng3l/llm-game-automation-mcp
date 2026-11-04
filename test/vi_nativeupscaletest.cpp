// Actual image helpers + exact-source validation/map excerpts. No target/window/input API.
#include "vi_image_encode.h"
#include <map>
#include <climits>
#include <cstdlib>
static int checks=0,failures=0;
static void Check(bool ok,const char* label){++checks;std::printf("%s %s\n",ok?"PASS":"FAIL",label);if(!ok)++failures;}
struct JV{int t=0;std::string s;double n=0;bool b=false;};using JObj=std::map<std::string,JV>;
static bool JHas(const JObj& j,const char* key){return j.find(key)!=j.end();}
static std::string JStr(const JObj& j,const char* key,const char* fallback=""){auto it=j.find(key);return it!=j.end()&&it->second.t==1?it->second.s:fallback;}
static bool JBool(const JObj& j,const char* key,bool fallback=false){auto it=j.find(key);return it!=j.end()&&it->second.t==3?it->second.b:fallback;}
static std::string ErrJ(const std::string& error){return "error:"+error;}
static LONG g_vcurX=0,g_vcurY=0;static HWND g_hwnd=(HWND)(ULONG_PTR)1;
struct Map{double w=1,h=1,ox=0,oy=0,cw=64,ch=48;};static Map g_savedCaptureMap;static bool mapCurrent=true;
static bool CaptureMapCurrent(){return mapCurrent;}
static BOOL MemoryGetClientRect(HWND h,RECT* rc){if(h!=g_hwnd||!rc)return FALSE;*rc=RECT{0,0,64,48};return TRUE;}
// The only window-reader call in the exact ResolveXY excerpt is memory-substituted.
#define GetClientRect MemoryGetClientRect
#include "upscale_source_extract.inl"
#undef GetClientRect
static JObj XY(double x,double y){JObj j;j["x"]=JV{2,{},x,false};j["y"]=JV{2,{},y,false};j["space"]=JV{1,"capture",0,false};return j;}
static bool Gate(double n){JObj j;j["scale"]=JV{2,{},n,false};return ExtractedCaptureScaleGate(j)=="accepted";}
int main(){
 std::string error;int w=0,h=0;
 Check(Gate(1)&&Gate(0.05),"actual DoCapture excerpt retains ordinary/downscale bounds");
 Check(Gate(2)&&Gate(4),"actual DoCapture excerpt admits 2x/4x before image I/O");
 JObj bad;bad["scale"]=JV{1,"2",0,false};Check(ExtractedCaptureScaleGate(bad)!="accepted","actual DoCapture excerpt rejects string scale");
 Check(!Gate(0.049)&&!Gate(4.001)&&!Gate((std::numeric_limits<double>::infinity)())&&!Gate((std::numeric_limits<double>::quiet_NaN)()),"actual DoCapture excerpt rejects nonfinite/out-of-range scale");
 Check(vi_image::OutputSize(17,11,0.05,0,w,h,error)&&w==1&&h==1,"small downscale retains minimum output dimensions");
 Check(vi_image::OutputSize(640,480,1,8192,w,h,error)&&w==640&&h==480,"max-width remains shrink-only");
 Check(vi_image::OutputSize(2048,512,4,0,w,h,error)&&w==8192&&h==2048,"4x exact 8192-edge/16Mi-pixel boundary accepted without allocation");
 Check(!vi_image::OutputSize(2049,512,4,0,w,h,error),"upscale beyond edge cap rejected");
 Check(!vi_image::OutputSize(2048,513,4,0,w,h,error),"upscale beyond pixel cap rejected");
 Check(!vi_image::OutputSize(INT_MAX,1,4,0,w,h,error),"oversized rounded doubles rejected before narrowing");
 Check(!vi_image::OutputSize(10,10,4.001,0,w,h,error)&&!vi_image::OutputSize(0,10,2,0,w,h,error),"helper rejects invalid scale/source geometry");
 vi_vision::Image source;source.w=128;source.h=96;source.pixels.resize(128*96*4);
 for(int y=0;y<source.h;y++)for(int x=0;x<source.w;x++){auto* p=&source.pixels[((size_t)y*source.w+x)*4];const bool yellow=((x/32+y/32)%2)==1;p[0]=yellow?20:150;p[1]=yellow?180:80;p[2]=yellow?230:30;p[3]=yellow?0:255;}
 const auto sourceBefore=source.pixels;
 // Known client64x48 renders128x96: client ROI20,10,12,8 covers source40,20,24,16.
 vi_vision::Image crop;const bool cropped=vi_vision::Crop(source,{40,20,24,16},crop,error);const auto cropBefore=crop.pixels;
 Check(cropped&&crop.w==24&&crop.h==16,"actual crop uses independently known source bounds for a client-space region");
 int ow=0,oh=0;const bool sized=cropped&&vi_image::OutputSize(crop.w,crop.h,2,0,ow,oh,error);
 Check(sized&&ow==48&&oh==32,"actual cropped source upscales to dimensions distinct from client ROI times2");
 vi_vision::Image scaled;const bool resized=sized&&vi_image::Resize(crop,ow,oh,scaled,error);
 Check(resized&&scaled.w==48&&scaled.h==32,"actual WIC resize accepts bounded 2x crop");
 vi_vision::Pixel top,bottom;
 Check(resized&&vi_vision::ReadPixel(scaled,16,8,top,error)&&top.r==230&&top.g==180&&top.b==20&&top.a==0,"upscaled interior retains known yellow RGB with alpha0");
 Check(resized&&vi_vision::ReadPixel(scaled,32,28,bottom,error)&&bottom.r==30&&bottom.g==80&&bottom.b==150&&bottom.a==255,"upscaled interior retains known blue RGB and alpha255");
 Check(source.pixels==sourceBefore&&crop.pixels==cropBefore,"source-region bytes used by pre-transform hash remain immutable");
 g_savedCaptureMap=Map{24,16,20,10,12,8};LONG x=0,y=0;
 Check(ResolveXY(XY(8,4),x,y)&&x==24&&y==12,"actual ResolveXY excerpt retains existing 1x crop-map conversion");
 g_savedCaptureMap=Map{(double)ow,(double)oh,20,10,12,8};
 Check(sized&&ResolveXY(XY(16,8),x,y)&&x==24&&y==12,"actual ResolveXY uses enlarged output dimensions for the same client point");
 Check(sized&&ResolveXY(XY(32,28),x,y)&&x==28&&y==17,"actual ResolveXY maps second upscaled image point into pre-scale client coordinates");
 mapCurrent=false;Check(!ResolveXY(XY(16,8),x,y),"stale map remains refused");JObj norm=XY(0.5,0.5);norm["norm"]=JV{3,{},0,true};
 Check(ResolveXY(norm,x,y)&&x==32&&y==24,"existing norm precedence remains independent of saved capture map");mapCurrent=true;
 vi_vision::Image flat;flat.w=17;flat.h=11;flat.pixels.resize(17*11*4);for(size_t i=0;i<flat.pixels.size();i+=4){flat.pixels[i]=33;flat.pixels[i+1]=77;flat.pixels[i+2]=155;flat.pixels[i+3]=0;}
 const bool size4=vi_image::OutputSize(17,11,4,0,w,h,error);vi_vision::Image four;const bool resize4=size4&&vi_image::Resize(flat,w,h,four,error);bool exact=resize4&&four.w==68&&four.h==44;
 if(exact)for(size_t i=0;i<four.pixels.size();i+=4)if(four.pixels[i]!=33||four.pixels[i+1]!=77||four.pixels[i+2]!=155||four.pixels[i+3]!=0){exact=false;break;}
 Check(exact,"actual 4x WIC pixels preserve a uniform visible color despite alpha0");
 Check(!vi_image::Resize(flat,8192,8192,four,error),"actual resize enforces pixel cap before allocating oversized output");
 std::printf("checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
