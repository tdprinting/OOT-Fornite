#include "asset_relief.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    using namespace royale::relief;
    uint8_t flat[16*4];std::fill(std::begin(flat),std::end(flat),128);
    Maps f=FromPixels(flat,4,4);
    for(size_t i=0;i<f.normal.size();i+=4)assert(f.normal[i]==128&&f.normal[i+1]==128&&f.normal[i+2]==255);
    uint8_t ramp[8*8*4];
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        int i=(y*8+x)*4;ramp[i]=ramp[i+1]=ramp[i+2]=x*32;ramp[i+3]=255;
    }
    Maps a=FromPixels(ramp,8,8);assert(a.normal[4*4]<128&&a.normal[4*4+1]==128);
    for(size_t i=0;i<a.normal.size();i+=4) {
        float length=0;for(int k=0;k<3;++k){float n=a.normal[i+k]/255.f*2-1;length+=n*n;}
        assert(std::fabs(length-1)<.02f);
    }
    assert(FromTexture(nullptr,0,1,1,1).normal.empty());
    assert(FromTexture(ramp,3,8,8,1).normal.empty());
    uint8_t tex[]={0xff,0xff,0,1};
    Maps metal=FromTexture(tex,sizeof(tex),2,1,2);assert(metal.bump[0]>metal.bump[4]);
    uint8_t indexed[]={0x01},palette[256]={};palette[2]=palette[3]=255;
    Maps wood=FromTexture(indexed,1,2,1,3,palette,nullptr,0);assert(wood.bump[0]<wood.bump[4]);
    assert(FromTexture(indexed,1,2,1,3).normal.empty());
    std::cout<<"Asset relief: original diffuse gradients, packed formats, palette and bounds passed\n";
}
