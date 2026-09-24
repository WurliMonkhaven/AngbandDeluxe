// Observes state boundaries only. No commands or gameplay delays.
// The old glyph grid is copied once per transition, never on ordinary turns.
struct SceneTransitions {
 enum class Kind { None, Load, Down, Up, Dissolve, Shop, Death };
 Kind kind=Kind::None;
 double started=0, colour_return_started=0;
 float colour_return_from=0;
 bool returning_colour=false;
 std::string level, floor, label;
 int depth=0;
 bool established=false, in_shop=false;
 RenderGrid previous;
 static double duration(Kind k) { return k==Kind::Death?.9:k==Kind::Load?.42:k==Kind::Shop?.30:.34; }
 float progress(double now) const { return std::clamp(float((now-started)/duration(kind)),0.f,1.f); }
 bool active(double now) const { return kind!=Kind::None && now-started<duration(kind); }
 void dismiss() { kind=Kind::None; previous={}; returning_colour=false; }
 void reset() { *this=SceneTransitions{}; }
 void start(Kind k,double now,const RenderGrid &old=RenderGrid{}) {
  kind=k; started=now; returning_colour=false; previous=old.semantic?old:RenderGrid{};
 }
 void observe(const json &state,const RenderGrid &old,double now) {
  const auto phase=state.value("phase","");
  if(phase=="launcher" || phase=="birth" || phase=="finished") { reset(); return; }
  if(state.contains("player") && state["player"].value("death_pending",false)) {
   if(kind!=Kind::Death) start(Kind::Death,now);
   return;
  }
  if(kind==Kind::Death) {
   if(state.contains("player") && state["player"].value("hp",0)>0) dismiss();
   else return;
  }

  if(!state.contains("player")) return;
  const bool shop=state.contains("store");
  if(established && shop!=in_shop) {
   start(Kind::Shop,now);
   label=shop?state["store"].value("name","Shop"):depth==0?"Town":"Depth "+std::to_string(depth);
  }
  in_shop=shop;
  // Transient prompts may omit the semantic view. Keep the last level identity.
  if(!state.contains("dungeon")) return;
  const auto next=state["dungeon"].value("level_id","");
  if(next.empty()) return;
  const auto &p=state["player"];
  const int new_depth=p.value("depth",0);
  if(!established) { start(Kind::Load,now); established=true; }
  else if(next!=level) {
   const bool stairs=floor.find("stair")!=std::string::npos;
   start(stairs && new_depth>depth?Kind::Down:stairs && new_depth<depth?Kind::Up:Kind::Dissolve,now,old);
  }
  level=next; depth=new_depth; floor=p.value("floor","");
 }
 void death(const json &,const RenderGrid &,double now) {
  if(kind!=Kind::Death) start(Kind::Death,now);
 }
 void restore_colour(double now) {
  if(kind!=Kind::Death || returning_colour) return;
  colour_return_from=desaturation(now);
  colour_return_started=now; returning_colour=true;
 }
 float desaturation(double now) const {
  if(kind!=Kind::Death) return 0;
  if(returning_colour) {
   const float t=std::clamp(float((now-colour_return_started)/.6),0.f,1.f);
   return colour_return_from*(1-t*t*(3-2*t));
  }
  const float t=progress(now);
  return t*t*(3-2*t);
 }
 static ImU32 alpha(ImU32 ink,float a) { return (ink&0xffffffu)|(ImU32(std::clamp(a,0.f,1.f)*255)<<24); }
 static void rect(ImDrawList *draw,ImVec2 p,ImVec2 size,ImU32 ink) {
  if(size.x>0 && size.y>0) draw->AddRectFilled(p,{p.x+size.x,p.y+size.y},ink);
 }
 static float noise(size_t x,size_t y) { return float((unsigned(x)*73856093u ^ unsigned(y)*19349663u)%101)/100.f; }
 void old_grid(ImDrawList *draw,ImVec2 p,ImVec2 size,float opacity,ImFont *face,float ratio) const {
  if(previous.cells.empty()) return;
  const float font=std::min(size.x/(std::max(size_t(1),previous.width)*ratio),size.y/(std::max(size_t(1),previous.height)*1.12f));
  const float cw=font*ratio,ch=font*1.12f;
  const ImVec2 origin(p.x+(size.x-cw*previous.width)*.5f,p.y+(size.y-ch*previous.height)*.5f);
  for(size_t y=0;y<previous.height;++y) for(size_t x=0;x<previous.width;++x) {
   const auto &cell=previous.cells[y*previous.width+x];
   if(!cell.glyph || cell.glyph==' ') continue;
   auto ink=ImGui::ColorConvertU32ToFloat4(color(cell.color));
   float fade=opacity;
   ImVec2 at(origin.x+x*cw,origin.y+y*ch);
   ink.w=fade;
   draw->AddText(face,font,at,ImGui::GetColorU32(ink),utf8(cell.glyph).c_str());
  }
 }

 void dungeon(ImDrawList *draw,ImVec2 p,ImVec2 size,double now,ImU32 accent,ImFont *face=nullptr,float ratio=.6f) const {
  if(!active(now) || kind==Kind::Shop || kind==Kind::Death) return;
  const float t=progress(now);
  draw->PushClipRect(p,{p.x+size.x,p.y+size.y},true);
  const auto black=IM_COL32(8,12,17,255);
  if(kind!=Kind::Load && t<.4f) {
   rect(draw,p,size,black); old_grid(draw,p,size,1,face?face:ImGui::GetFont(),ratio);
   const float closed=t/.4f;
   if(kind==Kind::Dissolve) rect(draw,p,size,alpha(black,closed));
   else rect(draw,{p.x,p.y+(kind==Kind::Up?size.y*(1-closed):0)}, {size.x,size.y*closed},black);
  } else {
   const float reveal=kind==Kind::Load?t:std::clamp((t-.4f)/.6f,0.f,1.f);
   if(kind==Kind::Dissolve) {
    const float cw=std::max(12.f,ImGui::GetFontSize()),ch=cw*1.2f;
    for(int y=0;y<int(size.y/ch)+1;++y) for(int x=0;x<int(size.x/cw)+1;++x) {
     const float a=1-std::clamp(reveal*1.5f-noise(x,y)*.5f,0.f,1.f);
     rect(draw,{p.x+x*cw,p.y+y*ch},{cw,ch},alpha(black,a));
    }
   } else {
    const bool up=kind==Kind::Up;
    const float edge=p.y+size.y*(up?1-reveal:reveal);
    rect(draw,{p.x,up?p.y:edge},{size.x,size.y*(1-reveal)},black);
    rect(draw,{p.x,edge-4},{size.x,8},alpha(accent,.13f*(1-reveal)));
    draw->AddLine({p.x,edge},{p.x+size.x,edge},alpha(accent,.8f*(1-reveal)),1.5f);
   }
  }
  if(kind==Kind::Load) {
   const ImVec2 corners[]={{p.x,p.y},{p.x+size.x,p.y},{p.x+size.x,p.y+size.y},{p.x,p.y+size.y},{p.x,p.y}};
   for(int i=0;i<4;++i) {
    float q=std::clamp(t*4-i,0.f,1.f);
    draw->AddLine(corners[i],{corners[i].x+(corners[i+1].x-corners[i].x)*q,corners[i].y+(corners[i+1].y-corners[i].y)*q},alpha(accent,1-t*.6f),2);
   }
  }
  draw->PopClipRect();
 }
 bool hold_shop(double now) const {
  return kind==Kind::Shop && active(now) && progress(now)<.4f;
 }
 float shop_darkness(double now) const {
  if(kind!=Kind::Shop || !active(now)) return 0;
  const float t=progress(now);
  const float phase=t<.4f?t/.4f:(1-t)/.6f;
  return phase*phase*(3-2*phase);
 }
};
