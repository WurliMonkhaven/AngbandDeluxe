// Short visual interpolation only: engine positions and input remain immediate.
struct MotionFeedback {
 struct Walk { int index,x,y,tx,ty; double born; };
 struct Ripple { int x,y; json tiles; double born; };
 std::map<int,Walk> walks;
 std::vector<Ripple> ripples;
 std::string level,revision;
 static constexpr double walk_time=.10, ripple_time=.34;
 static int key(int x,int y) { return y*65536+x; }
 void update(std::deque<json> &events,const json &state,bool walking,bool blinking,double now) {
  const auto current=state.contains("dungeon")?state["dungeon"].value("level_id",""):"";
  if(current!=level || current.empty() || state.value("phase","")!="playing") { walks.clear(); ripples.clear(); level=current; }
  if(!walking) walks.clear();
  if(!blinking) ripples.clear();
  for(auto i=walks.begin();i!=walks.end();) if(now-i->second.born>=walk_time) i=walks.erase(i); else ++i;
  ripples.erase(std::remove_if(ripples.begin(),ripples.end(),[&](const Ripple &r){return now-r.born>=ripple_time;}),ripples.end());
  const bool changed=!events.empty() || revision!=state.value("revision","");
  for(const auto &batch:events) {
   if(current.empty() || batch.value("level_id","")!=current) continue;
   const double born=batch.value("received",now);
   for(const auto &e:batch.at("effects")) {
    const int index=e.value("index",0),x=e.value("x",0),y=e.value("y",0),tx=e.value("tx",x),ty=e.value("ty",y);
    // A later event always replaces a previous move, including adjacent blinks.
    for(auto i=walks.begin();i!=walks.end();) if(i->second.index==index) i=walks.erase(i); else ++i;
    if(e.value("blink",false)) {
     if(blinking && now-born<ripple_time && ripples.size()<64) ripples.push_back({x,y,e.at("tiles"),born});
    } else if(walking && index>0 && now-born<walk_time && std::abs(tx-x)<=1 && std::abs(ty-y)<=1 && walks.size()<128)
     walks[key(tx,ty)]={index,x,y,tx,ty,born};
   }
  }
  events.clear();
  if(changed) {
   revision=state.value("revision","");
   // Removed, hidden, teleported or subsequently displaced actors cannot slide.
   for(auto i=walks.begin();i!=walks.end();) {
    bool valid=false;
    if(state.contains("monsters")) for(const auto &m:state["monsters"])
     if(m.value("index",0)==i->second.index && m.value("visible",false) && m.value("x",-1)==i->second.tx && m.value("y",-1)==i->second.ty) { valid=true; break; }
    if(!valid) i=walks.erase(i); else ++i;
   }
  }
 }
 ImVec2 offset(int x,int y,double now) const {
  const auto i=walks.find(key(x,y)); if(i==walks.end()) return {0,0};
  const auto &w=i->second;
  const float t=std::clamp(float((now-w.born)/walk_time),0.f,1.f), remaining=(1-t)*(1-t);
  return {(w.x-w.tx)*remaining,(w.y-w.ty)*remaining};
 }
 void draw(ImDrawList *draw,const json &view,ImVec2 origin,float cw,float ch,double now) const {
  const int ox=view.value("x",0),oy=view.value("y",0);
  for(const auto &r:ripples) {
   const float age=float((now-r.born)/ripple_time), radius=age*3.8f;
   for(const auto &tile:r.tiles) {
    const int x=tile[0],y=tile[1],vx=x-ox,vy=y-oy;
    if(vx<0 || vy<0 || vx>=view.value("width",0) || vy>=view.value("height",0)) continue;
    if(!view["cells"][vy][vx][10].get<int>()) continue;
    const float distance=std::sqrt(float((x-r.x)*(x-r.x)+(y-r.y)*(y-r.y)));
    const float a=std::max(0.f,1.f-std::abs(distance-radius)/.85f)*(1-age)*.30f;
    if(a<=0) continue;
    const ImVec2 p(origin.x+vx*cw,origin.y+vy*ch);
    draw->AddRectFilled(p,{p.x+cw,p.y+ch},ImGui::GetColorU32(ImVec4(.65f,.83f,1,a)),2);
   }
  }
 }
};
