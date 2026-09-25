#pragma once
#include <map>
// Ambient presentation uses only observed tiles. No engine RNG or input capture.
struct PresenceSettings {
 bool uniques=true,stairs=true,terrain=true,recall=true;
 void load(const json &j) { uniques=j.value("uniques",true); stairs=j.value("stairs",true); terrain=j.value("terrain",true); recall=j.value("recall",true); }
 json serialize() const { return {{"uniques",uniques},{"stairs",stairs},{"terrain",terrain},{"recall",recall}}; }
};
struct ScenePresence {
 struct Actor { double arrival=0,flare=-10; int hp=0; bool asleep=false; };
 std::map<int,Actor> actors;
 std::string level;
 int recall_peak=0;
 static bool observed(const json &view,int x,int y,bool monster=false) {
  x-=view.value("x",0); y-=view.value("y",0);
  if(x<0 || y<0 || x>=view.value("width",0) || y>=view.value("height",0)) return false;
  const auto &c=view["cells"][y][x];
  return c[10].get<int>() && !c[11].get<int>() && (!monster || (c[6].get<int>() && !c[12].get<int>()));
 }
 // Terrain feature IDs come from the player's remembered map, so known
 // lava remains animated outside current sight without exposing unknown tiles.
 static bool known_terrain(const json &cell) {
  return cell[8].get<int>()>0 && cell[0].get<int>()>32 && !cell[11].get<int>();
 }
 void draw(ImDrawList *d,const json &state,const json &catalog,ImVec2 origin,ImVec2 size,float cw,float ch,double now,const PresenceSettings &settings) {
  if(!state.contains("dungeon")) { actors.clear(); recall_peak=0; return; }
  const auto &v=state["dungeon"]; const auto current=v.value("level_id","");
  if(current!=level) { level=current; actors.clear(); recall_peak=0; }
  const int ox=v.value("x",0),oy=v.value("y",0);
  const float u=std::max(cw,ch),pixel=std::max(1.f,std::floor(u*.055f)),clock=float(std::floor(now*20)/20);
  auto point=[&](int x,int y) { return ImVec2(origin.x+(x-ox+.5f)*cw,origin.y+(y-oy+.5f)*ch); };
  const auto clip_min=d->GetClipRectMin(),clip_max=d->GetClipRectMax();
  auto on_screen=[&](ImVec2 p) { return p.x>clip_min.x-3*u && p.y>clip_min.y-3*u && p.x<clip_max.x+3*u && p.y<clip_max.y+3*u; };
  auto color=[](ImVec4 c,float a) { c.w=std::clamp(a,0.f,1.f); return ImGui::GetColorU32(c); };
  auto line=[&](ImVec2 a,ImVec2 b,ImVec4 ink,float alpha) {
   d->AddLine(a,b,color(ink,alpha*.14f),pixel*5); d->AddLine(a,b,color(ink,alpha),pixel);
  };
  auto ring=[&](ImVec2 c,float radius,ImVec4 ink,float alpha,float rotation,int segments) {
   for(int i=0;i<segments;++i) {
    const float a=rotation+i*6.2831853f/segments,b=a+4.1f/segments;
    line({c.x+std::cos(a)*radius,c.y+std::sin(a)*radius},{c.x+std::cos(b)*radius,c.y+std::sin(b)*radius},ink,alpha);
   }
  };
  d->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
  if(settings.terrain || settings.stairs) {
   std::map<int,int> features;
   for(const auto &f:catalog.value("features",json::array())) {
    const auto kind=f.value("map_kind","");
    features[f.value("id",-1)]=kind=="up"?1:kind=="down"?2:f.value("fiery",false)?3:0;
   }
   const int w=v.value("width",0),h=v.value("height",0);
   const int x0=std::clamp(int(std::floor((clip_min.x-origin.x)/cw))-3,0,w),x1=std::clamp(int(std::ceil((clip_max.x-origin.x)/cw))+3,0,w);
   const int y0=std::clamp(int(std::floor((clip_min.y-origin.y)/ch))-3,0,h),y1=std::clamp(int(std::ceil((clip_max.y-origin.y)/ch))+3,0,h);
   for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
    auto at=point(x+ox,y+oy); if(!on_screen(at)) continue;
    const auto &cell=v["cells"][y][x]; if(!known_terrain(cell)) continue;
    auto it=features.find(cell[8].get<int>());
    if(it==features.end() || !it->second) continue;
    const int kind=it->second; const float seed=(x+ox)*.173f+(y+oy)*.317f;
    if(kind<=2 && settings.stairs) {
     const ImVec4 ink=kind==1?ImVec4(1.f,.52f,.16f,1):ImVec4(.85f,.57f,1,1);
     const float pulse=.88f+.12f*std::sin(clock*1.8f+seed);
     const float radius=u*1.05f;
     // A continuous falloff gives a visible pool of light with no ring edge.
     constexpr int rings=12,segments=32;
     d->PrimReserve(rings*segments*6,(rings+1)*(segments+1));
     const auto base=d->_VtxCurrentIdx;
     for(int r=0;r<=rings;++r) {
      const float t=float(r)/rings;
      const auto glow=color(ink,.72f*pulse*(1-t)*(1-t));
      for(int i=0;i<=segments;++i) {
       const float angle=i*6.2831853f/segments;
       d->PrimWriteVtx({at.x+std::cos(angle)*radius*t,at.y+std::sin(angle)*radius*t},d->_Data->TexUvWhitePixel,glow);
      }
     }
     for(int r=0;r<rings;++r) for(int i=0;i<segments;++i) {
      const unsigned a=base+r*(segments+1)+i,b=a+segments+1;
      for(unsigned vertex:{a,b,a+1,a+1,b,b+1}) d->PrimWriteIdx(ImDrawIdx(vertex));
     }
    } else if(kind==3 && settings.terrain) {
     // A permanent hot bed keeps every known tile legible even between sparks.
     const float pulse=.5f+.5f*std::sin(clock*2.3f+seed),memory=cell[10].get<int>()?1.f:.8f;
     d->AddRectFilledMultiColor({at.x-cw*.5f,at.y-ch*.5f},{at.x+cw*.5f,at.y+ch*.5f},
      IM_COL32(255,65,8,int((65+25*pulse)*memory)),IM_COL32(255,95,12,int((75+20*pulse)*memory)),
      IM_COL32(235,35,5,int((65+20*pulse)*memory)),IM_COL32(255,55,6,int((75+20*pulse)*memory)));
     const float yy=at.y+std::sin(clock*1.5f+seed)*ch*.18f;
     line({at.x-cw*.4f,yy+ch*.1f},{at.x,yy},ImVec4(1,.42f,.04f,1),(.55f+.2f*pulse)*memory);
     line({at.x,yy},{at.x+cw*.4f,yy+ch*.12f},ImVec4(1,.6f,.08f,1),(.55f+.2f*pulse)*memory);
     for(int i=0;i<4;++i) {
      float t=std::fmod(clock*.65f+seed+i*.25f,1.f),alpha=std::sin(t*3.14159265f);
      ImVec2 p(at.x+std::sin(seed+i*2+t*3)*cw*.4f,at.y+ch*(.35f-t*1.05f));
      line(p,{p.x+pixel,p.y+pixel*2},ImVec4(1,.45f+.45f*t,.12f,1),alpha*memory);
     }
    }
   }
  }
  if(settings.uniques) for(const auto &m:state.value("monsters",json::array())) {
   if(!m.value("unique",false) || !m.value("visible",false) || !observed(v,m.value("x",-1),m.value("y",-1),true)) continue;
   const auto at=point(m.value("x",0),m.value("y",0));
   const int id=m.value("index",0),hp=m.value("hp",0); const bool sleeping=m.value("asleep",false),boss=m.value("morgoth",false);
   auto entry=actors.try_emplace(id,Actor{now,-10,hp,sleeping}); auto &a=entry.first->second;
   if(hp<a.hp || (a.asleep && !sleeping)) a.flare=now;
   a.hp=hp; a.asleep=sleeping;
   if(!on_screen(at)) continue;
   const float arrival=std::max(0.f,1-float(now-a.arrival)/1.4f),flare=std::max(0.f,1-float(now-a.flare)/.7f);
   const float pulse=.65f+.18f*std::sin(clock*2+id)+.3f*flare;
   const ImVec4 ink=boss?ImVec4(1,.07f,.22f,1):ImVec4(.85f,.35f,1,1);
   for(int i=4;i>0;--i) d->AddCircleFilled(at,u*(.25f+i*.19f),color(ink,(boss?.07f:.035f)*pulse),24);
   ring(at,u*(boss?.9f:.65f),ink,pulse,clock*(boss?-.22f:.18f)+id,boss?10:7);
   if(boss) ring(at,u*(1.1f+std::fmod(clock*.32f,1.f)),ink,.35f*(1-std::fmod(clock*.32f,1.f)),clock*.1f,14);
   for(int i=0;i<(boss?7:4);++i) {
    float angle=i*6.2831853f/(boss?7:4)+id+clock*.23f;
    float reach=u*(.85f+.25f*std::sin(clock*3+i)+flare*.45f);
    ImVec2 start(at.x+std::cos(angle)*u*.4f,at.y+std::sin(angle)*u*.4f),b(at.x+std::cos(angle+.12f)*reach,at.y+std::sin(angle+.12f)*reach);
    line(start,b,boss?ImVec4(1,.82f,.85f,1):ink,(boss?.75f:.5f)*pulse);
   }
   if(arrival>0) ring(at,u*(.7f+(1-arrival)*(boss?3.f:1.8f)),ink,arrival*.85f,-clock*.3f,16);
  } else actors.clear();
  if(state.contains("player")) {
   const auto &p=state["player"]; const int remaining=p.value("recall",0);
   if(remaining<=0 || !settings.recall) recall_peak=0;
   else {
    recall_peak=std::max(recall_peak,remaining);
    const float charge=1-float(remaining)/std::max(1,recall_peak);
    const auto at=point(p.value("x",0),p.value("y",0));
    if(observed(v,p.value("x",-1),p.value("y",-1))) {
     const ImVec4 ink(.25f,.75f,1,1);
     ring(at,u*(.65f+charge*.35f),ink,.4f+charge*.5f,clock*(.35f+charge),8);
     for(int i=0;i<6;++i) {
      const float t=std::fmod(clock*(.35f+charge*.6f)+i/6.f,1.f),angle=i*1.0472f+clock*.25f;
      ImVec2 a(at.x+std::cos(angle)*u*(1.6f-t),at.y+std::sin(angle)*u*(1.6f-t));
      line(a,{a.x,a.y-pixel*2},ink,std::sin(t*3.14159265f)*(.4f+charge*.6f));
     }
    }
   }
  }
  d->PopClipRect();
 }
};
