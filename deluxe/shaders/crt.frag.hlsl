[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> scene : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState scene_sampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> glow : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState glow_sampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> bloom : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState bloom_sampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> history : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState history_sampler : register(s3, space2);
cbuffer Crt : register(b0, space3) {
    float4 region;      // UV origin.xy, size.xy
    float4 viewport;    // pixel size.xy, time, history retention
    float4 effects;     // scanlines, glow, bloom, chromatic aberration
    float4 shape;       // vignette, barrel coefficient, hum, health glitch
};
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float3 untouched = scene.SampleLevel(scene_sampler, uv, 0).rgb;
    if (any(uv < region.xy) || any(uv > region.xy + region.zw)) return float4(untouched, 1);

    // Sample an inverse barrel map. All effects below use these source
    // coordinates, so scanlines and the hum front curve WITH the image.
    float2 destination = (uv - region.xy) / region.zw * 2 - 1;
    float2 p = destination;
    [unroll] for (int i = 0; i < 6; ++i) {
        p.x = destination.x / (1 - shape.y * p.y * p.y);
        p.y = destination.y / (1 - shape.y * p.x * p.x);
    }
    float2 local = (p + 1) * 0.5;
    float2 source = region.xy + local * region.zw;
    float2 edge_pixels = min(local, 1 - local) * region.zw * viewport.xy;
    float coverage = saturate(min(edge_pixels.x, edge_pixels.y) + 0.5);
    // Short, irregular line slips, rather than whole-screen flashes. Severity
    // increases both displacement and burst duration, keeping low HP readable.
    float tick = floor(viewport.z * 12);
    float band = floor(local.y * 38);
    float noise = frac(sin(band * 127.1 + tick * 311.7) * 43758.5453);
    float burst = step(frac(viewport.z * 1.5), 0.10 + shape.w * 0.32);
    float glitch = shape.w * burst * step(0.76 - shape.w * 0.18, noise);
    source.x += (noise - 0.5) * 18 * glitch / viewport.x;
    source = clamp(source, region.xy + 0.5 / viewport.xy, region.xy + region.zw - 0.5 / viewport.xy);
    float3 c = scene.SampleLevel(scene_sampler, source, 0).rgb;
    if (effects.w > 0 || glitch > 0) {
        float2 offset = float2((1.6 * effects.w + 3 * glitch) / viewport.x, 0);
        float3 fringe = float3(scene.SampleLevel(scene_sampler, clamp(source + offset, region.xy, region.xy + region.zw), 0).r,
            c.g, scene.SampleLevel(scene_sampler, clamp(source - offset, region.xy, region.xy + region.zw), 0).b);
        c = lerp(c, fringe, saturate(0.65 * effects.w + glitch));
    }
    c *= 1 - 0.18 * glitch;
    // Work in approximate linear light, matching the blur's gamma-2 decode.
    // Drive luminous phosphors harder without lifting unlit screen/backgrounds.
    float excitation = smoothstep(0.08, 0.45, max(c.r, max(c.g, c.b)));
    c *= c;
    c *= 1 + 0.9 * effects.y * excitation;

    float row = local.y * region.w * viewport.y;
    float distance = abs(frac((row - 0.5) / 3 + 0.5) * 3 - 1.5);
    float aa = max(0.5, 0.5 * fwidth(row));
    float scan = 1 - smoothstep(0.5 - aa, 0.5 + aa, distance);
    float scan_depth = effects.x * (150.0 / 255.0);
    // Redistribute beam energy rather than simply removing it. Each dark line
    // covers one third of the three-pixel pitch (including antialiasing).
    c *= (1 - scan * scan_depth) / (1 - scan_depth / 3);
    // Optical spill happens after the beam pattern, so its halos remain soft.
    if (effects.y > 0) c += glow.SampleLevel(glow_sampler, source, 0).rgb * effects.y * 0.9;
    if (effects.z > 0) c += bloom.SampleLevel(bloom_sampler, source, 0).rgb * effects.z * 1.2;
    float edge_width = min(region.z * viewport.x, region.w * viewport.y) * 0.09;
    c *= 1 - (1 - saturate(min(edge_pixels.x, edge_pixels.y) / max(edge_width, 1))) * shape.x * (160.0 / 255.0);
    // Fit overbright light into SDR by scaling the whole colour: independent
    // channel clipping would bleach saturated terminal colours toward white.
    c /= max(1, max(c.r, max(c.g, c.b)));
    c = sqrt(max(c, 0));
    float behind = frac(viewport.z / 12 - local.y + 1);
    float trail = pow(saturate(1 - behind / 0.24), 3);
    c = lerp(c, 1, trail * shape.z * (50.0 / 255.0));
    c = saturate(c) * coverage;
    if (viewport.w > 0) c = lerp(c, history.SampleLevel(history_sampler, uv, 0).rgb, viewport.w);
    return float4(c, 1);
}
