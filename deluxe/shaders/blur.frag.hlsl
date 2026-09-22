[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> image : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState image_sampler : register(s0, space2);
cbuffer Blur : register(b0, space3) {
    float4 step_threshold; // UV step.xy, highlight threshold, unused
    float4 region;         // UV origin.xy, size.xy
};
float3 fetch(float2 uv) {
    if (any(uv < region.xy) || any(uv > region.xy + region.zw)) return 0;
    float3 c = image.SampleLevel(image_sampler, uv, 0).rgb;
    float peak = max(c.r, max(c.g, c.b));
    return c * saturate((peak - step_threshold.z) / max(peak, 0.0001));
}
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float3 c = fetch(uv) * 0.227027;
    // Bilinear taps implement a nine-tap separable Gaussian with five reads.
    c += (fetch(uv + step_threshold.xy * 1.384615) + fetch(uv - step_threshold.xy * 1.384615)) * 0.316216;
    c += (fetch(uv + step_threshold.xy * 3.230769) + fetch(uv - step_threshold.xy * 3.230769)) * 0.070270;
    return float4(c, 1);
}
