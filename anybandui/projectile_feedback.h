// Real, visible engine tiles only. Animation never owns input or delays a turn.
struct ProjectileFeedback {
 struct Tile { int x,y; float delay; char glyph; ImVec4 colour; double born; bool arc=false; };
 std::vector<Tile> tiles;
 std::string level;
 static constexpr double lifetime=.38, breath_lifetime=.64;
 static ImVec4 colour(const std::string &kind) {
  if(kind=="FIRE" || kind=="PLASMA") return {1,.32f,.055f,1};
  if(kind=="COLD" || kind=="ICE") return {.12f,.78f,1,1};
  if(kind=="ELEC") return {1,.91f,.08f,1};
  if(kind=="ACID") return {.64f,1,.045f,1};
  if(kind=="POIS") return {.08f,1,.32f,1};
  if(kind=="DARK" || kind=="NETHER" || kind=="CHAOS") return {.78f,.19f,1,1};
  if(kind=="MISSILE") return {1,.72f,.2f,1};
  return {.38f,.65f,1,1};
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
 // Keep the halo saturated; only the short-lived hot core approaches white.
 static ImVec4 hot_core(ImVec4 colour,float hot) {
  const float white=.18f+.48f*hot;
  colour.x+=(1-colour.x)*white;
  colour.y+=(1-colour.y)*white;
  colour.z+=(1-colour.z)*white;
  return colour;
 }
 static void luminous_glyph(ImDrawList *draw,float font,ImVec2 at,const char *text,ImVec4 core,ImVec4 colour) {
  // One oversized colour silhouette, then the crisp core. Bounded geometry,
  // no blur textures or extra render passes; also reads with CRT disabled.
  const float spread=1.3f;
  colour.w=core.w*.35f;
  const auto normal=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,text);
  const auto outer=ImGui::GetFont()->CalcTextSizeA(font+spread*2,FLT_MAX,0,text);
  draw->AddText(ImGui::GetFont(),font+spread*2,{at.x-(outer.x-normal.x)*.5f,at.y-spread},ImGui::GetColorU32(colour),text);
  draw->AddText(ImGui::GetFont(),font,at,ImGui::GetColorU32(core),text);
 }
 static void draw_breath(ImDrawList *draw,const Tile &t,float x,float y,float cw,float ch,float age) {
  const float progress=age/.36f;
  const float attack=std::min(1.f,age/.015f), fade=std::min(1.f,(1-progress)/.72f);
  const float energy=attack*fade;
  // Contiguous translucent wash gives the cone a body; the hot leading edge
  // and sparse drifting ASCII wisps give it motion without hiding the dungeon.
  auto haze=t.colour; haze.w=energy*.42f;
  draw->AddRectFilled({x-cw*.5f,y-ch*.5f},{x+cw*.5f,y+ch*.5f},ImGui::GetColorU32(haze));
  auto core=t.colour;
  const float hot=std::max(0.f,1-age/.16f);
  core=hot_core(core,hot);
  core.w=energy;
  const unsigned seed=unsigned(t.x)*73856093u ^ unsigned(t.y)*19349663u;
  const float drift=(float(seed%101)/100.f-.5f)*cw*.35f;
  const float font=std::min(ch*.95f,ImGui::GetFontSize()*(1.f+.2f*hot));
  const char glyph[2]={t.glyph,0};
  const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,glyph);
  luminous_glyph(draw,font,{x-measure.x*.5f+drift*progress,y-measure.y*.5f-ch*.18f*progress},glyph,core,t.colour);
  // Tiny motes remain inside their affected tile. No fabricated path or RNG.
  if(seed%3==0) {
   auto mote=core; mote.w*=.9f;
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
   const float energy=std::min(1.f,(1-age/.20f)/.75f);
   auto ink=hot_core(t.colour,std::max(0.f,1-age/.12f)); ink.w=energy;
   auto halo=t.colour; halo.w=energy*.34f;
   draw->AddRectFilled({x-cw*.5f,y-ch*.46f},{x+cw*.5f,y+ch*.46f},ImGui::GetColorU32(halo),3);
   const char text[2]={t.glyph,0};
   const float font=std::min(ch,ImGui::GetFontSize()*1.15f);
   const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,text);
   luminous_glyph(draw,font,{x-measure.x*.5f,y-measure.y*.5f},text,ink,t.colour);
  }
  draw->PopClipRect();
 }
};
