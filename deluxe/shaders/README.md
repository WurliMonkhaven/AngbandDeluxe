# CRT rendering

The UI still generates ordinary ImGui geometry. CRT rendering no longer copies
glyphs, builds scanline geometry, or subdivides/warps triangles on the CPU.

## Responsibilities

- `crt_settings.h`: stable preference keys, presets, and CPU mouse-coordinate
  mapping. Its inverse barrel formula must match `crt.frag.hlsl`.
- `crt_renderer.h/.cpp`: owns SDL GPU pipelines, sampler, offscreen targets and
  temporal history. The application passes a `CrtFrame`; the renderer does not
  access the backend connection or write preferences.
- `*.hlsl`: GPU rendering algorithms. These are the authoritative shader sources.
- `generated.h`: embedded DXIL, SPIR-V and Metal source, generated from HLSL.
  Normal builds need neither a shader compiler nor a runtime shader file path.
- `test_crt_gpu.cpp`: offscreen rendering/readback checks using actual pixels.
  `test_client.cpp` covers settings transactions and CPU input/decay contracts.

## Pass order

1. Render unmodified ImGui draw lists into a full-resolution scene texture.
2. If enabled, calculate phosphor glow at half resolution and highlight bloom
   at quarter resolution. Each first averages the source pixel area into linear
   light, then uses horizontal and vertical Gaussian passes covering contiguous
   texels. Adjacent Gaussian taps share a bilinear read; the number of taps grows
   with radius instead of stretching a sparse kernel and leaving sampling gaps.
   The three passes reuse two textures. Disabled components skip the work.
3. A full-screen triangle samples the inverse barrel mapping, composes glow,
   bloom and colour fringing, and evaluates scanlines, vignetting and the hum
   wave in **source coordinates**. Consequently scanlines curve with the image.
   Screen-space derivatives smooth scanline coverage without blurring UI text.
   Lighting uses a reversible gamma-2 approximation: scene colours are decoded
   before emission/blur and encoded after composition. Scanlines redistribute
   beam energy; optical glow and bloom are added afterwards, so dark scanlines
   do not cut through the halo. Phosphor glow also boosts the luminous core,
   gated to leave dark backgrounds alone. Overbright colours scale as a whole
   to fit SDR rather than clipping each channel and bleaching their hue.
   Blur targets use 16-bit floating point to preserve the faint halo falloff;
   the scene, history and final output remain in the window's normal format.
   A client-supplied health intensity adds intermittent horizontal line slips
   and colour separation in the same scope. The engine supplies its warning
   threshold and death status; the client ramps a fatal burst down and disables
   it at the tombstone. This adds no render pass or texture allocation.
4. Blend previous output for brief, frame-rate-independent ghosting. Two
   full-resolution history textures alternate so a pass never samples its own
   render target. The CPU supplies elapsed time; the GPU blends the images.
5. Write directly to the destination (or blit the history output when ghosting is enabled). In game-only mode, render later ImGui
   layers on top, so popups and the sidebar retain their original appearance.

Game-only mode splits the draw-list sequence at the game view, rather than
post-processing a rectangle containing already-drawn popups. The blur passes
also respect the selected region. CPU input mapping accounts for the same barrel
curve; there is no per-vertex CPU distortion.

Blur/history targets are allocated only for enabled components and released when
disabled. Targets are reused across frames and replaced on resize. History resets on
resize, CRT settings/scope, display/UI scale, or session transitions. `shutdown`
must run before destroying the SDL GPU device. SDL defers resource destruction
until pending GPU use is complete. If shader setup or target allocation fails,
the client reports a system message and draws the plain UI instead.

## Uploads and frame pacing

The complete ImGui frame is uploaded once, then its game and overlay draw-list
ranges share the same buffers. `imgui_gpu_patch.cmake` generates a narrowly
adapted copy of the pinned upstream backend in the build directory: it adds
range drawing, rounds buffer growth up with spare capacity, and removes the
whole-device idle wait during growth. SDL's deferred resource release keeps
previously submitted buffers alive. Every source replacement is guarded so an
upstream change fails configuration rather than silently dropping the fixes.
`imgui_gpu_range.h` exposes the range API and upload/growth counters for tests.

The client waits for presentation capacity before sampling input and starting
ImGui's frame clock. CRT animation time is sampled from SDL's monotonic clock
after acquisition. This avoids timestamping a frame before a potentially long
presentation wait. History comparisons use typed values rather than allocating
and serializing JSON every frame.

`deluxe-gpu-tests <driver> --bench` runs a 1920x1080 offscreen workload with two
frames in flight and reports CPU recording/GPU-fence-wait percentiles. It also
checks one UI upload per frame and zero buffer growth after warmup. This does
not measure the desktop compositor or real-window presentation cadence.

## Regenerating shaders

Tool versions used:

- Microsoft DirectXShaderCompiler `v1.10.2605.37` (`dxc_2026_08_11.zip`).
- Khronos SPIRV-Cross commit `aa217aeb6c9f0ace7a0ab233b28807edf45eb165` (CLI).

These are development tools, not application/runtime dependencies. They can be
downloaded/built from their official repositories. Run from the repository root:

```text
python deluxe/shaders/build.py --dxc <path-to-dxc> --spirv-cross <path-to-spirv-cross>
```

Commit the HLSL changes and regenerated header together. Add `--check` to verify
that the header matches the sources and tool output. The header includes a source
hash. Resource bindings follow SDL GPU conventions: fragment samplers in space 2,
uniforms in space 3. SPIRV-Cross uses `--msl-decoration-binding` to retain sampler
indices; automatic remapping can silently bind the wrong images. SDL normalizes
the viewport convention, so the vertex shader does not need a Vulkan-only Y flip.

## Validation and limits

Build `deluxe-client-tests` and `deluxe-gpu-tests` with the normal build helper.
Run the client tests with an unused settings-file path and GPU tests with an
optional SDL driver name (`direct3d12`, `vulkan`, or `metal`). GPU tests create
textures and read them back without opening a window or automating the desktop.

Direct3D and Vulkan pixel tests were run on Windows. Metal source generation and
binding layout were checked, but a Mac build/runtime test is still required.
The Gaussian shader effects can differ slightly from the old per-glyph halos;
the preference names, saved values and scope semantics are preserved.
