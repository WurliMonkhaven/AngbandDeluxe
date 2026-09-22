#pragma once
#include "imgui_impl_sdlgpu3.h"

// Prepare the COMPLETE frame once, then draw ranges against those same buffers.
// Ranges use [first, last) draw-list indices, preserving original vertex offsets.
void DeluxeGpuRenderRange(ImDrawData*,SDL_GPUCommandBuffer*,SDL_GPURenderPass*,int first,int last,SDL_GPUGraphicsPipeline *pipeline=nullptr);
struct DeluxeGpuUploadStats { unsigned long long uploads=0,buffer_growths=0; };
DeluxeGpuUploadStats DeluxeGpuGetUploadStats();
