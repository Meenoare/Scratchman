// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>
namespace scratch {
constexpr double pi=3.14159265358979323846;
inline double clamp(double x,double lo,double hi){return std::max(lo,std::min(hi,x));}
inline double frac(double x){return x-std::floor(x);}
inline constexpr std::array<const char*,20> names={{"Baby","Chirp","Transformer","1-click Flare","2-click Flare","3-click Flare","Scribble","Tear","Forward","Reverse","Stab","Drag","Chirp-Flare","Orbit","Crab-style","Twiddle-style","Echo scratch","Brake","Spinback","Stutter"}};
struct Audio {double rate=44100;std::array<std::vector<float>,2> ch;};
struct Clip {std::shared_ptr<const Audio> audio;std::vector<double> cuts;};
struct Settings {double bpm=100,beats=1,speed=1,strokeMs=250,variation=.12;bool repeat=false;};
struct Point {double x,gate;};
inline Point gesture(int kind,double p,int cycle,double variation){
 double split=clamp(.5+variation*std::sin((cycle+1)*12.9898+kind*7.23)*.12,.38,.62);
 bool fw=p<split;double q=fw?p/split:(p-split)/(1-split),curve=.5-.5*std::cos(pi*q),x=fw?curve:1-curve,g=1;
 auto cut=[&](int n){for(int k=1;k<=n;++k)if(std::abs(q-double(k)/(n+1))<.035)return 0.;return 1.;};
 switch(kind){
 case 1:g=(p>.4&&p<.59)||p>.965?0:1;break;
 case 2:g=frac(p*8)<.43?1:0;break;
 case 3:g=cut(1);break;case 4:g=cut(2);break;case 5:g=cut(3);break;
 case 6:x=.5-.5*std::cos(6*pi*p);break;
 case 7:{double a=std::min(1.,(std::floor(q*3)+std::max(0.,(frac(q*3)-.34)/.66))/3);x=fw?a:1-a;break;}
 case 8:g=fw?1:0;break;case 9:g=fw?0:1;break;
 case 10:x=fw?std::min(1.,q*2):1-curve;g=fw&&q<.52?1:0;break;
 case 11:x=fw?std::pow(q,2.3):std::pow(1-q,2.3);break;
 case 12:g=fw?(q>.78?0:1):cut(1);break;
 case 13:x=fw?q:1-q;g=cut(2);break;
 case 14:g=q>.13&&q<.83?(frac((q-.13)*7)<.42?1:0):1;break;
 case 15:g=q>.22&&q<.78?(frac((q-.22)*5)<.40?1:0):1;break;
 case 16:{double v=frac(p*4);x=.5-.5*std::cos(2*pi*v);g=std::pow(.63,std::floor(p*4))*(v<.9?1:0);break;}
 case 17:x=1-std::pow(1-p,3);break;case 18:x=std::pow(1-p,2.5);break;
 case 19:x=frac(p*4);g=x<.7?1:0;break;default:break;
 }return {clamp(x,0,1),g};
}
class Engine {
public:
 void prepare(double sr){rate=std::max(8000.,sr);kill();}
 bool trigger(std::shared_ptr<const Clip> c,int slice,int kind,const Settings& s,bool whole=false){
  if(!c||!c->audio||c->audio->ch[0].empty()||c->audio->ch[1].size()!=c->audio->ch[0].size()||c->cuts.size()<2)return false;
  clip=std::move(c);slice=std::max(0,std::min(int(clip->cuts.size())-2,slice));auto& a=*clip->audio;
  start=whole?0:std::llround(clip->cuts[size_t(slice)]*a.rate);end=whole?double(a.ch[0].size()):std::llround(clip->cuts[size_t(slice+1)]*a.rate);
  start=clamp(start,0,double(a.ch[0].size()-1));end=clamp(end,start+1,double(a.ch[0].size()));clean=kind<0;mode=std::max(0,std::min(19,kind));
  duration=clean?(end-start)*rate/a.rate:std::max(32.,rate*60/clamp(s.bpm,20,400)*clamp(s.beats,.125,8)/clamp(s.speed,.25,4));
  span=clamp(s.strokeMs*a.rate/1000,0,std::max(0.,end-start-1));variation=clamp(s.variation,0,1);repeat=s.repeat;t=0;gate=0;release=0;active=true;return true;
 }
 void stop(){if(active)release=std::max(1,int(rate*.006));}
 void kill(){active=false;release=0;t=0;}
 bool isActive()const{return active;}
 bool isRepeating()const{return repeat;}
 double position()const{return active&&clip?pos/clip->audio->rate:-1;}
 void tick(float& l,float& r){
  l=r=0;if(!active||!clip)return;if(t>=duration&&!repeat){active=false;return;}
  double p=frac(t/duration);auto pt=clean?Point{0,1}:gesture(mode,p,int(t/duration),variation);
  pos=clamp(clean?start+p*(end-start):start+pt.x*span,start,end-1);
  gate=clean?1:gate+(pt.gate-gate)*(1-std::exp(-1/(rate*.0006)));
  double env=std::min(1.,t/(rate*.003));if(!repeat)env*=std::min(1.,std::max(0.,duration-t)/(rate*.003));
  if(release){env*=release/(rate*.006);if(--release==0){active=false;return;}}
  for(int c=0;c<2;++c){auto& a=clip->audio->ch[size_t(c)];auto i=size_t(pos);double f=pos-double(i);auto j=std::min(i+1,size_t(end-1));float v=float((a[i]*(1-f)+a[j]*f)*env*gate);if(c==0)l=v;else r=v;}++t;
 }
private:
 std::shared_ptr<const Clip> clip;double rate=44100,start=0,end=1,duration=1,span=1,t=0,gate=0,pos=0,variation=0;
 bool active=false,clean=false,repeat=false;int mode=0,release=0;
};
}
