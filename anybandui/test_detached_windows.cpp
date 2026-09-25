// Hidden native-window smoke test. Does not connect to a game or show windows.
#include "client.cpp"
#include <iostream>
static void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char **argv) {
 try {
  require(argc==2,"Pass a captured state fixture");
  require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
  auto *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL,true,"direct3d12"); require(gpu,SDL_GetError());
  auto *main=SDL_CreateWindow("Detached test host",800,600,SDL_WINDOW_HIDDEN); require(main,SDL_GetError());
  ImGui::CreateContext(); auto *root=ImGui::GetCurrentContext(); AnybandUITheme::apply();
  json fixture; std::ifstream(argv[1])>>fixture;
  Connection c; c.connected=c.negotiated=true; c.receive({{"kind","event"},{"event","state.changed"},{"data",fixture.at("state")}});
  UI ui{c}; ui.base_style=ImGui::GetStyle(); ui.layout.native_windows_available=true;
  DetachedPanels manager(gpu,main,ANYBANDUI_FONTS_DIR,true);
  for(int p=0;p<WorkspaceLayout::Count;++p) if(WorkspaceLayout::detachable(p)) ui.layout.detach(p,true);
  auto serialized=ui.layout.serialize(); WorkspaceLayout restored; restored.load(serialized);
  for(int p=0;p<WorkspaceLayout::Count;++p) if(WorkspaceLayout::detachable(p)) require(restored.detached[p].open && restored.contains(p),"Detached layout lost its docked position");
  ui.crt=2;
  for(int frame=0;frame<4;++frame) manager.draw(ui);
  require(ImGui::GetCurrentContext()==root,"Detached draw leaked its context");
  for(int p=0;p<WorkspaceLayout::Count;++p) if(WorkspaceLayout::detachable(p)) {
   require(bool(manager.windows[p]),"Detached window failed to open"); auto &w=*manager.windows[p];
   if(p==WorkspaceLayout::TrackedCreature || p==WorkspaceLayout::Character || p==WorkspaceLayout::Status || p==WorkspaceLayout::DungeonDetails) {
    int low,high; SDL_GetWindowMinimumSize(w.window,nullptr,&low); SDL_GetWindowMaximumSize(w.window,nullptr,&high);
    require(low>0 && low==high,"Detached compact panels must have a fixed height");
   }
   require(w.frame.scope==2,"Full-window CRT must include detached panels");
   ui.crt=1; require(DetachedPanels::crt_frame(ui,w,1).scope==0,"Game-only CRT must exclude utility panels");
   ui.crt=3; require(DetachedPanels::crt_frame(ui,w,1).scope==0,"Main-window-only CRT must exclude detached panels");
   ui.crt=0; require(DetachedPanels::crt_frame(ui,w,1).scope==0,"CRT Off must exclude detached panels");
   ui.crt=2;
   const ImVec2 sample(50,60); const auto mapped=DetachedPanels::mouse_position(ui,w,sample);
   int ww,hh; SDL_GetWindowSize(w.window,&ww,&hh);
   const auto expected=CrtCurve({0,0},{float(ww),float(hh)},ui.crt_settings).map(sample,true);
   require(std::abs(mapped.x-expected.x)<.001f && std::abs(mapped.y-expected.y)<.001f,"Detached cursor must use its own CRT geometry");
   SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.windowID=SDL_GetWindowID(w.window); event.key.key=SDLK_6;
   require(manager.event(ui,event),"Detached key was not consumed"); require(ui.keys.empty(),"Detached key reached gameplay");
   ImGui::SetCurrentContext(w.context);
   require(ImGui::GetDrawData()->TotalVtxCount>0,"Detached panel produced no geometry");
   SDL_GPUTextureCreateInfo texture{}; texture.type=SDL_GPU_TEXTURETYPE_2D; texture.format=SDL_GetGPUSwapchainTextureFormat(gpu,w.window);
   texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER; texture.width=560; texture.height=640; texture.layer_count_or_depth=texture.num_levels=1;
   auto *target=SDL_CreateGPUTexture(gpu,&texture); require(target,SDL_GetError());
   auto *cmd=SDL_AcquireGPUCommandBuffer(gpu); require(cmd,SDL_GetError());
   w.renderer.render(cmd,target,560,640,ImGui::GetDrawData(),w.frame); SDL_SubmitGPUCommandBuffer(cmd); SDL_WaitForGPUIdle(gpu); SDL_ReleaseGPUTexture(gpu,target);
   require(w.renderer.error().empty(),"Detached CRT rendering failed");
   ImGui::SetCurrentContext(root);
  }
  SDL_Event close{}; close.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED; close.window.windowID=SDL_GetWindowID(manager.windows[WorkspaceLayout::Inventory]->window);
  require(manager.event(ui,close),"Native close was not handled"); manager.draw(ui);
  require(!manager.windows[WorkspaceLayout::Inventory] && !ui.layout.detached[WorkspaceLayout::Inventory].open && ui.layout.contains(WorkspaceLayout::Inventory),"Closing did not redock inventory");
  ui.layout.detach(WorkspaceLayout::Inventory,true); manager.draw(ui); require(bool(manager.windows[WorkspaceLayout::Inventory]),"Reopening failed");
  WorkspaceLayout::Detached offscreen; offscreen.placed=true; offscreen.x=-99999; offscreen.y=-99999;
  auto recovered=DetachedPanels::recover(offscreen); require(recovered.x!=-99999 && recovered.y!=-99999,"Missing monitor recovery failed");
  c.state["phase"]="launcher"; manager.draw(ui);
  for(const auto &w:manager.windows) require(!w,"Session end left a detached window alive");
  manager.shutdown(); ImGui::DestroyContext(); SDL_DestroyWindow(main); SDL_DestroyGPUDevice(gpu); SDL_Quit();
  std::cout<<"Detached window rendering, input isolation, layout persistence, close/redock and recovery checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
