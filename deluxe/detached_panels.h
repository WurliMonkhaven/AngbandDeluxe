#pragma once
// Each native utility window owns its ImGui/platform/GPU state. UI and Connection
// remain shared, so actions use the same backend and prompts stay in the main window.
struct DetachedPanels {
 struct Window {
  SDL_Window *window=nullptr; ImGuiContext *context=nullptr;
  FontLibrary fonts; CrtRenderer renderer; HealthGlitch health_glitch; CrtFrame frame;
  std::string renderer_error;
  bool platform=false,gpu_backend=false,claimed=false;
 };
 SDL_GPUDevice *gpu; SDL_Window *main_window; ImGuiContext *main_context;
 fs::path font_folder; bool hidden=false,geometry_pending=false; Uint64 geometry_changed=0;
 std::array<std::unique_ptr<Window>,WorkspaceLayout::Count> windows;
 DetachedPanels(SDL_GPUDevice *device,SDL_Window *main,const fs::path &fonts,bool test_hidden=false):gpu(device),main_window(main),main_context(ImGui::GetCurrentContext()),font_folder(fonts),hidden(test_hidden) {}
 void close(int p) {
  if(!windows[p]) return;
  auto &w=*windows[p]; SDL_WaitForGPUIdle(gpu);
  if(w.context) {
   ImGui::SetCurrentContext(w.context); w.renderer.shutdown();
   if(w.gpu_backend) ImGui_ImplSDLGPU3_Shutdown();
   if(w.platform) ImGui_ImplSDL3_Shutdown();
   ImGui::DestroyContext(w.context);
  }
  if(w.claimed) SDL_ReleaseWindowFromGPUDevice(gpu,w.window);
  if(w.window) SDL_DestroyWindow(w.window);
  windows[p].reset(); ImGui::SetCurrentContext(main_context);
 }
 bool focused() const {
  auto *window=SDL_GetKeyboardFocus(); if(window==main_window) return true;
  for(const auto &w:windows) if(w && w->window==window) return true;
  return false;
 }
 void shutdown() { for(int p=0;p<WorkspaceLayout::Count;++p) close(p); }
 static SDL_Rect recover(WorkspaceLayout::Detached d) {
  SDL_Rect bounds{}; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(),&bounds);
  int count=0; auto *displays=SDL_GetDisplays(&count);
  bool found=false;
  if(d.placed) for(int i=0;i<count;++i) {
   SDL_Rect candidate{}; if(!SDL_GetDisplayUsableBounds(displays[i],&candidate)) continue;
   if(d.x>=candidate.x && d.x<candidate.x+candidate.w && d.y>=candidate.y && d.y<candidate.y+candidate.h) { bounds=candidate; found=true; break; }
  }
  SDL_free(displays);
  if(bounds.w<=0 || bounds.h<=0) bounds={0,0,1280,720};
  d.w=std::clamp(d.w,std::min(280,bounds.w),bounds.w); d.h=std::clamp(d.h,std::min(180,bounds.h),bounds.h);
  if(!found) { d.x=bounds.x+(bounds.w-d.w)/2; d.y=bounds.y+(bounds.h-d.h)/2; }
  return {std::clamp(d.x,bounds.x,bounds.x+bounds.w-d.w),std::clamp(d.y,bounds.y,bounds.y+bounds.h-d.h),d.w,d.h};
 }
 bool open(UI &ui,int p) {
  windows[p]=std::make_unique<Window>(); auto &w=*windows[p];
  const auto rect=recover(ui.layout.detached[p]);
  const std::string title=std::string("Angband Deluxe - ")+WorkspaceLayout::names[p];
  w.window=SDL_CreateWindow(title.c_str(),rect.w,rect.h,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_HIDDEN);
  if(!w.window) { close(p); return false; }
  SDL_SetWindowPosition(w.window,rect.x,rect.y); SDL_SetWindowMinimumSize(w.window,280,180);
  w.claimed=SDL_ClaimWindowForGPUDevice(gpu,w.window);
  if(!w.claimed) { close(p); return false; }
  // Main window supplies pacing; secondary swapchains must not add another wait.
  auto mode=SDL_WindowSupportsGPUPresentMode(gpu,w.window,SDL_GPU_PRESENTMODE_IMMEDIATE)?SDL_GPU_PRESENTMODE_IMMEDIATE:SDL_GPU_PRESENTMODE_VSYNC;
  SDL_SetGPUSwapchainParameters(gpu,w.window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,mode);
  w.context=ImGui::CreateContext(); ImGui::SetCurrentContext(w.context);
  ImGui::GetIO().IniFilename=nullptr; ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
  DeluxeTheme::apply(); w.fonts.load(font_folder);
  w.platform=ImGui_ImplSDL3_InitForSDLGPU(w.window);
  ImGui_ImplSDLGPU3_InitInfo info{}; info.Device=gpu; info.ColorTargetFormat=SDL_GetGPUSwapchainTextureFormat(gpu,w.window); info.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
  w.gpu_backend=ImGui_ImplSDLGPU3_Init(&info);
  if(!w.platform || !w.gpu_backend || !w.renderer.initialize(gpu,info.ColorTargetFormat)) { close(p); return false; }
  ImGui::SetCurrentContext(main_context); if(!hidden) SDL_ShowWindow(w.window); remember(ui,p); return true;
 }
 void remember(UI &ui,int p) {
  auto &d=ui.layout.detached[p]; auto previous=d;
  SDL_GetWindowPosition(windows[p]->window,&d.x,&d.y); SDL_GetWindowSize(windows[p]->window,&d.w,&d.h); d.placed=true;
  if(d.x!=previous.x || d.y!=previous.y || d.w!=previous.w || d.h!=previous.h || !previous.placed) { geometry_pending=true; geometry_changed=SDL_GetTicks(); }
 }
 static ImVec2 mouse_position(const UI &ui,const Window &w,ImVec2 point) {
  if(ui.crt!=2 || !w.renderer.ready()) return point;
  int width=0,height=0; SDL_GetWindowSize(w.window,&width,&height);
  if(point.x<0 || point.y<0 || point.x>width || point.y>height) return point;
  return CrtCurve({0,0},{float(width),float(height)},ui.crt_settings).map(point,true);
 }
 static CrtFrame crt_frame(UI &ui,Window &w,float dpi) {
  CrtFrame frame; frame.scope=ui.crt==2?2:0; frame.settings=ui.crt_settings;
  frame.seconds=double(SDL_GetTicksNS())/1e9; frame.ui_scale=ui.scale*dpi;
  frame.session=ui.c.state.value("phase","");
  frame.health_glitch=w.health_glitch.update(ui.c.state,frame.seconds,ui.low_animation,false);
  frame.desaturation=ui.scene_animation?ui.c.transitions.desaturation(frame.seconds):0;
  return frame;
 }
 bool event(UI &ui,const SDL_Event &event) {
  auto *target=SDL_GetWindowFromEvent(&event); if(!target || target==main_window) return false;
  for(int p=0;p<WorkspaceLayout::Count;++p) if(windows[p] && windows[p]->window==target) {
   if(event.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED) { remember(ui,p); ui.layout.detach(p,false); return true; }
   if(event.type==SDL_EVENT_WINDOW_FOCUS_GAINED) { ui.grid_focus=false; ui.keys.clear(); }
   if(event.type==SDL_EVENT_WINDOW_MOVED || event.type==SDL_EVENT_WINDOW_RESIZED) {
    if(!(SDL_GetWindowFlags(target)&(SDL_WINDOW_MINIMIZED|SDL_WINDOW_MAXIMIZED))) remember(ui,p);
   }
   SDL_Event mapped=event;
   if(mapped.type==SDL_EVENT_MOUSE_MOTION) {
    const auto point=mouse_position(ui,*windows[p],{mapped.motion.x,mapped.motion.y}); mapped.motion.x=point.x; mapped.motion.y=point.y;
   }
   ImGui::SetCurrentContext(windows[p]->context); ImGui_ImplSDL3_ProcessEvent(&mapped); ImGui::SetCurrentContext(main_context);
   return true; // Never send detached text/key events to gameplay.
  }
  return false;
 }
 void draw(UI &ui) {
  if(geometry_pending && SDL_GetTicks()-geometry_changed>=250) { ui.layout.dirty=true; geometry_pending=false; }
  const bool playing=ui.c.state.value("phase","")=="playing" && ui.c.run_report.is_null();
  const bool modal=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
  for(int p=0;p<WorkspaceLayout::Count;++p) {
   const bool wanted=playing && ui.layout.detached[p].open && ui.layout.contains(p);
   if(!wanted) { close(p); continue; }
   if(!windows[p] && !open(ui,p)) { ui.layout.detach(p,false); ui.c.notice("Could not create the detached panel window. The panel has been returned to the main layout."); continue; }
   auto &w=*windows[p];
   if(SDL_GetWindowFlags(w.window)&SDL_WINDOW_MINIMIZED) continue;
   ImGui::SetCurrentContext(w.context);
   ImGui::GetStyle()=ui.base_style; DeluxeTheme::configure(ui.theme_settings);
   const float dpi=std::max(.5f,SDL_GetWindowDisplayScale(w.window));
   ImGui::GetStyle().ScaleAllSizes(dpi*ui.scale); ImGui::GetStyle().FontScaleDpi=dpi; ImGui::GetStyle().FontScaleMain=ui.scale;
   ImGui::GetIO().FontDefault=w.fonts.get(ui.font_settings.interface_font);
   ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
   if(ui.crt==2 && SDL_GetMouseFocus()==w.window) {
    float x,y; SDL_GetMouseState(&x,&y); const auto point=mouse_position(ui,w,{x,y}); ImGui::GetIO().AddMousePosEvent(point.x,point.y);
   }
   ImGui::NewFrame();
   if(p==WorkspaceLayout::TrackedCreature) {
    const auto &style=ImGui::GetStyle();
    const float pixels=CharacterOverview::tracked_height(ui.layout.heading(WorkspaceLayout::TrackedCreature))+4*style.WindowPadding.y+ImGui::GetFrameHeightWithSpacing()+1+style.ItemSpacing.y;
    int ww,hh; SDL_GetWindowSize(w.window,&ww,&hh);
    const int fitted=std::max(1,int(std::ceil(pixels*hh/std::max(1.f,ImGui::GetIO().DisplaySize.y))));
    int min_height,max_height;
    SDL_GetWindowMinimumSize(w.window,nullptr,&min_height); SDL_GetWindowMaximumSize(w.window,nullptr,&max_height);
    if(min_height!=fitted || max_height!=fitted) {
     SDL_SetWindowMaximumSize(w.window,0,0);
     SDL_SetWindowMinimumSize(w.window,280,fitted);
     SDL_SetWindowMaximumSize(w.window,0,fitted);
    }
    if(hh!=fitted) SDL_SetWindowSize(w.window,ww,fitted);
   }
   ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
   ImGui::Begin("Detached panel",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
   if(ImGui::SmallButton("Return to main window")) ui.layout.detach(p,false);
   ImGui::Separator();
   const auto before=ui.c.outgoing.size(); const bool details=ui.open_character_sheet,history=ui.message_history.open,inscribe=ui.item_rules_panel.auto_open,study=ui.select_spells_tab;
   auto *old_fonts=ui.font_library; ui.font_library=&w.fonts;
   ImGui::BeginChild("Panel content");
   ImGui::BeginDisabled(modal || ui.layout.editing || ui.c.state.contains("store"));
   ui.workspace_panel(p);
   ImGui::EndDisabled(); ImGui::EndChild(); ui.font_library=old_fonts;
   ImGui::End(); ImGui::Render();
   w.frame=crt_frame(ui,w,dpi);
   auto *cmd=SDL_AcquireGPUCommandBuffer(gpu); SDL_GPUTexture *surface=nullptr; Uint32 width=0,height=0;
   if(cmd) {
    if(SDL_AcquireGPUSwapchainTexture(cmd,w.window,&surface,&width,&height)) {
     if(surface) w.renderer.render(cmd,surface,width,height,ImGui::GetDrawData(),w.frame);
     SDL_SubmitGPUCommandBuffer(cmd);
    } else SDL_CancelGPUCommandBuffer(cmd);
   }
   ImGui::SetCurrentContext(main_context);
   if(w.renderer_error!=w.renderer.error()) {
    w.renderer_error=w.renderer.error(); if(!w.renderer_error.empty()) ui.c.notice(std::string(WorkspaceLayout::names[p])+": "+w.renderer_error);
   }
   if(ui.open_character_sheet!=details || ui.message_history.open!=history || ui.item_rules_panel.auto_open!=inscribe || ui.select_spells_tab!=study || (ui.c.outgoing.size()!=before && ui.c.busy)) {
    if(!hidden) { SDL_RestoreWindow(main_window); SDL_RaiseWindow(main_window); } ui.focus_requested=true;
   }
  }
  ImGui::SetCurrentContext(main_context); DeluxeTheme::current=ui.theme_settings;
 }
};
