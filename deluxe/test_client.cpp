// Headless checks: no native window, GPU or computer-control automation.
#include "client.cpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char **argv) {
 try {
  check(argc==2,"Pass an unused settings-file path");
  const fs::path path=argv[1];
  check(!fs::exists(path) && !fs::exists(path.string()+".tmp"),"Test file already exists");
  Connection connection; UI ui{connection}; ui.settings_path=path.string();
  // Legacy preferences acquire sensible defaults.
  { std::ofstream out(path); out<<R"({"scale":1.25,"game_fraction":0.65})"; }
  ui.load_settings(); check(ui.scale==1.25f && !ui.fullscreen && ui.crt==0,"Legacy settings");
  check(ui.crt_strength==1,"Existing settings should default to Classic");
  ui.begin_settings(); ui.draft_scale=1.5f; ui.draft_crt=2; ui.draft_fullscreen=true;
  ui.draft_crt_strength=3; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(!ui.crt_settings.parts[Hum].enabled,"Hum bar draft applied immediately");
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_crt_settings.parts[Hum].enabled && !ui.crt_settings.parts[Hum].enabled,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.scale==1.5f && loaded.crt==1 && !loaded.fullscreen,"Saved values");
  check(loaded.crt_settings.parts[Hum].enabled,"Hum bar did not persist");
  loaded.begin_settings(); loaded.draft_crt=2; loaded.draft_crt_settings.parts[Hum].enabled=false;
  check(loaded.apply_settings(nullptr),"Replace settings file");
  ui.load_settings(); check(!ui.crt_settings.parts[Hum].enabled,"Hum bar off did not persist"); check(ui.crt==2,"Full CRT persistence");
  for(int strength=0;strength<4;++strength) {
   loaded.begin_settings(); loaded.draft_crt_strength=strength; loaded.draft_crt_settings=CrtSettings(strength);
   check(loaded.apply_settings(nullptr),"Save CRT strength");
   ui.load_settings(); check(ui.crt_strength==strength,"Strength did not persist");
  }
  loaded.begin_settings(); loaded.draft_crt_strength=-1;
  for(int i=0;i<CrtPartCount;++i) loaded.draft_crt_settings.parts[i]={i%2==0,13.f+i*7.f};
  check(loaded.apply_settings(nullptr),"Save custom components"); ui.load_settings();
  check(ui.crt_strength==-1 && ui.crt_settings.serialize()==loaded.crt_settings.serialize(),"Custom components did not persist");
  ui.begin_settings(); ui.draft_crt_settings=CrtSettings(3); ui.begin_settings();
  check(ui.draft_crt_settings.serialize()==ui.crt_settings.serialize(),"Cancel retained component changes");
  loaded.settings_path=(path/"missing"/"settings.json").string();
  loaded.begin_settings(); loaded.draft_scale=.75f;
  check(!loaded.apply_settings(nullptr) && loaded.scale==1.5f,"Failed save changed active settings");
  check(crt_hum_trail(-.0001f)==0 && crt_hum_trail(0)==1,"Hum leading edge is not sharp");
  check(crt_hum_trail(.02f)>crt_hum_trail(.1f) && crt_hum_trail(.24f)==0,"Hum trailing edge does not fade");
  // Exercise draw-list generation and clip bounds without rendering a window.
  ImGui::CreateContext(); auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(1600,1000); io.DeltaTime=1.f/60;
  io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
  unsigned char *pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  crt_init_halo();
  ImGui::NewFrame(); auto *draw=ImGui::GetForegroundDrawList();
  crt_effect(draw,ImVec2(20,30),ImVec2(400,300));
  check(draw->VtxBuffer.Size>0,"CRT effect emitted no geometry");
  for(const auto &v:draw->VtxBuffer) check(v.pos.x>=20 && v.pos.x<=420 && v.pos.y>=30 && v.pos.y<=330,"CRT escaped selected surface");
  auto plain=crt_phosphor(*draw);
  check(plain->IdxBuffer.Size==draw->IdxBuffer.Size,"Solid backgrounds acquired a halo");
  plain.reset();
  draw->PushClipRect(ImVec2(20,30),ImVec2(1400,950));
  const std::string row(80,'@');
  for(int y=0;y<34;++y) draw->AddText(ImVec2(30,40+y*20.f),IM_COL32_WHITE,row.c_str());
  draw->PopClipRect();
  const auto start=std::chrono::steady_clock::now();
  auto glowing=crt_phosphor(*draw);
  std::cout<<"CRT geometry for 2,720 glyphs: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms\n";
  check(glowing->IdxBuffer.Size>draw->IdxBuffer.Size,"Missing text glow geometry");
  bool red=false,cyan=false;
  for(const auto &v:glowing->VtxBuffer) {
   red|=(v.col&~IM_COL32_A_MASK)==(IM_COL32(255,0,0,0));
   cyan|=(v.col&~IM_COL32_A_MASK)==(IM_COL32(0,255,255,0));
  }
  check(red&&cyan,"Missing chromatic fringes");
  for(const auto &command:glowing->CmdBuffer) {
   check(command.IdxOffset+command.ElemCount<=unsigned(glowing->IdxBuffer.Size),"Invalid draw range");
   for(unsigned i=0;i<command.ElemCount;++i)
    check(command.VtxOffset+glowing->IdxBuffer[command.IdxOffset+i]<unsigned(glowing->VtxBuffer.Size),"Invalid vertex offset");
  }
  const auto curve_start=std::chrono::steady_clock::now();
  auto curved=crt_curve(*glowing,CrtCurve(ImVec2(0,0),io.DisplaySize,3));
  std::cout<<"Barrel geometry: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-curve_start).count()<<" ms\n";
  for(const auto &cmd:curved->CmdBuffer) for(unsigned i=0;i<cmd.ElemCount;++i)
   check(cmd.VtxOffset+curved->IdxBuffer[cmd.IdxOffset+i]<unsigned(curved->VtxBuffer.Size),"Curved index overflow");
  curved.reset(); glowing.reset();
  {
  ImDrawList single(ImGui::GetDrawListSharedData()); single._ResetForNewFrame(); single.Flags=draw->Flags;
  single.PushClipRect(ImVec2(0,0),ImVec2(200,200)); single.PushTexture(io.Fonts->TexRef);
  single.AddText(ImVec2(50,50),IM_COL32_WHITE,"H");
  single.PopTexture(); single.PopClipRect();
  check(single.IdxBuffer.Size==6,"Expected one glyph quad");
  CrtSettings disabled;
  for(auto &part:disabled.parts) part.enabled=false;
  auto no_glow=crt_phosphor(single,disabled);
  check(no_glow->IdxBuffer.Size==single.IdxBuffer.Size,"Disabled components still add text effects");
  CrtCurve flat(ImVec2(0,0),ImVec2(400,300),disabled);
  check(flat.map(ImVec2(10,10)).x==10 && flat.map(ImVec2(10,10)).y==10,"Disabled barrel still curves");
  ImDrawList no_overlay(ImGui::GetDrawListSharedData()); no_overlay._ResetForNewFrame();
  crt_effect(&no_overlay,ImVec2(20,30),ImVec2(400,300),disabled,6);
  check(no_overlay.VtxBuffer.Size==0,"Disabled overlays still draw");
  for(int part=0;part<CrtPartCount;++part) {
   auto isolated=disabled; isolated.parts[part]={true,100};
   for(int other=0;other<CrtPartCount;++other) check(isolated.level(other)==(other==part?1.f:0.f),"Components are coupled");
   if(part==Scanlines || part==Edges || part==Hum) {
    ImDrawList overlay(ImGui::GetDrawListSharedData()); overlay._ResetForNewFrame();
    overlay.PushClipRect(ImVec2(20,30),ImVec2(420,330));
    crt_effect(&overlay,ImVec2(20,30),ImVec2(400,300),isolated,6);
    overlay.PopClipRect();
    check(overlay.VtxBuffer.Size>0,"Independent overlay missing");
    if(part==Scanlines) {
     const CrtCurve scan_curve(ImVec2(20,30),ImVec2(400,300),3);
     auto curved_scanlines=crt_curve(overlay,scan_curve);
     bool bent=false;
     for(const auto &v:curved_scanlines->VtxBuffer)
      bent|=std::abs(scan_curve.map(v.pos,true).y-30)<.001f && v.pos.y>30.1f;
     check(bent,"Scanlines were not included in barrel distortion");
     // Sample actual curved triangles at neighbouring pixel centres. A hard
     // stripe jumps by its full alpha; coverage ramps must change gradually.
     auto sample=[&](float x,float y) {
      float alpha=0;
      for(const auto &cmd:curved_scanlines->CmdBuffer) for(unsigned i=0;i+2<cmd.ElemCount;i+=3) {
       const auto &a=curved_scanlines->VtxBuffer[cmd.VtxOffset+curved_scanlines->IdxBuffer[cmd.IdxOffset+i]];
       const auto &b=curved_scanlines->VtxBuffer[cmd.VtxOffset+curved_scanlines->IdxBuffer[cmd.IdxOffset+i+1]];
       const auto &c=curved_scanlines->VtxBuffer[cmd.VtxOffset+curved_scanlines->IdxBuffer[cmd.IdxOffset+i+2]];
       const float det=(b.pos.y-c.pos.y)*(a.pos.x-c.pos.x)+(c.pos.x-b.pos.x)*(a.pos.y-c.pos.y);
       if(std::abs(det)<1e-8f) continue;
       const float u=((b.pos.y-c.pos.y)*(x-c.pos.x)+(c.pos.x-b.pos.x)*(y-c.pos.y))/det;
       const float v=((c.pos.y-a.pos.y)*(x-c.pos.x)+(a.pos.x-c.pos.x)*(y-c.pos.y))/det;
       const float w=1-u-v;
       if(u>=0 && v>=0 && w>=0) alpha=std::max(alpha,(u*(a.col>>IM_COL32_A_SHIFT)+v*(b.col>>IM_COL32_A_SHIFT)+w*(c.col>>IM_COL32_A_SHIFT))/255.f);
      }
      return alpha;
     };
     float last=sample(30.5f,33.5f),maximum_jump=0; bool partial=false;
     for(int x=31;x<410;++x) {
      const float current=sample(x+.5f,33.5f);
      maximum_jump=std::max(maximum_jump,std::abs(current-last)); last=current;
      partial|=current>.05f && current<.45f;
     }
     check(partial && maximum_jump<.08f,"Curved scanline edges still jump abruptly between pixels");
    }
    if(part==Hum) for(const auto &v:overlay.VtxBuffer)
     check(v.pos.y<=180.001f,"Hum glow extends ahead of its downward-facing edge");
   }
   if(part==Glow || part==Bloom || part==Fringe) {
    auto effect=crt_phosphor(single,isolated);
    check(effect->IdxBuffer.Size>single.IdxBuffer.Size,"Independent text effect missing");
   }
  }
  auto glow_only=disabled,bloom_only=disabled,ghost_only=disabled;
  glow_only.parts[Glow]={true,100}; bloom_only.parts[Bloom]={true,100}; ghost_only.parts[Ghost]={true,100};
  auto halo=crt_phosphor(single,glow_only),bloom=crt_phosphor(single,bloom_only),ghost=crt_phosphor(single,ghost_only);
  auto extent=[](const ImDrawList &list) {
   float top=1e6f,bottom=-1e6f;
   for(const auto &v:list.VtxBuffer) { top=std::min(top,v.pos.y); bottom=std::max(bottom,v.pos.y); }
   return bottom-top;
  };
  check(extent(*halo)>extent(single)+8,"Glow is still confined to the strokes");
  check(extent(*bloom)>extent(*halo)+8,"Bloom is not more diffuse than glow");
  // Probe the actual halo texture outside a glyph, where the sharp core cannot
  // hide the light. Geometry-only checks missed the overly concentrated halo.
  const auto glow_parameters=glow_only.resolved();
  float glyph_left=single.VtxBuffer[0].pos.x,glyph_right=glyph_left;
  float glyph_ymin=single.VtxBuffer[0].pos.y,glyph_ymax=glyph_ymin;
  for(const auto &v:single.VtxBuffer) {
   glyph_left=std::min(glyph_left,v.pos.x); glyph_right=std::max(glyph_right,v.pos.x);
   glyph_ymin=std::min(glyph_ymin,v.pos.y); glyph_ymax=std::max(glyph_ymax,v.pos.y);
  }
  const float half_width=(glyph_right-glyph_left)*.5f;
  const float glow_scale=std::max(1.f,(glyph_ymax-glyph_ymin)/16.f);
  const float outside=(half_width+2)/(half_width+glow_parameters.glow_radius*glow_scale);
  check(glow_parameters.glow_alpha*crt_halo_alpha(outside,0)>.12f,"Maximum glow is invisible outside glyphs");
  check(crt_halo_alpha(0,0)==1 && crt_halo_alpha(1,0)==0 && crt_halo_alpha(.2f,0)>crt_halo_alpha(.7f,0),"Halo lacks smooth falloff to transparent edges");
  ImFontAtlasRect halo_rect; check(io.Fonts->GetCustomRect(crt_halo_rect,&halo_rect),"Missing halo texture");
  auto *edge_pixel=static_cast<unsigned char*>(io.Fonts->TexData->GetPixelsAt(halo_rect.x,halo_rect.y));
  check(edge_pixel[io.Fonts->TexData->BytesPerPixel-1]==0,"Halo texture edge is opaque");
  check(ghost->IdxBuffer.Size==single.IdxBuffer.Size,"Ghosting still creates displaced copies");
  for(int i=0;i<6;++i) {
   const auto &original=single.VtxBuffer[single.IdxBuffer[i]],&actual=ghost->VtxBuffer[i];
   check(actual.pos.x==original.pos.x && actual.pos.y==original.pos.y,"Ghosting shifts the live image");
  }
  check(crt_persistence_alpha(0,.016)==0,"Disabled persistence retains history");
  check(crt_persistence_alpha(1,.31)==0,"Stale image survives a long pause");
  check(crt_persistence_alpha(.2f,.016)<crt_persistence_alpha(1,.016),"Persistence slider does not control decay");
  for(float strength:{.1f,.5f,1.f}) {
   const float frame=crt_persistence_alpha(strength,1./60.);
   check(std::abs(frame*frame-crt_persistence_alpha(strength,1./30.))<.00001f,"Persistence depends on frame rate");
   check(crt_persistence_alpha(strength,.1)<.04f,"Afterimage does not fade quickly");
   float unchanged=.7f;
   for(int i=0;i<100;++i) unchanged=.7f*(1-frame)+unchanged*frame;
   check(std::abs(unchanged-.7f)<.00001f,"Static image accumulates brightness");
  }
  for(auto &v:single.VtxBuffer) v.col=IM_COL32(60,60,60,255);
  auto dim_bloom=crt_phosphor(single,bloom_only);
  check(dim_bloom->IdxBuffer.Size==single.IdxBuffer.Size,"Bloom should reject dim text below threshold");
  for(auto &v:single.VtxBuffer) v.col=IM_COL32_WHITE;
  float glyph_top=single.VtxBuffer[0].pos.y,glyph_bottom=glyph_top;
  for(const auto &v:single.VtxBuffer) { glyph_top=std::min(glyph_top,v.pos.y); glyph_bottom=std::max(glyph_bottom,v.pos.y); }
  for(int strength=0;strength<4;++strength) {
   auto readable=crt_phosphor(single,strength);
   for(const auto &v:readable->VtxBuffer) {
    const bool halo_uv=v.uv.x>=halo_rect.uv0.x && v.uv.x<=halo_rect.uv1.x && v.uv.y>=halo_rect.uv0.y && v.uv.y<=halo_rect.uv1.y;
    if(!halo_uv) check(v.pos.y==glyph_top || v.pos.y==glyph_bottom,"Glow introduced vertical glyph copies");
   }
   for(int k=0;k<6;++k) {
    const auto &original=single.VtxBuffer[single.IdxBuffer[k]];
    const auto &core=readable->VtxBuffer[readable->VtxBuffer.Size-6+k];
    check(core.pos.x==original.pos.x && core.pos.y==original.pos.y && core.col==original.col,"Sharp glyph core was not drawn last");
   }
   ImDrawList first(ImGui::GetDrawListSharedData()),later(ImGui::GetDrawListSharedData());
   first._ResetForNewFrame(); later._ResetForNewFrame();
   crt_effect(&first,ImVec2(20,30),ImVec2(400,300),strength,0);
   crt_effect(&later,ImVec2(20,30),ImVec2(400,300),strength,4.5);
   check(first.VtxBuffer.Size==later.VtxBuffer.Size,"Unexpected wave geometry at aligned phases");
   bool changed=false,bright_bar=false;
   for(int i=0;i<first.VtxBuffer.Size;++i) {
    const auto &v=first.VtxBuffer[i];
    check(v.pos.x>=20 && v.pos.x<=420 && v.pos.y>=30 && v.pos.y<=330,"Preset escaped surface");
    changed|=v.col!=later.VtxBuffer[i].col || v.pos.y!=later.VtxBuffer[i].pos.y;
    bright_bar|=(v.col&~IM_COL32_A_MASK)==IM_COL32(255,255,255,0) && ((v.col>>IM_COL32_A_SHIFT)&255)>=5;
   }
   check(changed,"Hum bar did not roll");
   check(bright_bar,"Hum bar cannot lift dark background");
   ImDrawList off(ImGui::GetDrawListSharedData()),off_later(ImGui::GetDrawListSharedData());
   off._ResetForNewFrame(); off_later._ResetForNewFrame();
   CrtSettings disabled_hum(strength); disabled_hum.parts[Hum].enabled=false;
   crt_effect(&off,ImVec2(20,30),ImVec2(400,300),disabled_hum,0);
   crt_effect(&off_later,ImVec2(20,30),ImVec2(400,300),disabled_hum,4.5);
   check(off.VtxBuffer.Size<first.VtxBuffer.Size,"Disabled hum still emitted geometry");
   for(int i=0;i<off.VtxBuffer.Size;++i) check(off.VtxBuffer[i].col==off_later.VtxBuffer[i].col,"Disabled hum still animates");
   CrtCurve curve(ImVec2(20,30),ImVec2(400,300),strength);
   for(int y=0;y<=10;++y) for(int x=0;x<=10;++x) {
    const ImVec2 p(20+x*40.f,30+y*30.f); const auto mapped=curve.map(p),inverse=curve.map(mapped,true);
    check(std::abs(inverse.x-p.x)<.001f && std::abs(inverse.y-p.y)<.001f,"Curved mouse target mismatch");
   }
   const auto corner=curve.map(ImVec2(20,30));
   check(corner.x>20 && corner.y>30,"Missing barrel curvature");
   ImDrawList clipped(ImGui::GetDrawListSharedData()); clipped._ResetForNewFrame(); clipped.Flags=draw->Flags;
   clipped.PushClipRect(ImVec2(40,50),ImVec2(380,310));
   clipped.AddRectFilled(ImVec2(0,0),ImVec2(500,400),IM_COL32_WHITE);
   clipped.PopClipRect();
   auto bowed=crt_curve(clipped,curve);
   check(bowed->IdxBuffer.Size>6,"Large surface was not subdivided for curvature");
   for(const auto &v:bowed->VtxBuffer) {
    const auto p=curve.map(v.pos,true);
    check(p.x>=39.99f && p.x<=380.01f && p.y>=49.99f && p.y<=310.01f,"Curvature leaked past scroll clipping");
   }
   auto curved_single=crt_curve(single,curve);
   for(const auto &v:curved_single->VtxBuffer) {
    const auto original=curve.map(v.pos,true);
    check(original.x>=49.99f && original.y>=49.99f,"Curve introduced extra glyph copies");
   }
   auto preset=crt_phosphor(*draw,strength);
   for(const auto &command:preset->CmdBuffer) for(unsigned i=0;i<command.ElemCount;++i)
    check(command.VtxOffset+preset->IdxBuffer[command.IdxOffset+i]<unsigned(preset->VtxBuffer.Size),"Preset index overflow");
  }
  }
  ImGui::EndFrame(); ImGui::DestroyContext();
  // Match the app's dynamic atlas lifecycle (including a subsequent font size).
  ImGui::CreateContext(); auto &dynamic_io=ImGui::GetIO(); dynamic_io.IniFilename=nullptr;
  dynamic_io.DisplaySize=ImVec2(800,600); dynamic_io.DeltaTime=1.f/60;
  dynamic_io.BackendFlags|=ImGuiBackendFlags_RendererHasTextures;
  dynamic_io.Fonts->AddFontDefault(); crt_init_halo();
  for(float size:{18.f,36.f,72.f}) {
   ImGui::NewFrame(); ImGui::GetFont()->GetFontBaked(size);
   ImFontAtlasRect rect; check(dynamic_io.Fonts->GetCustomRect(crt_halo_rect,&rect),"Atlas lost the halo after font resize");
   auto *center=static_cast<unsigned char*>(dynamic_io.Fonts->TexData->GetPixelsAt(rect.x+32,rect.y+32));
   check(center[dynamic_io.Fonts->TexData->BytesPerPixel-1]>240,"Halo texture is empty after atlas update");
   ImGui::EndFrame();
  }
  ImGui::DestroyContext();
  fs::remove(path);
  std::cout<<"Graphics settings and CRT geometry checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
