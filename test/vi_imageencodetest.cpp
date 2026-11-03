#include "../src/vi_image_encode.h"
#include <cstdio>
static int failures=0,checks=0;
static void Check(bool value,const char* label){++checks;printf("%s %s\n",value?"PASS":"FAIL",label);if(!value)++failures;}
static bool LoadJpeg(const std::wstring& path,vi_vision::Image& image,std::string& error){
    vi_vision::detail::ComScope com;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    GUID container{};if(SUCCEEDED(hr))hr=decoder->GetContainerFormat(&container);
    if(SUCCEEDED(hr)&&!IsEqualGUID(container,GUID_ContainerFormatJpeg)){error="not JPEG";return false;}
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
    UINT w=0,h=0;if(SUCCEEDED(hr))hr=frame->GetSize(&w,&h);
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
    if(SUCCEEDED(hr))hr=converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    if(SUCCEEDED(hr)){image.w=w;image.h=h;image.pixels.resize((size_t)w*h*4);hr=converter->CopyPixels(nullptr,w*4,(UINT)image.pixels.size(),image.pixels.data());}
    return !vi_vision::detail::Failure(hr,"test JPEG decode",error);
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;std::wstring folder=argv[1];std::string error;
    vi_vision::Image source;source.w=64;source.h=32;source.pixels.resize(64*32*4);
    for(size_t i=0;i<source.pixels.size();i+=4){source.pixels[i]=33;source.pixels[i+1]=77;source.pixels[i+2]=155;source.pixels[i+3]=0;}
    vi_vision::Image resized;
    Check(vi_image::Resize(source,32,16,resized,error),"actual WIC downsample succeeds");
    Check(!resized.pixels.empty()&&resized.pixels[0]==33&&resized.pixels[1]==77&&resized.pixels[2]==155,"downsample retains visible RGB even with zero swapchain alpha");
    std::wstring png=folder+L"\\roundtrip.png",jpeg=folder+L"\\roundtrip.jpg";
    Check(vi_image::Save(png,resized,"png",85,error),"PNG encode succeeds");vi_vision::Image pngImage;
    Check(vi_vision::LoadPng(png,pngImage,error)&&pngImage.w==32&&pngImage.h==16&&pngImage.pixels==resized.pixels,"PNG decode matches exact transformed pixels and alpha");
    Check(vi_image::Save(jpeg,resized,"jpeg",85,error),"JPEG encode succeeds");vi_vision::Image jpegImage;
    Check(LoadJpeg(jpeg,jpegImage,error)&&jpegImage.w==32&&jpegImage.h==16,"independent JPEG decoder confirms container and dimensions");
    Check(!jpegImage.pixels.empty()&&abs((int)jpegImage.pixels[0]-33)<=4&&abs((int)jpegImage.pixels[1]-77)<=4&&abs((int)jpegImage.pixels[2]-155)<=4,"JPEG lossy RGB stays in tolerance despite source alpha0");
    Check(!vi_vision::LoadPng(jpeg,pngImage,error),"PNG-only template loader still rejects JPEG");
    for(int y=0;y<source.h;++y)for(int x=0;x<source.w;++x)source.pixels[((size_t)y*source.w+x)*4+3]=x<32?0:255;
    vi_vision::Image mixed;
    Check(vi_image::Resize(source,32,16,mixed,error),"mixed-alpha WIC resize succeeds");
    Check(!mixed.pixels.empty()&&mixed.pixels[0]==33&&mixed.pixels[3]==0&&mixed.pixels[(32-1)*4+3]==255,"mixed alpha is scaled separately without blackening RGB");
    Check(vi_image::Save(folder+L"\\mixed.png",mixed,"png",85,error)&&vi_vision::LoadPng(folder+L"\\mixed.png",pngImage,error)&&mixed.pixels==pngImage.pixels,"mixed-alpha PNG roundtrip is lossless");
    Check(!vi_image::Save(folder+L"\\bad.bmp",source,"bmp",85,error),"unsupported format fails");
    Check(!vi_image::Dimensions(8192,8192,error)&&vi_image::Dimensions(8192,2048,error),"output pixel bound checked without huge allocations");
    int w=0,h=0;Check(!vi_image::OutputSize(800,450,0,0,w,h,error),"zero scale rejected");
    Check(vi_image::OutputSize(800,450,1,320,w,h,error)&&w==320&&h==180,"max width preserves expected aspect");
    printf("checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
