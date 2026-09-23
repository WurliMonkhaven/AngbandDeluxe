[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] Texture2D<float4> scene : register(t0, space2);
[[vk::combinedImageSampler]] [[vk::binding(0, 2)]] SamplerState scene_sampler : register(s0, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] Texture2D<float4> glow : register(t1, space2);
[[vk::combinedImageSampler]] [[vk::binding(1, 2)]] SamplerState glow_sampler : register(s1, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] Texture2D<float4> bloom : register(t2, space2);
[[vk::combinedImageSampler]] [[vk::binding(2, 2)]] SamplerState bloom_sampler : register(s2, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] Texture2D<float4> history : register(t3, space2);
[[vk::combinedImageSampler]] [[vk::binding(3, 2)]] SamplerState history_sampler : register(s3, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] Texture2D<float4> glass : register(t4, space2);
[[vk::combinedImageSampler]] [[vk::binding(4, 2)]] SamplerState glass_sampler : register(s4, space2);
cbuffer Crt : register(b0, space3) {
    float4 region;      // UV origin.xy, size.xy
    float4 viewport;    // pixel size.xy, time, history retention
    float4 effects;     // scanlines, glow, bloom, chromatic aberration
    float4 shape;       // vignette, barrel coefficient, hum, health glitch
    float4 surface;     // phosphor dots, signal interference, mask layout, shutdown progress (-1: inactive)
    float4 optics;      // beam width, edge defocus, glass diffusion, raster unit in output pixels
};
float3 sample_tube(float2 uv) {
    float2 lo=region.xy+0.5/viewport.xy, hi=region.xy+region.zw-0.5/viewport.xy;
    float3 c=scene.SampleLevel(scene_sampler,clamp(uv,lo,hi),0).rgb;
    if(effects.w>0) {
        float2 radial=(uv-region.xy)/region.zw*2-1;
        float edge=saturate(dot(radial,radial)*0.5);
        float2 offset=(float2(0.35,0)+radial*(0.5+5*edge))*effects.w/viewport.xy;
        c.r=scene.SampleLevel(scene_sampler,clamp(uv+offset,lo,hi),0).r;
        c.b=scene.SampleLevel(scene_sampler,clamp(uv-offset,lo,hi),0).b;
    }
    return c;
}
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float3 untouched = scene.SampleLevel(scene_sampler, uv, 0).rgb;
    if (any(uv < region.xy) || any(uv > region.xy + region.zw)) return float4(untouched, 1);

    // Sample an inverse barrel map. All effects below use these source
    // coordinates, so scanlines and the hum front curve WITH the image.
    float2 destination = (uv - region.xy) / region.zw * 2 - 1;
    float2 screen_position=destination;
    float off=saturate(surface.w);
    // Vertical deflection collapses first, then horizontal deflection. The
    // whole tube image (including its raster) is compressed with the beam.
    float2 deflection=float2(1,1);
    if(surface.w>=0) {
        deflection.y=max(0.001, pow(1-smoothstep(0.0,0.38,off),3));
        deflection.x=max(0.001, 1-smoothstep(0.23,0.58,off));
        destination/=deflection;
    }
    float2 p = clamp(destination,-2,2);
    [unroll] for (int i = 0; i < 6; ++i) {
        p.x = clamp(destination.x,-2,2) / (1 - shape.y * p.y * p.y);
        p.y = clamp(destination.y,-2,2) / (1 - shape.y * p.x * p.x);
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
    // Signal strength has a deliberately broad range: fine instability at low
    // settings, visibly broken horizontal sync at the top of the slider.
    float signal = surface.y * surface.y;
    float line_noise = frac(sin(floor(local.y * region.w * viewport.y / 3) * 12.9898 + floor(viewport.z * 24) * 78.233) * 43758.5453) - 0.5;
    source.x += line_noise * 36 * signal / viewport.x;
    source = clamp(source, region.xy + 0.5 / viewport.xy, region.xy + region.zw - 0.5 / viewport.xy);
    float edge_focus = saturate(dot(p,p)*0.5);
    float3 c = sample_tube(source);
    if(optics.y>0) {
        float2 spread = optics.y * 6 * edge_focus * edge_focus / viewport.xy;
        float3 h0=sample_tube(source+float2(spread.x,0));
        float3 h1=sample_tube(source-float2(spread.x,0));
        float3 v0=sample_tube(source+float2(0,spread.y));
        float3 v1=sample_tube(source-float2(0,spread.y));
        c=sqrt((c*c*2+h0*h0+h1*h1+v0*v0+v1*v1)/6);
    }
    if(glitch>0) {
        float2 offset=float2(3*glitch/viewport.x,0);
        c=lerp(c,float3(sample_tube(source+offset).r,c.g,sample_tube(source-offset).b),glitch);
    }
    c *= 1 - 0.18 * glitch;
    // Work in approximate linear light, matching the blur's gamma-2 decode.
    // Drive luminous phosphors harder without lifting unlit screen/backgrounds.
    float excitation = smoothstep(0.08, 0.45, max(c.r, max(c.g, c.b)));
    float beam_luminance = dot(c*c,float3(0.2126,0.7152,0.0722));
    c *= c;
    c *= 1 + 0.9 * effects.y * excitation;

    float row = local.y * region.w * viewport.y / optics.w;
    float distance = abs(frac((row - 0.5) / 3 + 0.5) * 3 - 1.5);
    float aa = max(0.5, 0.5 * fwidth(row));
    float scan = 1 - smoothstep(0.5 - aa, 0.5 + aa, distance);
    float scan_depth = effects.x * (150.0 / 255.0);
    // Redistribute beam energy rather than simply removing it. Each dark line
    // covers one third of the three-pixel pitch (including antialiasing).
    float beam_gain = (1 - scan * scan_depth) / (1 - scan_depth / 3);
    // Periodic Gaussian beam: brighter phosphors broaden, dim strokes stay
    // narrow. Integrate the output-pixel footprint and preserve mean energy.
    float beam_sigma = 0.22 + optics.x * (0.15 + 0.85 * sqrt(beam_luminance));
    float sigma2_beam = beam_sigma*beam_sigma + fwidth(row)*fwidth(row)/12;
    float beam_phase = frac((row-0.5)/3)*3-1.5;
    float gaussian_beam = 0;
    [unroll] for(int beam_line=-1;beam_line<=1;++beam_line) {
        float d=beam_phase+beam_line*3;
        gaussian_beam+=exp(-d*d/(2*sigma2_beam))*3/sqrt(6.2831853*sigma2_beam);
    }
    float beam_resolved=1-smoothstep(1.5,3,fwidth(row));
    beam_gain=lerp(beam_gain,gaussian_beam,optics.x*beam_resolved);
    c *= beam_gain;
    // Staggered delta-gun triads: all three emitters in a group receive the
    // SAME area-filtered image sample. That is essential for thin white strokes
    // to illuminate a complete RGB group instead of arbitrary coloured specks.
    float2 tube = local * region.zw * viewport.xy / optics.w;
    float footprint = max(length(ddx(tube)), length(ddy(tube)));
    float resolved = 1 - smoothstep(2, 4, footprint);
    if (surface.x > 0 && resolved > 0 && surface.z < 0.5) {
        const float pitch = 4;
        const float row_pitch = 3.4641016;
        float sigma2 = 0.36 + footprint * footprint / 12 + optics.y * edge_focus * edge_focus;
        float3 emitted = 0;
        float row_center = round(tube.y / row_pitch);
        [unroll] for (int ry = -1; ry <= 1; ++ry) {
            float cy = row_center + ry;
            float shift = frac(cy * 0.5) * pitch;
            float column = floor((tube.x - shift) / pitch);
            [unroll] for (int rx = 0; rx < 2; ++rx) {
                float2 center = float2((column + rx) * pitch + shift, cy * row_pitch);
                float3 drive = 0;
                [unroll] for (int sample_y = -1; sample_y <= 1; sample_y += 2)
                    [unroll] for (int sample_x = -1; sample_x <= 1; sample_x += 2) {
                        float2 sample_uv = source + (center - tube + float2(sample_x, sample_y) * 0.85) * optics.w / viewport.xy;
                        sample_uv = clamp(sample_uv, region.xy + 0.5 / viewport.xy, region.xy + region.zw - 0.5 / viewport.xy);
                        float3 sample_color = sample_tube(sample_uv);
                        drive += sample_color * sample_color * 0.25;
                    }
                float2 d = tube - center;
                float2 dr = d - float2(-0.95, -0.55);
                float2 dg = d - float2(0.95, -0.55);
                float2 db = d - float2(0, 1.1);
                float3 distance2 = float3(dot(dr,dr), dot(dg,dg), dot(db,db));
                // Gaussian spot profiles overlap optically; normalize their
                // integrated energy, not their individual peak RGB values.
                emitted += drive * exp(-distance2 / (2 * sigma2)) * (pitch * row_pitch / (6.2831853 * sigma2));
            }
        }
        emitted *= (1 + 0.9 * effects.y * excitation) * beam_gain;
        c = lerp(c, emitted, surface.x * resolved * 0.85);
    }
    if(surface.x>0 && resolved>0 && surface.z>=0.5) {
        // Aperture grille uses vertical RGB strips; slot masks interrupt them
        // with staggered bridges. Both share the raster's physical pitch.
        float3 stripe_phase = tube.x/4 + float3(0,1.0/3,2.0/3);
        float3 stripe_distance = abs(frac(stripe_phase+0.5)-0.5)*4;
        float edge_aa=max(0.25,footprint*0.5);
        float3 mask=1-smoothstep(0.55-edge_aa,0.55+edge_aa,stripe_distance);
        if(surface.z>1.5) {
            float stagger=frac(floor(tube.x/4)*0.5)*3;
            float bridge=abs(frac((tube.y+stagger)/6+0.5)-0.5)*6;
            mask*=smoothstep(0.25,0.25+edge_aa,bridge);
        }
        c*=lerp(1,0.15+2.6*mask,surface.x*resolved);
    }
    // Local gain noise, not a full-screen brightness flash.
    float grain = frac(sin(floor(tube.y) * 12.9898 + floor(viewport.z * 24) * 78.233) * 43758.5453) - 0.5;
    c *= 1 + grain * surface.y * 1.8;
    // Optical spill happens after the beam pattern, so its halos remain soft.
    if (effects.y > 0) c += glow.SampleLevel(glow_sampler, source, 0).rgb * effects.y * 0.9;
    if (effects.z > 0) c += bloom.SampleLevel(bloom_sampler, source, 0).rgb * effects.z * 1.2;
    if(optics.z>0) c += glass.SampleLevel(glass_sampler,source,0).rgb * optics.z * 2;
    float edge_width = min(region.z * viewport.x, region.w * viewport.y) * 0.09;
    c *= 1 - (1 - saturate(min(edge_pixels.x, edge_pixels.y) / max(edge_width, 1))) * shape.x * (160.0 / 255.0);
    // Fit overbright light into SDR by scaling the whole colour: independent
    // channel clipping would bleach saturated terminal colours toward white.
    float3 hue_fitted = c / max(1, max(c.r, max(c.g, c.b)));
    c = lerp(hue_fitted, saturate(c), surface.x * resolved);
    c = sqrt(max(c, 0));
    // At strong settings the signal also leaks visible static into unlit areas.
    float static_noise = frac(sin(dot(floor(tube), float2(127.1,311.7)) + floor(viewport.z * 24) * 74.7) * 43758.5453);
    c = lerp(c, static_noise.xxx, signal * 0.65);
    float behind = frac(viewport.z / 12 - local.y + 1);
    float trail = pow(saturate(1 - behind / 0.24), 3);
    c = lerp(c, 1, trail * shape.z * (50.0 / 255.0));
    c = saturate(c) * coverage;
    if (viewport.w > 0) c = lerp(c, history.SampleLevel(history_sampler, uv, 0).rgb, viewport.w);
    if(surface.w>=0) {
        // A little stored beam energy makes a luminous horizontal trace and
        // warm central phosphor spot, not an indiscriminate white flash.
        float2 pixels=screen_position*region.zw*viewport.xy*0.5;
        float line_width=max(0.55,region.w*viewport.y*0.003*(1-off));
        float beam_trace=exp(-pixels.y*pixels.y/(2*line_width*line_width));
        float length=region.z*viewport.x*0.47*deflection.x;
        beam_trace*=1-smoothstep(length,length+3,abs(pixels.x));
        float energy=smoothstep(0.12,0.30,off)*(1-smoothstep(0.46,0.64,off));
        float radius=0.7+2*(1-smoothstep(0.55,0.92,off));
        float spot=exp(-dot(pixels,pixels)/(2*radius*radius));
        float spot_energy=smoothstep(0.42,0.58,off)*(1-smoothstep(0.60,0.95,off));
        c*=1-smoothstep(0.36,0.54,off);
        c*=step(abs(screen_position.x),deflection.x)*step(abs(screen_position.y),deflection.y);
        c+=float3(0.78,0.9,1)*beam_trace*energy;
        c+=float3(1,0.88,0.70)*spot*spot_energy;
        c=saturate(c)*(1-smoothstep(0.94,0.99,off));
    }
    return float4(c, 1);
}
