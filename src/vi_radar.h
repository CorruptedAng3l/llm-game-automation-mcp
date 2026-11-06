#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Bounded screen-space observations. A marker bearing is not world yaw and a
// marker near the configured centre is not proof of a mission/gameplay event.
namespace vi_radar {
struct Pixel {unsigned char r=0,g=0,b=0;};
struct Image {const unsigned char* bgra=nullptr;unsigned w=0,h=0;};
struct Target {Pixel colour;unsigned tolerance=12,minPixels=2,maxPixels=256;std::vector<unsigned char> rgba;unsigned w=0,h=0;double minScore=.95;};
struct Detection {bool complete=false,found=false,ambiguous=false;double x=0,y=0,score=0;unsigned candidates=0,matchedPixels=0;std::string reason;};
inline bool Near(const unsigned char* p,const Pixel& c,unsigned tolerance){return (unsigned)std::abs((int)p[2]-c.r)<=tolerance&&(unsigned)std::abs((int)p[1]-c.g)<=tolerance&&(unsigned)std::abs((int)p[0]-c.b)<=tolerance;}
inline bool Valid(const Image& image){return image.bgra&&image.w&&image.h&&image.w<=512&&image.h<=512;}
inline Detection DetectColour(const Image& image,const Target& target){
 Detection out;if(!Valid(image)||target.tolerance>255||!target.minPixels||target.maxPixels<target.minPixels||target.maxPixels>65536){out.reason="invalid_marker_image_or_colour";return out;}
 const size_t count=(size_t)image.w*image.h;std::vector<unsigned char> visited(count);std::vector<unsigned> pending;pending.reserve(count);
 for(unsigned start=0;start<count;++start){if(visited[start])continue;visited[start]=1;if(!Near(image.bgra+4u*start,target.colour,target.tolerance))continue;
  pending.clear();pending.push_back(start);std::uint64_t sx=0,sy=0;unsigned pixels=0;
  for(size_t next=0;next<pending.size();++next){const unsigned at=pending[next],x=at%image.w,y=at/image.w;++pixels;sx+=x;sy+=y;
   const unsigned neighbour[4]={x?at-1:at,x+1<image.w?at+1:at,y?at-image.w:at,y+1<image.h?at+image.w:at};
   for(unsigned index:neighbour)if(!visited[index]){visited[index]=1;if(Near(image.bgra+4u*index,target.colour,target.tolerance))pending.push_back(index);}
  }
  if(pixels>=target.minPixels&&pixels<=target.maxPixels){++out.candidates;if(out.candidates==1){out.x=(double)sx/pixels;out.y=(double)sy/pixels;out.matchedPixels=pixels;out.score=1;}}
 }
 out.complete=true;out.found=out.candidates==1;out.ambiguous=out.candidates>1;out.reason=out.found?"unique_colour_component":out.ambiguous?"ambiguous_marker":"marker_not_found";return out;
}
inline Detection DetectTemplate(const Image& image,const Target& target){
 Detection out;if(!Valid(image)||image.w>256||image.h>256||!target.w||!target.h||target.w>32||target.h>32||target.w>image.w||target.h>image.h||target.rgba.size()!=(size_t)target.w*target.h*4||target.tolerance>255||!std::isfinite(target.minScore)||target.minScore<.5||target.minScore>1){out.reason="invalid_marker_template";return out;}
 struct Sample {unsigned x,y;Pixel colour;};std::vector<Sample> samples;
 for(unsigned y=0;y<target.h;++y)for(unsigned x=0;x<target.w;++x){const auto* p=target.rgba.data()+4u*(y*target.w+x);if(p[3]>=128)samples.push_back({x,y,{p[0],p[1],p[2]}});}
 if(samples.size()<2||samples.size()>1024){out.reason="template_needs_two_opaque_pixels";return out;}
 const unsigned required=(unsigned)std::ceil(target.minScore*samples.size());unsigned work=0;
 struct Match {double x,y,score;};std::vector<Match> clusters;
 for(unsigned y=0;y+target.h<=image.h;++y)for(unsigned x=0;x+target.w<=image.w;++x){unsigned matched=0;
  for(size_t n=0;n<samples.size();++n){if(++work>2000000){out.reason="template_scan_budget_exhausted";return out;}const auto& s=samples[n];if(Near(image.bgra+4u*((y+s.y)*image.w+x+s.x),s.colour,target.tolerance))++matched;if(matched+samples.size()-n-1<required)break;}
  if(matched<required)continue;const double cx=x+(target.w-1)*.5,cy=y+(target.h-1)*.5,score=(double)matched/samples.size();bool merged=false;
  for(auto& cluster:clusters)if(std::abs(cluster.x-cx)<=target.w*.5&&std::abs(cluster.y-cy)<=target.h*.5){if(score>cluster.score)cluster={cx,cy,score};merged=true;break;}
  if(!merged){clusters.push_back({cx,cy,score});if(clusters.size()>64){out.reason="template_candidate_cap";return out;}}
 }
 out.complete=true;out.candidates=(unsigned)clusters.size();out.found=clusters.size()==1;out.ambiguous=clusters.size()>1;
 if(out.found){out.x=clusters[0].x;out.y=clusters[0].y;out.score=clusters[0].score;out.matchedPixels=required;}
 out.reason=out.found?"unique_template_match":out.ambiguous?"ambiguous_marker":"marker_not_found";return out;
}
inline Detection Detect(const Image& image,const Target& target){return target.rgba.empty()?DetectColour(image,target):DetectTemplate(image,target);}
inline double Angle(double degrees){while(degrees>180)degrees-=360;while(degrees<=-180)degrees+=360;return degrees;}
struct Config {double centreX=0,centreY=0,arrivalRadius=8,angleTolerance=8,forwardCone=25,fullSteer=60,steeringSign=1;unsigned samples=3,progressMs=1500,periodMs=100;bool turnOnly=false,throttleWhileTurning=false;double degrees=0;};
struct Decision {bool observed=false,forward=false,done=false;double steer=0,bearing=0,error=0,distance=0,radarAngleDelta=0;std::string reason;};
struct State {bool started=false,lastForward=false;double initialBearing=0,goal=0,best=0;std::uint64_t lastQpc=0,lastProgress=0;unsigned stable=0,samples=0;Decision latest;};
struct Trace {std::uint64_t qpc=0;Decision decision;};
inline bool Valid(const Config& c){return std::isfinite(c.centreX)&&std::isfinite(c.centreY)&&std::isfinite(c.arrivalRadius)&&c.arrivalRadius>=1&&c.arrivalRadius<=128&&std::isfinite(c.angleTolerance)&&c.angleTolerance>=1&&c.angleTolerance<=45&&std::isfinite(c.forwardCone)&&c.forwardCone>=1&&c.forwardCone<=90&&std::isfinite(c.fullSteer)&&c.fullSteer>=10&&c.fullSteer<=180&&(c.steeringSign==1||c.steeringSign==-1)&&c.samples>=1&&c.samples<=10&&c.progressMs>=250&&c.progressMs<=10000&&c.periodMs>=20&&c.periodMs<=1000&&std::isfinite(c.degrees)&&std::abs(c.degrees)<=179;}
inline Decision Observe(State& state,const Config& c,const Detection& marker,double scaleX,double scaleY,std::uint64_t qpc,std::uint64_t now){
 Decision out;if(!Valid(c)||!marker.complete||!marker.found||marker.ambiguous||!qpc||qpc<=state.lastQpc||!std::isfinite(scaleX)||!std::isfinite(scaleY)||scaleX<=0||scaleY<=0){out.done=true;out.reason=!marker.complete?marker.reason:marker.ambiguous?"ambiguous_marker":!marker.found?"marker_not_found":"invalid_or_regressed_observation";state.latest=out;return out;}
 out.observed=true;
 const double x=marker.x*scaleX-c.centreX,y=marker.y*scaleY-c.centreY;out.distance=std::hypot(x,y);out.bearing=std::atan2(x,-y)*180/3.14159265358979323846;
 if(c.turnOnly&&out.distance<1){out.observed=false;out.done=true;out.reason="reference_marker_at_centre";state.latest=out;return out;}
 if(!state.started){state.started=true;state.initialBearing=out.bearing;state.goal=c.turnOnly?Angle(out.bearing-c.degrees):0;state.lastProgress=now;state.best=(std::numeric_limits<double>::max)();}
 state.lastQpc=qpc;++state.samples;out.error=Angle(out.bearing-state.goal);out.radarAngleDelta=Angle(state.initialBearing-out.bearing);
 const bool close=c.turnOnly?std::abs(out.error)<=c.angleTolerance:out.distance<=c.arrivalRadius;
 state.stable=close?state.stable+1:0;
 if(state.stable>=c.samples){out.done=true;out.reason=c.turnOnly?"radar_bearing_aligned":"radar_marker_near_centre";state.latest=out;return out;}
 if(!close){out.steer=std::abs(out.error)<=c.angleTolerance?0:(std::min)(1.0,std::abs(out.error)/c.fullSteer)*(out.error<0?-1:1)*c.steeringSign;out.forward=!c.turnOnly&&(c.throttleWhileTurning||std::abs(out.error)<=c.forwardCone);}
 if(state.lastForward!=out.forward){state.best=(std::numeric_limits<double>::max)();state.lastProgress=now;state.lastForward=out.forward;}
 const double metric=c.turnOnly||!out.forward?std::abs(out.error):out.distance;const double gain=c.turnOnly||!out.forward?2:1;
 if(metric+gain<state.best){state.best=metric;state.lastProgress=now;}
 else if(!close&&now>=state.lastProgress&&now-state.lastProgress>=c.progressMs){out.done=true;out.forward=false;out.steer=0;out.reason="no_marker_progress";}
 state.latest=out;return out;
}
inline bool PulseDown(double steer,unsigned period,std::uint64_t elapsed){const double duty=std::abs(steer);if(duty<=0)return false;if(duty>=1)return true;const unsigned on=(std::max)(10u,(std::min)(period-10,(unsigned)std::llround(period*duty)));return elapsed%period<on;}
}
