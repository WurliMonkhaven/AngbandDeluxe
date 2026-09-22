#pragma once
#include "crt_settings.h"
#include <SDL3/SDL.h>
#include <string>

struct CrtFrame {
 int scope=0; // Off, game only, full
 CrtSettings settings;
 ImDrawList *game=nullptr;
 ImVec2 game_pos{},game_size{};
 double seconds=0;
 float health_glitch=0; // Visual intensity only; gameplay remains in the engine.
 float ui_scale=1;
 std::string session;
};

// Owns all CRT GPU resources. No engine state, input processing or settings UI.
// Call shutdown before destroying the SDL GPU device.
class CrtRenderer {
public:
 CrtRenderer()=default;
 ~CrtRenderer() { shutdown(); }
 CrtRenderer(const CrtRenderer&)=delete;
 CrtRenderer& operator=(const CrtRenderer&)=delete;
 bool initialize(SDL_GPUDevice*,SDL_GPUTextureFormat);
 void shutdown();
 void reset_history() { history_valid_=false; }
 bool ready() const { return composite_!=nullptr && error_.empty(); }
 const std::string &error() const { return error_; }
 void render(SDL_GPUCommandBuffer*,SDL_GPUTexture *destination,Uint32 width,Uint32 height,ImDrawData*,const CrtFrame&);
private:
 bool resize(Uint32,Uint32);
 bool ensure_target(int index);
 void release_targets();
 void draw_ui(SDL_GPUCommandBuffer*,SDL_GPUTexture*,ImDrawData*,bool clear,int first=0,int last=-1);
 void draw_pass(SDL_GPUCommandBuffer*,SDL_GPUTexture*,SDL_GPUGraphicsPipeline*,
                SDL_GPUTexture **inputs,unsigned count,const void *uniforms,unsigned bytes);
 SDL_GPUDevice *device_=nullptr;
 SDL_GPUTextureFormat format_{};
 SDL_GPUGraphicsPipeline *blur_=nullptr,*composite_=nullptr;
 SDL_GPUSampler *sampler_=nullptr;
 std::array<SDL_GPUTexture*,7> targets_{}; // scene, glow pair, bloom pair, history pair
 Uint32 width_=0,height_=0;
 int previous_=0;
 bool history_valid_=false;
 double last_time_=0;
 CrtSettings history_settings_;
 std::array<float,8> history_geometry_{};
 std::string history_session_,error_;
};
