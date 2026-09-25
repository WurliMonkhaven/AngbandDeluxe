#pragma once
// Draw beneath glyphs. Observation-only input; no live item records or RNG.
struct ItemGlow {
 static int kind(const json &item,const json &view) {
  const int x=item.value("x",-1)-view.value("x",0),y=item.value("y",-1)-view.value("y",0);
  if(x<0 || y<0 || x>=view.value("width",0) || y>=view.value("height",0)) return 0;
  const auto &cell=view["cells"][y][x];
  if(!cell[4].get<int>() || !cell[10].get<int>() || cell[11].get<int>() || cell[6].get<int>() || cell[12].get<int>()) return 0;
  const auto aura=item.value("aura","");
  return aura=="cursed"?3:aura=="artifact"?2:aura=="rune"?1:0;
 }
 static void draw(ImDrawList *draw,const json &view,ImVec2 origin,ImVec2 size,float cw,float ch,double now) {
  if(!view.contains("items")) return;
  // One halo per pile; curses take precedence over artifacts, then runes.
  std::vector<int> piles(size_t(view.value("width",0)*view.value("height",0)),0);
  const int width=view.value("width",0);
  for(const auto &item:view["items"]) {
   int k=kind(item,view); if(!k) continue;
   const int index=(item.value("y",0)-view.value("y",0))*width+item.value("x",0)-view.value("x",0);
   piles[index]=std::max(piles[index],k);
  }
  draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
  for(size_t index=0;index<piles.size();++index) {
   const int k=piles[index]; if(!k) continue;
   const float seed=float((int(index%width)+view.value("x",0))* .73+(int(index/width)+view.value("y",0))*.37);
   const float clock=float(std::floor(now*18.)/18.); // Deliberately stepped, terminal-like motion.
   const float phase=clock*.95f+seed,pulse=.82f+.18f*std::sin(phase);
   const ImVec2 center(origin.x+(index%width+.5f)*cw,origin.y+(index/width+.45f)*ch);
   const ImVec4 ink=k==3?ImVec4(1.f,.05f,.15f,1):k==2?ImVec4(1.f,.85f,.38f,1):ImVec4(.3f,.65f,1.f,1);
   const float radius=std::max(cw,ch)*(k==2?1.7f:k==3?1.5f:1.25f);
   // A continuous radial mesh avoids visible concentric bands at high intensity.
   constexpr int rings=16,segments=48;
   draw->PrimReserve(rings*segments*6,(rings+1)*(segments+1));
   const auto base=draw->_VtxCurrentIdx;
   for(int ring=0;ring<=rings;++ring) {
    const float t=float(ring)/rings;
    auto c=ink; c.w=(k==1?.58f:.82f)*pulse*(1-t)*(1-t);
    const auto color=ImGui::GetColorU32(c);
    for(int n=0;n<=segments;++n) {
     const float angle=float(n)*6.2831853f/segments;
     draw->PrimWriteVtx({center.x+std::cos(angle)*radius*t,center.y+std::sin(angle)*radius*t},draw->_Data->TexUvWhitePixel,color);
    }
   }
   for(int ring=0;ring<rings;++ring) for(int n=0;n<segments;++n) {
    const unsigned a=base+ring*(segments+1)+n,b=a+segments+1;
    for(unsigned v:{a,b,a+1,a+1,b,b+1}) draw->PrimWriteIdx(ImDrawIdx(v));
   }
   const float unit=std::max(cw,ch),pixel=std::max(1.f,std::floor(unit*.055f));
   auto point=[&](float angle,float distance) { return ImVec2(center.x+std::cos(angle)*distance,center.y+std::sin(angle)*distance); };
   auto colour=[&](float alpha,bool white=false) { auto c=white?ImVec4(1.f,.97f,.8f,1):ink; c.w=alpha; return ImGui::GetColorU32(c); };
   if(k==2) {
    // A slow six-point sunburst opens, breaks into rays, and gathers again.
    // The empty middle protects the actual item glyph.
    const float beat=.5f+.5f*std::sin(clock*2.4f+seed);
    for(int ray=0;ray<6;++ray) {
     const float angle=ray*1.04719755f+clock*.12f+seed;
     const float length=unit*(.7f+.7f*beat+(ray%2)*.2f);
     for(int segment=0;segment<4;++segment) {
      const float t=float(segment)/4;
      auto a=point(angle,unit*.25f+length*t),b=point(angle,unit*.25f+length*(t+.20f));
      const float alpha=(1-t)*(.35f+.5f*beat);
      draw->AddLine(a,b,colour(alpha*.22f),pixel*4);
      draw->AddLine(a,b,colour(alpha,true),pixel);
     }
    }
    // Square gold motes lift away; tiny cross-shaped flashes punctuate them.
    for(int i=0;i<5;++i) {
     const float t=std::fmod(clock*.38f+seed+i*.2f,1.f);
     const float alpha=std::sin(t*3.14159265f);
     ImVec2 at(center.x+std::sin(seed+i*2.4f+t)*cw*.85f,center.y-ch*(.15f+t*1.9f));
     at.x=std::floor(at.x/pixel)*pixel; at.y=std::floor(at.y/pixel)*pixel;
     draw->AddRectFilled({at.x-pixel,at.y-pixel},{at.x+pixel,at.y+pixel},colour(alpha,true));
     if(i%2==0) {
      draw->AddLine({at.x-pixel*3,at.y},{at.x+pixel*3,at.y},colour(alpha*.6f),pixel);
      draw->AddLine({at.x,at.y-pixel*3},{at.x,at.y+pixel*3},colour(alpha*.6f),pixel);
     }
    }
   } else if(k==1) {
    // Broken diamond wavefronts read like a little arcane circuit waking up.
    for(int wave=0;wave<2;++wave) {
     const float t=std::fmod(clock*.48f+seed+wave*.5f,1.f);
     const float r=unit*(.3f+t*.85f),alpha=.8f*std::sin(t*3.14159265f);
     for(int edge=0;edge<4;++edge) {
      const auto a=point(edge*1.5707963f,r),b=point((edge+1)*1.5707963f,r);
      ImVec2 start(a.x+(b.x-a.x)*.12f,a.y+(b.y-a.y)*.12f),end(a.x+(b.x-a.x)*.7f,a.y+(b.y-a.y)*.7f);
      draw->AddLine(start,end,colour(alpha*.18f),pixel*4);
      draw->AddLine(start,end,colour(alpha),pixel);
      draw->AddRectFilled({a.x-pixel,a.y-pixel},{a.x+pixel,a.y+pixel},colour(alpha));
     }
    }
   } else {
    // Crimson shards crawl upwards in crooked, pixel-snapped trails.
    for(int i=0;i<5;++i) {
     const float t=std::fmod(clock*.46f+seed+i*.2f,1.f),alpha=std::sin(t*3.14159265f);
     ImVec2 at(center.x+std::sin(i*2.1f+seed+t*4)*cw*(.4f+t*.6f),center.y+ch*(.3f-t*1.8f));
     at.x=std::floor(at.x/pixel)*pixel; at.y=std::floor(at.y/pixel)*pixel;
     ImVec2 elbow(at.x+(i%2?1:-1)*pixel*3,at.y+pixel*3),tail(at.x,at.y+pixel*7);
     draw->AddLine(tail,elbow,colour(alpha*.22f),pixel*3);
     draw->AddLine(elbow,at,colour(alpha*.8f),pixel*2);
     draw->AddRectFilled({at.x-pixel,at.y-pixel},{at.x+pixel,at.y+pixel},IM_COL32(255,110,130,int(alpha*240)));
    }
   }
  }
  draw->PopClipRect();
 }
};
