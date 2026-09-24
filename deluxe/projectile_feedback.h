// Real, visible engine tiles only. Animation never owns input or delays a turn.
struct ProjectileFeedback {
 struct Tile { int x,y; float delay; char glyph; ImVec4 colour; double born; bool arc=false; };
 std::vector<Tile> tiles;
 std::string level;
 static constexpr double lifetime=.38, breath_lifetime=.64;
 static ImVec4 colour(const std::string &kind) {
  if(kind=="FIRE" || kind=="PLASMA") return {1,.45f,.16f,1};
  if(kind=="COLD" || kind=="ICE") return {.45f,.87f,1,1};
  if(kind=="ELEC") return {1,.95f,.43f,1};
  if(kind=="ACID") return {.64f,1,.24f,1};
  if(kind=="POIS") return {.36f,.88f,.5f,1};
  if(kind=="DARK" || kind=="NETHER" || kind=="CHAOS") return {.8f,.52f,1,1};
  if(kind=="MISSILE") return {1,.82f,.48f,1};
  return {.68f,.82f,1,1};
 }
 void update(std::deque<json> &events,const json &state,bool enabled,double now) {
  const auto current=state.contains("dungeon")?state["dungeon"].value("level_id",""):"";
  if(!enabled || current.empty() || state.value("phase","")!="playing") {
   tiles.clear(); events.clear(); level=current; return;
  }
  if(level!=current) { tiles.clear(); level=current; }
  tiles.erase(std::remove_if(tiles.begin(),tiles.end(),[&](const Tile &t){return now-t.born>=(t.arc?breath_lifetime:lifetime);}),tiles.end());
  for(const auto &event:events) {
   const double born=event.value("received",now);
   if(event.value("level_id","")!=level || now-born>=breath_lifetime) continue;
   for(const auto &effect:event.at("effects")) {
    const auto &points=effect.at("tiles");
    const bool blast=effect.value("blast",false), arc=effect.value("arc",false);
    if(now-born>=(arc?breath_lifetime:lifetime)) continue;
    const auto kind=effect.value("element","");
    const auto ink=colour(kind);
    int radius=1;
    if(blast) for(const auto &p:points) radius=std::max(radius,p[2].get<int>());
    for(size_t i=0;i<points.size() && tiles.size()<2048;++i) {
     const auto &p=points[i];
     char glyph='*';
     if(!blast) {
      int dx=1,dy=0;
      if(i) { dx=p[0].get<int>()-points[i-1][0].get<int>(); dy=p[1].get<int>()-points[i-1][1].get<int>(); }
      glyph=dy==0?'-':dx==0?'|':dx*dy>0?'\\':'/';
      if(kind=="ELEC") glyph=i%2?'~':'*';
     } else if(kind=="COLD" || kind=="ICE") glyph='+';
     if(arc) glyph=(kind=="COLD" || kind=="ICE")?'+':kind=="ELEC"?'*':(kind=="POIS" || kind=="ACID")?'~':'^';
     const float delay=arc?.24f*p[2].get<float>()/radius:blast?.07f+.1f*p[2].get<float>()/radius:.12f*float(i)/std::max(size_t(1),points.size()-1);
     tiles.push_back({p[0].get<int>(),p[1].get<int>(),delay,glyph,ink,born,arc});
    }
   }
  }
  events.clear();
 }
 static void draw_breath(ImDrawList *draw,const Tile &t,float x,float y,float cw,float ch,float age) {
  const float progress=age/.36f;
  const float attack=std::min(1.f,age/.025f), fade=(1-progress)*(1-progress);
  const float energy=attack*fade;
  // Contiguous translucent wash gives the cone a body; the hot leading edge
  // and sparse drifting ASCII wisps give it motion without hiding the dungeon.
  auto haze=t.colour; haze.w=energy*.26f;
  draw->AddRectFilled({x-cw*.5f,y-ch*.5f},{x+cw*.5f,y+ch*.5f},ImGui::GetColorU32(haze));
  auto core=t.colour;
  const float hot=std::max(0.f,1-age/.11f);
  core.x+=(1-core.x)*hot*.75f; core.y+=(1-core.y)*hot*.75f; core.z+=(1-core.z)*hot*.75f;
  core.w=energy*.9f;
  const unsigned seed=unsigned(t.x)*73856093u ^ unsigned(t.y)*19349663u;
  const float drift=(float(seed%101)/100.f-.5f)*cw*.35f;
  const float font=std::min(ch*.85f,ImGui::GetFontSize()*(.8f+.25f*hot));
  const char glyph[2]={t.glyph,0};
  const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,glyph);
  draw->AddText(ImGui::GetFont(),font,{x-measure.x*.5f+drift*progress,y-measure.y*.5f-ch*.18f*progress},ImGui::GetColorU32(core),glyph);
  // Tiny motes remain inside their affected tile. No fabricated path or RNG.
  if(seed%3==0) {
   auto mote=core; mote.w*=.65f;
   const float mx=x+cw*(seed%2?.3f:-.3f),my=y+ch*(.27f-.4f*progress);
   draw->AddCircleFilled({mx,my},std::max(1.f,cw*.055f),ImGui::GetColorU32(mote),6);
  }
 }
 void draw(ImDrawList *draw,ImVec2 origin,ImVec2 size,float cw,float ch,int ox,int oy,double now) const {
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  for(const auto &t:tiles) {
   const float age=float(now-t.born)-t.delay;
   if(age<0 || age>(t.arc?.36f:.20f)) continue;
   const float x=origin.x+(t.x-ox+.5f)*cw,y=origin.y+(t.y-oy+.5f)*ch;
   if(x<origin.x || y<origin.y || x>=origin.x+size.x || y>=origin.y+size.y) continue;
   if(t.arc) { draw_breath(draw,t,x,y,cw,ch,age); continue; }
   auto ink=t.colour; ink.w=(1-age/.20f)*.9f;
   auto halo=ink; halo.w*=.14f;
   draw->AddRectFilled({x-cw*.45f,y-ch*.4f},{x+cw*.45f,y+ch*.4f},ImGui::GetColorU32(halo),3);
   const char text[2]={t.glyph,0};
   const float font=std::min(ch,ImGui::GetFontSize()*1.15f);
   const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,text);
   draw->AddText(ImGui::GetFont(),font,{x-measure.x*.5f,y-measure.y*.5f},ImGui::GetColorU32(ink),text);
  }
  draw->PopClipRect();
 }
};
