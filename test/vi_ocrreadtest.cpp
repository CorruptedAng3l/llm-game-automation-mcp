#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <vi_ocr_read.h>
#include <vi_ocr.h>
#include <cstdio>
#pragma comment(lib,"gdi32.lib")
static unsigned passed=0,failed=0;
static void Check(bool good,const char* what){printf("%s %s\n",good?"PASS":"FAIL",what);good?++passed:++failed;}
static vi_text::Line Line(const wchar_t* text){vi_text::Line l;l.text=text;l.box={11,7,50,14};l.words.push_back({text,l.box});return l;}
static vi_vision::Image Render(int fontPx,COLORREF bg,COLORREF fg,const wchar_t* text){
    const int w=300,h=70;BITMAPINFO bi{};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=w;bi.bmiHeader.biHeight=-h;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;bi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HDC dc=CreateCompatibleDC(nullptr);HBITMAP dib=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&bits,nullptr,0);vi_vision::Image out;
    if(!dc||!dib||!bits){if(dib)DeleteObject(dib);if(dc)DeleteDC(dc);return out;}
    auto old=SelectObject(dc,dib);RECT r{0,0,w,h};HBRUSH brush=CreateSolidBrush(bg);FillRect(dc,&r,brush);DeleteObject(brush);
    HFONT font=CreateFontW(-fontPx,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);
    SetTextColor(dc,fg);SetBkMode(dc,TRANSPARENT);r.left=10;r.top=8;DrawTextW(dc,text,-1,&r,DT_LEFT|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
    out.w=w;out.h=h;const auto* pixels=static_cast<const uint8_t*>(bits);out.pixels.assign(pixels,pixels+(size_t)w*h*4);
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteObject(dib);DeleteDC(dc);return out;
}
static vi_ocr_read::Result ReadImage(const vi_vision::Image& image,bool retry){
    return vi_ocr_read::Read(vi_ocr_read::Plan(1,false,"none",false,retry),[&](const vi_ocr_read::Pass& pass,std::vector<vi_text::Line>& lines,std::string& lang,std::string& error){
        vi_vision::Image prepared;if(!vi_ocr::Preprocess(image,pass.preprocessing,prepared,error))return false;
        return vi_ocr::Recognize(prepared,pass.scale,"en-US",lines,lang,error);
    });
}
static int Real(){
    const auto positive=ReadImage(Render(32,RGB(255,255,255),RGB(0,0,0),L"Read Me"),true);
    Check(positive.ok&&!vi_text::Find(positive.lines,L"Read Me",{}).empty()&&positive.attempts.size()==1,"memory-only WinRT positive control reads known Read Me text without retry");
    if(!positive.ok){printf("OCR error: %s\n",positive.error.c_str());return 1;}
    unsigned recovered=0,misses=0;
    for(int fontPx:{6,7,8,9,10,12}){
        const auto image=Render(fontPx,RGB(238,151,45),RGB(118,87,57),L"Log OK");
        const auto before=ReadImage(image,false),after=ReadImage(image,true);
        std::wstring raw,read;for(auto& l:before.lines)raw+=l.text;for(auto& l:after.lines)read+=l.text;
        const bool recoveredThis=before.ok&&!vi_ocr_read::HasText(before.lines)&&after.ok&&vi_ocr_read::HasText(after.lines);
        if(recoveredThis)++recovered;if(!vi_ocr_read::HasText(after.lines))++misses;
        printf("MEASURED font_px=%d before_lines=%zu after_lines=%zu passes=%zu selected_scale=%.0f selected_mode=%s recovered=%d before=",fontPx,before.lines.size(),after.lines.size(),after.attempts.size(),after.attempts[after.selected].pass.scale,after.attempts[after.selected].pass.preprocessing.c_str(),recoveredThis?1:0);
        for(wchar_t c:raw)printf("%lc",c);printf(" after=");for(wchar_t c:read)printf("%lc",c);printf("\n");
        bool boxes=true;for(auto& l:after.lines)boxes=boxes&&l.box.x>=0&&l.box.y>=0&&l.box.x+l.box.w<=image.w&&l.box.y+l.box.h<=image.h;
        Check(after.ok&&after.attempts.size()<=4&&boxes,"WinRT retry remains bounded with source-pixel boxes");
    }
    printf("MEASURED recovered=%u still_empty=%u of 6; nonempty recognition is not proof of correct text\n",recovered,misses);
    printf("ocr real: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--real")return Real();
    using namespace vi_ocr_read;const auto defaults=Plan(1,false,"none",false,true);
    Check(defaults.size()==4&&defaults[0].scale==1&&defaults[1].scale==2&&defaults[2].preprocessing=="contrast"&&defaults[3].scale==3,"default plan has at most four same-image passes");
    Check(Plan(1,false,"none",false,false).size()==1,"retry opt-out is one requested pass");
    const auto explicitScale=Plan(4,true,"none",false,true);Check(explicitScale.size()==2&&explicitScale[0].scale==4&&explicitScale[1].scale==4,"explicit scale never silently changes");
    const auto explicitMode=Plan(1,false,"threshold",true,true);Check(explicitMode.size()==3&&explicitMode[1].preprocessing=="threshold"&&explicitMode[2].preprocessing=="threshold","explicit preprocessing is retained and duplicate passes removed");
    Check(Plan(3,true,"contrast",true,true).size()==1,"fully explicit repeated pass is not run twice");
    auto r=Read(defaults,[](const Pass&,auto& lines,auto& lang,auto&){lines={Line(L"Log")};lang="en-US";return true;});
    Check(r.ok&&r.attempts.size()==1&&r.lines[0].text==L"Log","nonempty initial OCR is never retried or merged");
    r=Read(defaults,[](const Pass& pass,auto& lines,auto& lang,auto&){if(pass.scale==2)lines={Line(L"Log")};lang="en-US";return true;});
    Check(r.ok&&r.attempts.size()==2&&r.selected==1&&r.language=="en-US","empty OCR retries upscale and stops on text");
    r=Read(defaults,[](const Pass& pass,auto& lines,auto&,auto&){if(pass.preprocessing=="contrast")lines={Line(L"OK")};return true;});
    Check(r.ok&&r.attempts.size()==3&&r.attempts[r.selected].pass.preprocessing=="contrast","empty low-contrast result reaches contrast pass");
    Check(r.lines[0].box.x==11&&r.lines[0].box.y==7&&r.lines[0].box.w==50,"pipeline preserves original capture coordinate boxes");
    r=Read(defaults,[](const Pass&,auto&,auto&,auto&){return true;});Check(r.ok&&r.attempts.size()==4&&r.lines.empty(),"all-empty result is bounded and stays empty");
    unsigned call=0;r=Read(defaults,[&](const Pass&,auto& lines,auto&,auto&){lines={Line(++call==1?L"  \t":L"OK")};return true;});Check(r.attempts.size()==2,"whitespace-only lines count as empty");
    r=Read(defaults,[](const Pass&,auto&,auto&,auto& error){error="no OCR language installed";return false;});Check(!r.ok&&r.attempts.size()==1&&r.error=="no OCR language installed","initial engine error is preserved and never retried");
    call=0;r=Read(defaults,[&](const Pass&,auto& lines,auto&,auto& error){++call;if(call==2){error="image exceeds engine limit";return false;}if(call==3)lines={Line(L"OK")};return true;});Check(r.ok&&r.attempts.size()==3&&!r.attempts[1].ok&&!r.attempts[1].error.empty()&&r.selected==2,"retry failure evidence survives subsequent recovery");
    call=0;r=Read(defaults,[&](const Pass&,auto&,auto&,auto& error){if(++call>1){error="retry failed";return false;}return true;});Check(r.ok&&r.selected==0&&r.lines.empty()&&r.attempts.size()==4,"optional failures do not erase valid original empty result");
    vi_vision::Image image;image.w=2;image.h=1;image.pixels={45,151,238,255,57,87,118,255};auto original=image.pixels;vi_vision::Image contrast;std::string error;
    Check(vi_ocr::Preprocess(image,"contrast",contrast,error)&&contrast.w==2&&contrast.h==1&&contrast.pixels[0]==255&&contrast.pixels[4]==0&&image.pixels==original,"actual dark-on-orange contrast preserves source grid and stretches dynamic range");
    const auto enlarged=vi_ocr::Upscale(contrast,3);Check(enlarged.w==6&&enlarged.h==3&&image.pixels==original,"actual upscale preserves immutable source image");
    printf("ocr read: %u PASS / %u FAIL\n",passed,failed);return failed?1:0;
}
