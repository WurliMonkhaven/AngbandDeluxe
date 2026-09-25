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
2. If enabled, calculate phosphor glow at half resolution, highlight bloom
   and broader glass diffusion at quarter resolution. Each first averages the source pixel area into linear
   light, then uses horizontal and vertical Gaussian passes covering contiguous
   texels. Adjacent Gaussian taps share a bilinear read; the number of taps grows
   with radius instead of stretching a sparse kernel and leaving sampling gaps.
   The three passes reuse two textures. Disabled components skip the work.
3. A full-screen triangle samples the inverse barrel mapping, composes glow,
   bloom, glass diffusion and RGB convergence, and evaluates scanlines, vignetting and the hum
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
   threshold; Low Health Animation gates the intensity and preserves the selected
   CRT scope and staged settings behavior. Fatal glitching is disabled.
   This adds no render pass or texture allocation.
   Phosphor Dots reconstructs staggered triangular RGB groups at a fixed
   four-pixel pitch in tube coordinates. All emitters in a group share an
   area-filtered linear-light image sample, instead of masking unrelated glyph
   pixels per channel. Gaussian spots overlap and are energy-normalized;
   unwrapped derivatives widen their footprint and fade unresolved groups.
   The slider blends from the original beam to resolved RGB groups, retaining
   a small continuous beam contribution for legibility at maximum strength.
   This is an SDR approximation, not a simulation of a particular CRT model.
   A shared virtual raster sets beam spacing, phosphor pitch and optical blur
   scale together. Fixed raster-line counts remain stable as the window changes
   size; Match window resolution retains the previous pixel-based scaling.
   Pixel derivatives integrate the raster footprint and suppress unresolved
   mask detail. Beam Width blends in an energy-normalized Gaussian beam whose
   width increases with luminance. RGB Convergence varies radially toward the
   edges. Edge Defocus spreads the image near the corners while retaining a
   sharp centre; delta-dot spots also broaden there.
   Signal Interference combines line gain noise, horizontal sync displacement
   and static. Static/displacement ramp quadratically to leave room for subtle
   low settings; maximum strength intentionally disrupts the picture. Both
   controls are invisible at zero. Presets use restrained amounts except Zero
   Cool; manually selected 100% is intentionally beyond the preset range.
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

## Tube controls and presets

Tube presets are authored starting points, not measured hardware emulations:
Desktop Monitor uses 480 raster lines and an aperture grille; Shadow-mask
Monitor uses 360 lines and delta RGB dots; Soft Terminal uses 240 lines, a slot
mask and softer optics. All three disable hum and signal interference. Strength
presets adjust amounts while preserving the selected raster and mask. Manual
changes mark the tube Custom. All tube controls participate in staged Save and
Close/Cancel, persistence and temporal-history invalidation.

Phosphor Glow supplies a tight core and halo, Bloom spreads bright highlights,
and Glass Diffusion supplies a broader, lower-threshold veil. Each is independently
enabled and allocated, and zero strength skips its passes. Existing component
keys remain stable (including `chromatic_aberration`, now labeled RGB Convergence).
Old saved configurations retain their delta mask and window-relative pitch
until a tube preset or raster setting is chosen.

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

`anybandui-gpu-tests <driver> --bench` runs a 1920x1080 offscreen workload with two
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
python anybandui/shaders/build.py --dxc <path-to-dxc> --spirv-cross <path-to-spirv-cross>
```

Commit the HLSL changes and regenerated header together. Add `--check` to verify
that the header matches the sources and tool output. The header includes a source
hash. Resource bindings follow SDL GPU conventions: fragment samplers in space 2,
uniforms in space 3. SPIRV-Cross uses `--msl-decoration-binding` to retain sampler
indices; automatic remapping can silently bind the wrong images. SDL normalizes
the viewport convention, so the vertex shader does not need a Vulkan-only Y flip.

## Validation and limits

Build `anybandui-client-tests` and `anybandui-gpu-tests` with the normal build helper.
Run the client tests with an unused settings-file path and GPU tests with an
optional SDL driver name (`direct3d12`, `vulkan`, or `metal`). GPU tests create
textures and read them back without opening a window or automating the desktop.

Direct3D and Vulkan pixel tests were run on Windows. Metal source generation and
binding layout were checked, but a Mac build/runtime test is still required.
The Gaussian shader effects can differ slightly from the old per-glyph halos;
the preference names, saved values and scope semantics are preserved.

## Death screen

Death drains colour from the entire composed window over 900 ms, including
UI outside the CRT scope. A final GPU pass blends towards luminance after CRT
and foreground UI composition. Scene transitions retain the last composed image in a full-window GPU target,
so shop fade-out shows the outgoing scene until black, then fades in the new
scene. This uses one final composition pass and no CPU image readback. The
target is released when scene transitions and colour drain are disabled. The Scene transitions setting
controls this effect independently of CRT. Acknowledgement opens the native
post-mortem without delay; its colours return smoothly over 600 ms from the
current desaturation level, including when death is acknowledged early.
