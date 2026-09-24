// Final composition pass: includes UI outside the selected CRT scope.
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> image : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState image_sampler : register(s0, space2);
cbuffer ColourDrain : register(b0, space3) { float4 amount; };
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float4 c = image.SampleLevel(image_sampler, uv, 0);
    // Match the renderer's gamma-2 approximation, preserving light intensity.
    float3 light = c.rgb * c.rgb;
    float luminance = dot(light, float3(0.2126, 0.7152, 0.0722));
    c.rgb = sqrt(lerp(light, luminance.xxx, saturate(amount.x)));
    c.rgb *= 1.0 - saturate(amount.y);
    return c;
}
