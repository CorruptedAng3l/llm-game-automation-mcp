// Bounded output transforms for optional scaled captures and contact sheets.
#pragma once
#include "vi_vision.h"
#include "vi_paths.h"
namespace vi_image {
inline bool Dimensions(int w, int h, std::string& error) {
    if (w < 1 || h < 1 || w > 8192 || h > 8192 || (std::uint64_t)w*h > 16u*1024u*1024u) {
        error="output image must be 1..8192 per edge and at most 16 million pixels";return false;
    }
    return true;
}
inline bool OutputSize(int w,int h,double scale,int maxWidth,int& ow,int& oh,std::string& error) {
    if(w<1||h<1){error="source image dimensions must be positive";return false;}
    if(!std::isfinite(scale)||scale<0.05||scale>4||maxWidth<0||maxWidth>8192){error="capture scale must be 0.05..4 and max-width 1..8192";return false;}
    double factor=maxWidth?(std::min)(1.0,(double)maxWidth/w):scale;
    // Bound rounded doubles before narrowing: upscaling a large source int must
    // not overflow lround/int before the unchanged edge/pixel limits run.
    const double width=(std::max)(1.0,std::round(w*factor)),height=(std::max)(1.0,std::round(h*factor));
    if(width>8192||height>8192){error="output image must be 1..8192 per edge and at most 16 million pixels";return false;}
    ow=(int)width;oh=(int)height;
    return Dimensions(ow,oh,error);
}
inline bool Resize(const vi_vision::Image& source,int w,int h,vi_vision::Image& output,std::string& error) {
    if(!vi_vision::Validate(source,error)||!Dimensions(w,h,error))return false;
    vi_vision::detail::ComScope com;
    if(FAILED(com.result)&&com.result!=RPC_E_CHANGED_MODE)return !vi_vision::detail::Failure(com.result,"COM",error);
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICBitmap> bitmap;Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    // WIC's BGRA scaler premultiplies alpha, losing visible RGB when a swapchain
    // stores alpha0. Scale RGB as BGRX, then preserve/filter alpha independently.
    if(SUCCEEDED(hr))hr=factory->CreateBitmapFromMemory(source.w,source.h,GUID_WICPixelFormat32bppBGR,source.w*4,(UINT)source.pixels.size(),const_cast<BYTE*>(source.pixels.data()),&bitmap);
    if(SUCCEEDED(hr))hr=factory->CreateBitmapScaler(&scaler);
    if(SUCCEEDED(hr))hr=scaler->Initialize(bitmap.Get(),w,h,WICBitmapInterpolationModeFant);
    vi_vision::Image scaled;scaled.w=w;scaled.h=h;
    if(SUCCEEDED(hr)){scaled.pixels.resize((size_t)w*h*4);hr=scaler->CopyPixels(nullptr,w*4,(UINT)scaled.pixels.size(),scaled.pixels.data());}
    if(SUCCEEDED(hr)){
        BYTE firstAlpha=source.pixels[3];bool uniform=true;
        for(size_t i=3;i<source.pixels.size();i+=4)if(source.pixels[i]!=firstAlpha){uniform=false;break;}
        if(uniform){for(size_t i=3;i<scaled.pixels.size();i+=4)scaled.pixels[i]=firstAlpha;}
        else{
            std::vector<BYTE> alpha((size_t)source.w*source.h),resizedAlpha((size_t)w*h);
            for(size_t i=0;i<alpha.size();++i)alpha[i]=source.pixels[i*4+3];
            Microsoft::WRL::ComPtr<IWICBitmap> alphaBitmap;Microsoft::WRL::ComPtr<IWICBitmapScaler> alphaScaler;
            hr=factory->CreateBitmapFromMemory(source.w,source.h,GUID_WICPixelFormat8bppGray,source.w,(UINT)alpha.size(),alpha.data(),&alphaBitmap);
            if(SUCCEEDED(hr))hr=factory->CreateBitmapScaler(&alphaScaler);
            if(SUCCEEDED(hr))hr=alphaScaler->Initialize(alphaBitmap.Get(),w,h,WICBitmapInterpolationModeFant);
            if(SUCCEEDED(hr))hr=alphaScaler->CopyPixels(nullptr,w,(UINT)resizedAlpha.size(),resizedAlpha.data());
            if(SUCCEEDED(hr))for(size_t i=0;i<resizedAlpha.size();++i)scaled.pixels[i*4+3]=resizedAlpha[i];
        }
    }
    if(vi_vision::detail::Failure(hr,"WIC resize",error))return false;
    output=std::move(scaled);return true;
}
inline bool Save(const std::wstring& path,const vi_vision::Image& image,const std::string& format,int quality,std::string& error) {
    if(!vi_vision::Validate(image,error)||!Dimensions(image.w,image.h,error))return false;
    if(format!="png"&&format!="jpeg"){error="format must be png or jpeg";return false;}
    if(quality<1||quality>100){error="JPEG quality must be 1..100";return false;}
    if(!vi_paths::CaptureParent(path,error))return false;
    vi_vision::detail::ComScope com;
    if(FAILED(com.result)&&com.result!=RPC_E_CHANGED_MODE)return !vi_vision::detail::Failure(com.result,"COM",error);
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;Microsoft::WRL::ComPtr<IWICStream> stream;
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->CreateStream(&stream);
    if(SUCCEEDED(hr))hr=stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
    if(SUCCEEDED(hr))hr=factory->CreateEncoder(format=="jpeg"?GUID_ContainerFormatJpeg:GUID_ContainerFormatPng,nullptr,&encoder);
    if(SUCCEEDED(hr))hr=encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache);
    if(SUCCEEDED(hr))hr=encoder->CreateNewFrame(&frame,&properties);
    if(SUCCEEDED(hr)&&format=="jpeg"){
        PROPBAG2 option{};option.pstrName=const_cast<wchar_t*>(L"ImageQuality");VARIANT value{};value.vt=VT_R4;value.fltVal=quality/100.0f;
        hr=properties->Write(1,&option,&value);
    }
    if(SUCCEEDED(hr))hr=frame->Initialize(properties.Get());
    if(SUCCEEDED(hr))hr=frame->SetSize(image.w,image.h);
    WICPixelFormatGUID requested=format=="jpeg"?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat32bppBGRA,pixelFormat=requested;
    if(SUCCEEDED(hr))hr=frame->SetPixelFormat(&pixelFormat);
    if(SUCCEEDED(hr)&&!IsEqualGUID(requested,pixelFormat)){error="WIC encoder negotiated an unsupported pixel format";return false;}
    std::vector<BYTE> bgr;
    const BYTE* pixels=image.pixels.data();UINT stride=image.w*4,bytes=(UINT)image.pixels.size();
    if(SUCCEEDED(hr)&&format=="jpeg"){
        // Drop alpha without multiplying RGB: swapchain alpha may be zero for otherwise visible pixels.
        bgr.resize((size_t)image.w*image.h*3);
        for(size_t i=0,j=0;i<image.pixels.size();i+=4,j+=3){bgr[j]=image.pixels[i];bgr[j+1]=image.pixels[i+1];bgr[j+2]=image.pixels[i+2];}
        pixels=bgr.data();stride=image.w*3;bytes=(UINT)bgr.size();
    }
    if(SUCCEEDED(hr))hr=frame->WritePixels(image.h,stride,bytes,const_cast<BYTE*>(pixels));
    if(SUCCEEDED(hr))hr=frame->Commit();
    if(SUCCEEDED(hr))hr=encoder->Commit();
    return !vi_vision::detail::Failure(hr,"WIC image encode",error);
}
}
