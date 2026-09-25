[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> image : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState image_sampler : register(s0, space2);
cbuffer Blur : register(b0, space3) {
    // Prefilter: output texel size.xy, threshold, reduction factor.
    // Gaussian: input texel step.xy, sigma in input texels, zero.
    float4 step_filter;
    float4 region;         // UV origin.xy, size.xy
};
float3 fetch(float2 uv) {
    if (any(uv < region.xy) || any(uv > region.xy + region.zw)) return 0;
    float3 c = image.SampleLevel(image_sampler, uv, 0).rgb;
    if (step_filter.w > 0) {
        float peak = max(c.r, max(c.g, c.b));
        // Decode before averaging; Gaussian passes already contain light.
        c *= c;
        c *= saturate((peak - step_filter.z) / max(peak, 0.0001));
    }
    return c;
}
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    if (step_filter.w > 0) {
        // Area prefilter before reducing resolution, so one-pixel strokes
        // cannot vanish or become stripes depending on their pixel alignment.
        int factor = (int)step_filter.w;
        float3 sum = 0;
        for (int y = 0; y < factor; ++y)
            for (int x = 0; x < factor; ++x)
                sum += fetch(uv + ((float2(x, y) + 0.5) / factor - 0.5) * step_filter.xy);
        return float4(sum / (factor * factor), 1);
    }
    float sigma = step_filter.z;
    int extent = (int)ceil(3 * sigma);
    float3 sum = fetch(uv);
    float weight = 1;
    // Cover EVERY source texel. Stretching a fixed five-read kernel leaves
    // holes and produces separated copies of thin text. Pair adjacent taps
    // using bilinear filtering without increasing their spacing.
    for (int i = 1; i <= extent; i += 2) {
        float a = exp(-0.5 * i * i / (sigma * sigma));
        float b = i + 1 <= extent ? exp(-0.5 * (i + 1) * (i + 1) / (sigma * sigma)) : 0;
        float pair = a + b;
        float2 offset = step_filter.xy * (i + b / pair);
        sum += (fetch(uv + offset) + fetch(uv - offset)) * pair;
        weight += 2 * pair;
    }
    return float4(sum / weight, 1);
}
