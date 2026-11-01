#include "Engine.h"
#include <cstdlib>
#include <iostream>
#include <set>
void check(bool b){if(!b){std::cerr<<"FAIL\n";std::abort();}}
int main(){
 auto a=std::make_shared<scratch::Audio>();a->rate=48000;for(auto& c:a->ch){c.resize(48000);for(int i=12000;i<24000;++i)c[size_t(i)]=float(.5*std::sin(i*.031));}
 auto clip=std::make_shared<scratch::Clip>();clip->audio=a;clip->cuts={0,.25,.5,1};scratch::Settings s;std::set<long long> signatures;
 for(int mode=0;mode<20;++mode){scratch::Engine e;e.prepare(48000);check(e.trigger(clip,1,mode,s));double sig=0,energy=0;for(int i=0;i<30000;++i){float l,r;e.tick(l,r);check(std::isfinite(l)&&std::isfinite(r)&&l==r);sig+=l*std::sin(i*.021);energy+=l*l;}check(energy>1);check(!e.isActive());signatures.insert(std::llround(sig*1e5));e.kill();e.trigger(clip,0,mode,s);for(int i=0;i<30000;++i){float l,r;e.tick(l,r);check(l==0&&r==0);}s.repeat=true;e.trigger(clip,1,mode,s);for(int i=0;i<60000;++i){float l,r;e.tick(l,r);}check(e.isActive());e.stop();for(int i=0;i<500;++i){float l,r;e.tick(l,r);}check(!e.isActive());s.repeat=false;}
 check(signatures.size()>=18);
 for(double rate:{44100.,48000.,96000.}){scratch::Engine e;e.prepare(rate);s.speed=3;s.strokeMs=10;e.trigger(clip,1,-1,s);int active=0;for(int i=0;i<int(rate*.4);++i){float l,r;e.tick(l,r);if(e.isActive())++active;check(std::isfinite(l));}check(std::abs(active-int(rate*.25))<=2);}
 scratch::Engine e;e.prepare(48000);e.trigger(clip,0,-1,s,true);for(int i=0;i<48000;++i){float l,r;e.tick(l,r);}float l,r;e.tick(l,r);check(!e.isActive());
 // Clean interior is unchanged at equal source/host rate, regardless of scratch controls.
 e.trigger(clip,1,-1,s);for(int i=0;i<10000;++i){e.tick(l,r);if(i>200)check(std::abs(l-a->ch[0][size_t(12000+i)])<1e-6f);}
 std::cout<<"PASS: 20 gestures, finite stereo, slice isolation, repeat/release, clean duration at 3 rates, full phrase, unchanged clean interior. Unique signatures: "<<signatures.size()<<"\n";
}
