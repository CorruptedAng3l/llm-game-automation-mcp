// Memory-only actual Windows OCR and extracted production CmdOcr controls.
// No HWND, screen DC, injected process, game, OS input or foreground writer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <vi_ocr_numbers.h>
#include <vi_ocr.h>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <functional>
#pragma comment(lib,"gdi32.lib")
using Args=std::map<std::string,std::string>;
static bool HasOpt(const Args& a,const std::string& key){return a.find(key)!=a.end();}
static std::string GetOpt(const Args& a,const std::string& key,const std::string& fallback=""){auto it=a.find(key);return it==a.end()?fallback:it->second;}
static std::string JEsc(const std::string& text){std::string out;for(unsigned char c:text){if(c=='"'||c=='\\'){out+='\\';out+=(char)c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32)out+='?';else out+=(char)c;}return out;}
static std::wstring U2W(const std::string& text){if(text.empty())return {};const int count=MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0);std::wstring out((size_t)count,L'\0');MultiByteToWideChar(CP_UTF8,0,text.data(),(int)text.size(),out.data(),count);return out;}
static std::string W2U(const std::wstring& text){if(text.empty())return {};const int count=WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),nullptr,0,nullptr,nullptr);std::string out((size_t)count,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),(int)text.size(),out.data(),count,nullptr,nullptr);return out;}
struct Reply{bool ok;std::string text;};
static Reply Fail(const std::string& error){return {false,"{\"ok\":false,\"error\":\""+JEsc(error)+"\"}"};}
#include "actual_ocr_number_frame.inl"
static Frame sourceFrame;static unsigned captures=0;
static bool Grab(DWORD,const Args&,Frame& frame,std::string&,bool preserve){++captures;if(!preserve)return false;frame=sourceFrame;return true;}
static bool OcrTiles(const Frame&,double,const Args&,std::vector<vi_text::Line>&,std::string&,std::string&,const std::string&){return false;}
static std::string ObservationImageFields(const Frame&,const Args&){return {};}
#include "actual_ocr_number_command.inl"
static unsigned passed=0,failed=0;
static void Check(bool good,const char* name){printf("%s %s\n",good?"PASS":"FAIL",name);good?++passed:++failed;}
static vi_text::Line Line(const wchar_t* text,vi_text::Box box={11,7,50,14}){vi_text::Line line;line.text=text;line.box=box;line.words.push_back({text,box});return line;}
static vi_ocr_numbers::Result ReadTexts(const std::vector<std::wstring>& texts,int failAt=-1){size_t at=0;const auto plan=vi_ocr_numbers::Plan(1,false,"none",true);return vi_ocr_numbers::Read(plan,128,64,[&](const auto&,auto& lines,auto& lang,auto& error){const auto i=at++;lang="en-US";if((int)i==failAt){error="controlled engine error";return false;}if(i<texts.size()&&!texts[i].empty())lines={Line(texts[i].c_str())};return true;});}
static void Pure(){
 using namespace vi_ocr_numbers;
 Check(Plan(1,false,"none",true).size()==4&&Plan(1,false,"none",false).size()==1,"digits pass plan is bounded and retry opt-out stays one pass");
 const auto explicitPlan=Plan(3,true,"contrast",true);Check(explicitPlan.size()==2&&std::all_of(explicitPlan.begin(),explicitPlan.end(),[](const auto& p){return p.scale==3;}),"explicit scale is respected and duplicate planned passes removed");
 unsigned duplicateCalls=0;const auto duplicate=Read(std::vector<Pass>{{1,"none"},{1,"none"}},128,64,[&](const auto&,auto&,auto&,auto&){++duplicateCalls;return true;});Check(!duplicate.ok&&!duplicate.complete&&duplicate.reason=="duplicate_plan"&&duplicateCalls==0,"identical replayed passes cannot satisfy distinct-pass agreement");
 for(const auto* text:{L"0",L"1",L"9",L"123456",L"0012"}){const auto o=Evaluate({1,"none"},true,{Line(text)},"en-US","",128,64);Check(o.value==text,"whole ASCII digit strings retain exact value including leading zeros");}
 for(const auto* text:{L"UBS",L"O",L"I",L"l",L"B",L"S",L"1B",L"1 2",L"1,2",L"-1",L"1.2",L"1234567",L"\x0661"}){const auto o=Evaluate({1,"none"},true,{Line(text)},"en-US","",128,64);Check(o.value.empty(),"nonnumeric/split/oversize tokens are rejected without substitutions");}
 auto r=ReadTexts({L"UBS",L"5",L"5",L"5"});Check(r.ok&&r.complete&&r.value==L"5"&&r.agreement==3&&r.observations.size()==4&&r.observations[0].text==L"UBS","nonnumeric nonempty initial text is retryable and retained as literal evidence");
 r=ReadTexts({L"5",L"5",L"3",L"5"});Check(r.value.empty()&&r.status=="ambiguous"&&r.reason=="numeric_disagreement","minority numeric disagreement cannot be hidden by majority agreement");
 r=ReadTexts({L"",L"5",L"",L""});Check(r.value.empty()&&r.reason=="insufficient_agreement","one eligible observation remains unknown");
 r=ReadTexts({L"5",L"5",L"",L"5"},2);Check(r.ok&&!r.complete&&r.value.empty()&&r.reason=="incomplete_engine_analysis"&&!r.observations[2].error.empty(),"failed planned pass preserves error and prevents a known value");
 r=ReadTexts({},0);Check(!r.ok&&!r.complete&&r.observations.size()==1&&r.error=="controlled engine error","initial engine failure remains a failure");
 r=ReadTexts({L"UBS",L"UBS",L"UBS",L"UBS"});Check(r.ok&&r.complete&&r.value.empty()&&r.reason=="no_numeric_text","letters-only analysis is unknown, never fabricated digits");
 auto two=Line(L"1 2");two.words={ {L"1",{10,10,5,10}},{L"2",{20,10,5,10}} };
 Check(Evaluate({1,"none"},true,{two},"","",128,64).value.empty(),"separate OCR words are never concatenated into a number");
 Check(Evaluate({1,"none"},true,{Line(L"1"),Line(L"2")},"","",128,64).value.empty(),"multiple OCR lines stay unknown");
 for(const auto box:{vi_text::Box{0,5,10,10},vi_text::Box{5,0,10,10},vi_text::Box{118,5,10,10},vi_text::Box{5,54,10,10}})Check(Evaluate({1,"none"},true,{Line(L"1",box)},"","",128,64).reason=="clipped","source-edge glyph boxes are rejected as potentially clipped");
 vi_vision::Image image;image.w=4;image.h=1;image.pixels={200,200,200,255,10,210,240,255,170,210,190,255,169,169,169,255};const auto original=image.pixels;vi_vision::Image mask;std::string error;
 Check(vi_ocr::Preprocess(image,"white_glyph",mask,error)&&mask.w==4&&mask.h==1&&mask.pixels[0]==0&&mask.pixels[4]==255&&mask.pixels[8]==0&&mask.pixels[12]==255&&image.pixels==original,"actual white-glyph preprocessing thresholds, source grid and immutability are exact");
 Frame frame;frame.ox=100;frame.oy=200;frame.sx=1.5;frame.sy=2.5;
 r=ReadTexts({L"7",L"7",L"7",L"7"});const auto json=NumberReadJson(frame,r);
 Check(json.find("\"box_client\":[117,218,75,35]")!=std::string::npos&&json.find("\"value\":\"7\"")!=std::string::npos,"actual production number serializer applies nonzero origin and nonuniform client mapping");
 captures=0;Check(!CmdOcr(1,{{"digits-only","true"}}).ok&&captures==0,"actual CmdOcr refuses missing digits region before capture");
 Check(!CmdOcr(1,{{"digits-only","true"},{"region","0,0,513,24"}}).ok&&captures==0,"actual CmdOcr refuses oversize digits region before capture");
 Check(!CmdOcr(1,{{"digits-only","true"},{"region","0,0,24,24"},{"tiles","true"}}).ok&&captures==0,"actual CmdOcr refuses tiled digits mode before capture");
 Check(!CmdOcr(1,{{"digits-only","maybe"}}).ok&&captures==0,"actual CmdOcr refuses malformed digits boolean before capture");
}
static vi_vision::Image Render(const wchar_t* text,COLORREF color,int fontPx=40){
 const int width=128,height=96;BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=width;bi.bmiHeader.biHeight=-height;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
 void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);vi_vision::Image image;
 if(!dc||!bitmap||!bits){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return image;}
 const auto old=SelectObject(dc,bitmap);RECT rectangle{0,0,width,height};HBRUSH background=CreateSolidBrush(RGB(35,35,35));FillRect(dc,&rectangle,background);DeleteObject(background);
 HBRUSH circle=CreateSolidBrush(color);const auto oldBrush=SelectObject(dc,circle);const auto oldPen=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,3,3,width-3,height-3);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(circle);
 HFONT font=CreateFontW(-fontPx,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");const auto oldFont=SelectObject(dc,font);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,text,-1,&rectangle,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
 image.w=width;image.h=height;const auto* pixels=static_cast<const uint8_t*>(bits);image.pixels.assign(pixels,pixels+(size_t)width*height*4);for(size_t p=3;p<image.pixels.size();p+=4)image.pixels[p]=255;
 SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return image;
}
static vi_ocr_numbers::Result ReadImage(const vi_vision::Image& image){return vi_ocr_numbers::Read(vi_ocr_numbers::Plan(1,false,"none",true),image.w,image.h,[&](const auto& pass,auto& lines,auto& language,auto& error){vi_vision::Image prepared;if(!vi_ocr::Preprocess(image,pass.preprocessing,prepared,error))return false;return vi_ocr::Recognize(prepared,pass.scale,"en-US",lines,language,error);});}
static void Report(const char* group,const char* id,const std::wstring& expected,const vi_ocr_numbers::Result& r){printf("OBSERVATION {\"group\":\"%s\",\"id\":\"%s\",\"expected\":\"%s\",\"number_read\":%s}\n",group,id,JEsc(W2U(expected)).c_str(),NumberReadJson(Frame{},r).c_str());}
static void Real(const std::string& data){
 unsigned observed=0,unknown=0,wrong=0;bool complete=true;
 for(const auto* text:{L"0",L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"12",L"345"}){
  const auto image=Render(text,RGB(135,75,175));const auto original=image.pixels;const auto r=ReadImage(image);Report("memory_circle",W2U(text).c_str(),text,r);
  if(r.value.empty())++unknown;else if(r.value==text)++observed;else ++wrong;complete=complete&&r.ok&&r.complete&&image.pixels==original;
 }
 for(const auto color:{RGB(230,190,35),RGB(12,12,12)}){const auto r=ReadImage(Render(L"123",color));Report("memory_color","123",L"123",r);Check(r.ok&&r.complete&&r.value==L"123","actual WinRT exact multidigit positive on yellow/black circle");}
 printf("MEASURED memory_circle observed=%u unknown=%u incorrect=%u total=12; agreement is not confidence\n",observed,unknown,wrong);
 Check(complete&&observed>0&&wrong==0,"actual WinRT circles include exact numeric positives with no observed incorrect values; unknowns counted separately");
 for(const auto* text:{L"UBS",L"O",L"I",L"B",L"S",L"l",L"",L"*"}){const auto r=ReadImage(Render(text,RGB(135,75,175)));Report("memory_reject",W2U(text).c_str(),L"",r);Check(r.ok&&r.complete&&r.value.empty(),"actual WinRT nonnumeric/blank/icon region remains unknown");}
 sourceFrame={};sourceFrame.img=Render(L"123",RGB(135,75,175));sourceFrame.ox=100;sourceFrame.oy=200;sourceFrame.sx=1.5;sourceFrame.sy=2.5;sourceFrame.clientW=1000;sourceFrame.clientH=800;sourceFrame.captureScope="{\"id\":\"memory-only-frame\"}";sourceFrame.qpcFrequency="10000000";sourceFrame.captureQpc="18446744073709551614";sourceFrame.frameQpc="18446744073709551600";sourceFrame.nativeFrame=77;
 const auto before=sourceFrame.img.pixels;captures=0;auto reply=CmdOcr(1,{{"digits-only","true"},{"region","100,200,192,240"},{"lang","en-US"},{"text-only","true"}});
 printf("COMMAND %s\n",reply.text.c_str());
 Check(reply.ok&&captures==1&&sourceFrame.img.pixels==before&&reply.text.find("\"number_read\":{\"version\":1,\"status\":\"numeric_observed\",\"value\":\"123\"")!=std::string::npos,"actual CmdOcr WinRT all-pass numeric result uses exactly one immutable captured frame");
 Check(reply.text.find("\"ocr_confidence\":null")!=std::string::npos&&reply.text.find("18446744073709551614")!=std::string::npos&&reply.text.find("memory-only-frame")!=std::string::npos,"actual compact CmdOcr preserves confidence null and exact same-frame timing/scope");
 captures=0;reply=CmdOcr(1,{{"lang","en-US"}});Check(reply.ok&&captures==1&&reply.text.find("\"text\":\"123\"")!=std::string::npos&&reply.text.find("\"ocr_passes\":1")!=std::string::npos&&reply.text.find("number_read")==std::string::npos,"ordinary actual CmdOcr retains first-nonempty behavior and rich output without numeric semantics");
 if(data.empty())return;
 struct Case{const char* id;const wchar_t* expected;};const Case cases[]={{"purple_one",L"1"},{"yellow_one",L"1"},{"black_five",L"5"},{"purple_three",L"3"},{"black_one",L"1"},{"black_three",L"3"},{"yellow_four",L"4"},{"yellow_two",L"2"}};
 observed=unknown=wrong=0;complete=true;
 for(const auto& item:cases){vi_vision::Image image;std::string error;const bool loaded=vi_vision::LoadPng(U2W(data+"/"+item.id+".png"),image,error);if(!loaded){Check(false,"pinned static Risk crop loads");continue;}const auto r=ReadImage(image);Report("risk_png_crop",item.id,item.expected,r);if(r.value.empty())++unknown;else if(r.value==item.expected)++observed;else ++wrong;complete=complete&&r.ok&&r.complete;}
 printf("MEASURED risk_png_crop observed=%u unknown=%u incorrect=%u total=8; source PNG coordinates only, no native-client mapping claim\n",observed,unknown,wrong);
 Check(complete&&wrong==0,"pinned Risk crops report exact observed/unknown/incorrect counts without fabricated readings");
}
int main(int argc,char** argv){Pure();if(argc>1&&std::string(argv[1])=="--real")Real(argc>2?argv[2]:"");printf("ocr numbers: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;}
