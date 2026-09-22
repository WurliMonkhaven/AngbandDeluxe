# Small, guarded adaptation of the pinned upstream SDL GPU backend. Generate a
# build-tree copy; never modify fetched sources or duplicate the entire backend.
file(READ "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp" gpu_backend)
function(gpu_replace old new)
    string(FIND "${gpu_backend}" "${old}" location)
    if(location EQUAL -1)
        message(FATAL_ERROR "Pinned ImGui GPU backend changed; review imgui_gpu_patch.cmake")
    endif()
    string(REPLACE "${old}" "${new}" gpu_backend "${gpu_backend}")
    set(gpu_backend "${gpu_backend}" PARENT_SCOPE)
endfunction()
gpu_replace("#include \"imgui_impl_sdlgpu3_shaders.h\"" [=[#include "imgui_impl_sdlgpu3_shaders.h"
#include "imgui_gpu_range.h"
static DeluxeGpuUploadStats deluxe_upload_stats;
DeluxeGpuUploadStats DeluxeGpuGetUploadStats() { return deluxe_upload_stats; }
]=])
gpu_replace([=[    // FIXME-OPT: Not optimal, but this is fairly rarely called.
    SDL_WaitForGPUIdle(v->Device);]=] [=[    // SDL defers destruction of resources still referenced by GPU commands.
    // Reserve headroom instead of flushing the GPU for small UI size changes.
    new_size = ((new_size + new_size / 4 + 65535u) / 65536u) * 65536u;
    ++deluxe_upload_stats.buffer_growths;]=])
gpu_replace("    uint32_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);" [=[    ++deluxe_upload_stats.uploads;
    uint32_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);]=])
gpu_replace("void ImGui_ImplSDLGPU3_RenderDrawData(ImDrawData* draw_data, SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass, SDL_GPUGraphicsPipeline* pipeline)" [=[void ImGui_ImplSDLGPU3_RenderDrawData(ImDrawData* draw_data, SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass, SDL_GPUGraphicsPipeline* pipeline)
{
    DeluxeGpuRenderRange(draw_data, command_buffer, render_pass, 0, draw_data->CmdListsCount, pipeline);
}

void DeluxeGpuRenderRange(ImDrawData* draw_data, SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* render_pass, int first, int last, SDL_GPUGraphicsPipeline* pipeline)]=])
gpu_replace([=[    int global_idx_offset = 0;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        for (int cmd_i]=] [=[    int global_idx_offset = 0;
    int list_index = 0;
    for (const ImDrawList* draw_list : draw_data->CmdLists)
    {
        if (list_index >= last) break;
        if (list_index++ < first)
        {
            global_idx_offset += draw_list->IdxBuffer.Size;
            global_vtx_offset += draw_list->VtxBuffer.Size;
            continue;
        }
        for (int cmd_i]=])
set(gpu_backend_file "${CMAKE_CURRENT_BINARY_DIR}/imgui_impl_sdlgpu3_deluxe.cpp")
file(CONFIGURE OUTPUT "${gpu_backend_file}" CONTENT "${gpu_backend}" @ONLY)
