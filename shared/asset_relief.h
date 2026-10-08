#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Relief follows actual loaded diffuse pixels and UVs. This does not recover
// physical depth from artwork: small, smoothed gradients preserve the OoT paint.
namespace royale::relief {
struct Maps { unsigned width=0,height=0; std::vector<uint8_t> normal,bump; };
inline Maps FromPixels(const uint8_t* rgba,unsigned width,unsigned height) {
    Maps out;
    if (!rgba || !width || !height || width>256 || height>256) return out;
    out.width=width;out.height=height;
    const size_t count=static_cast<size_t>(width)*height;
    std::vector<float> heightmap(count),raw(count);
    for(size_t i=0;i<count;++i)
        raw[i]=rgba[i*4+3] ? (rgba[i*4]*.2126f+rgba[i*4+1]*.7152f+rgba[i*4+2]*.0722f)/255 : .5f;
    auto at=[&](const std::vector<float>& data,int x,int y) {
        return data[std::clamp(y,0,static_cast<int>(height)-1)*width+std::clamp(x,0,static_cast<int>(width)-1)];
    };
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        float total=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)total+=at(raw,x+dx,y+dy);
        heightmap[y*width+x]=.5f+.16f*(total/9-.5f);
    }
    out.normal.resize(count*4);out.bump.resize(count*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        float dx=(at(heightmap,x+1,y)-at(heightmap,x-1,y))*2;
        float dy=(at(heightmap,x,y+1)-at(heightmap,x,y-1))*2;
        const float length=std::sqrt(dx*dx+dy*dy+1);
        const size_t i=(y*width+x)*4;
        out.normal[i]=static_cast<uint8_t>(std::lround((-dx/length*.5f+.5f)*255));
        out.normal[i+1]=static_cast<uint8_t>(std::lround((-dy/length*.5f+.5f)*255));
        out.normal[i+2]=static_cast<uint8_t>(std::lround((1/length*.5f+.5f)*255));out.normal[i+3]=255;
        const uint8_t b=static_cast<uint8_t>(std::lround(heightmap[y*width+x]*255));
        out.bump[i]=out.bump[i+1]=out.bump[i+2]=b;out.bump[i+3]=255;
    }
    return out;
}

// Engine TextureType numeric values. Inputs are read-only user's OTR assets;
// nothing from a ROM is embedded or redistributed by this generator.
inline Maps FromTexture(const uint8_t* data,size_t bytes,unsigned w,unsigned h,int format,
                        const uint8_t* palette0=nullptr,const uint8_t* palette1=nullptr,unsigned bank=0) {
    if(!data || !w || !h || w>256 || h>256) return {};
    const size_t count=static_cast<size_t>(w)*h;
    const int bits[]={0,32,16,4,8,4,8,4,8,16};
    if(format<1 || format>9 || bytes<(count*bits[format]+7)/8) return {};
    std::vector<uint8_t> rgba(count*4);
    auto rgba16=[&](size_t i,uint16_t value) {
        rgba[i*4]=((value>>11)&31)*255/31;rgba[i*4+1]=((value>>6)&31)*255/31;
        rgba[i*4+2]=((value>>1)&31)*255/31;rgba[i*4+3]=(value&1)?255:0;
    };
    for(size_t i=0;i<count;++i) {
        const uint8_t nib=(i&1)?data[i/2]&15:data[i/2]>>4;
        if(format==1) {for(int k=0;k<4;++k)rgba[i*4+k]=data[i*4+k];continue;}
        if(format==2) {rgba16(i,(data[i*2]<<8)|data[i*2+1]);continue;}
        if(format==3 || format==4) {
            const unsigned index=format==3 ? bank*16+nib : data[i];
            const uint8_t* pal=index<128?palette0:palette1;
            if(!pal || index>=256)return {};
            rgba16(i,(pal[(index%128)*2]<<8)|pal[(index%128)*2+1]);continue;
        }
        uint8_t intensity=0,alpha=255;
        switch(format) {
            case 5:intensity=nib*17;break;
            case 6:intensity=data[i];break;
            case 7:intensity=(nib>>1)*255/7;alpha=(nib&1)?255:0;break;
            case 8:intensity=(data[i]>>4)*17;alpha=(data[i]&15)*17;break;
            case 9:intensity=data[i*2];alpha=data[i*2+1];break;
        }
        rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=intensity;rgba[i*4+3]=alpha;
    }
    return FromPixels(rgba.data(),w,h);
}
}
