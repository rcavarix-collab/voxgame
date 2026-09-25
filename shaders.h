// shaders.h
//
// All HLSL, as C strings compiled at start-up (through the shader cache in
// render.cpp, so a machine compiles each only once). Shader model 4 on
// purpose: the target is an older, modest Windows PC. Checked off Windows
// by tools/check_shaders.py (glslangValidator's HLSL front end).
//
// Colour is computed in linear light and written to an sRGB target, so
// shading and fog blend correctly; UI colours are given in sRGB and
// converted on the way in.

#pragma once

// ---- Sky: a full-screen triangle; the view ray from the camera's basis ----
static const char* g_skyShaderSrc = R"(
cbuffer SkyCB : register(b0) {
    float4 camRight;   // xyz, w = tan(half fov x)
    float4 camUp;      // xyz, w = tan(half fov y)
    float4 camFwd;
    float4 sunDir;     // xyz toward the sun, w = strength 0..1
    float4 zenith;
    float4 horizon;
    float4 groundHaze;
};
struct VSOut { float4 pos : SV_POSITION; float2 ndc : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);
    o.pos = float4(p, 1.0, 1.0);
    o.ndc = p;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float3 d = normalize(camFwd.xyz + i.ndc.x * camRight.w * camRight.xyz + i.ndc.y * camUp.w * camUp.xyz);
    float up = saturate(d.y);
    float3 c = lerp(horizon.rgb, zenith.rgb, sqrt(up));
    c = lerp(c, groundHaze.rgb, saturate(-d.y * 4.0));
    float s = saturate(dot(d, sunDir.xyz));
    // A soft disc and a wide glow: steady, never a flash.
    c += (pow(s, 900.0) * 6.0 + pow(s, 12.0) * 0.25) * sunDir.w * float3(1.0, 0.92, 0.78);
    return float4(c, 1.0);
}
)";

// ---- Terrain: flat-shaded facets; look from ground type, facing and depth ----
static const char* g_terrainShaderSrc = R"(
cbuffer FrameCB : register(b0) {
    row_major float4x4 viewProj;
    float4 eye;
    float4 sunDir;     // xyz toward the sun, w = strength
    float4 sunColor;
    float4 ambient;    // sky light
    float4 fogColor;
    float4 fogParams;  // x start, y 1/range, z max
};
struct VSIn { float3 pos : POSITION; uint4 info : TEXCOORD0; };
struct VSOut {
    float4 pos : SV_POSITION;
    float3 wpos : TEXCOORD0;
    nointerpolation uint mat : TEXCOORD1;
    nointerpolation float ao : TEXCOORD2;
    nointerpolation float3 fpos : TEXCOORD3;  // the facet's first vertex: a stable per-facet seed
};
VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0), viewProj);
    o.wpos = v.pos;
    o.mat = v.info.x;
    o.ao = v.info.y / 255.0;
    o.fpos = v.pos;
    return o;
}
// Ground types (terrain.h Ground): meadow, dry grass, moss, clover, loam, clay, gravel, rock.
static const float3 kGround[8] = {
    float3(0.105, 0.190, 0.058), float3(0.205, 0.185, 0.085), float3(0.068, 0.132, 0.052), float3(0.088, 0.205, 0.078),
    float3(0.118, 0.080, 0.050), float3(0.165, 0.098, 0.062), float3(0.150, 0.142, 0.128), float3(0.150, 0.146, 0.140)
};
static const uint kSoil[8] = { 4, 5, 4, 4, 4, 5, 6, 7 };
float Hash(float3 p) {
    uint3 q = (uint3)(int3)floor(p * 7.0 + 1000.0);
    uint h = q.x * 73856093u ^ q.y * 19349663u ^ q.z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 1023u) / 1023.0;
}
float4 PSMain(VSOut i) : SV_TARGET {
    // The facet's own normal, from screen-space derivatives: flat per facet.
    float3 n = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
    if (dot(n, eye.xyz - i.wpos) < 0.0) n = -n;
    uint g = i.mat & 0x7Fu;
    if (g > 7u) g = 7u;
    // Grass only on facets that face up; its steep sides show the soil it grows in.
    if (g <= 3u && n.y < 0.72) g = kSoil[g];
    float3 c = kGround[g];
    float h = Hash(i.fpos);
    c *= 0.94 + 0.12 * h;                          // neighbouring facets read apart, gently
    if ((i.mat & 0x80u) != 0u) c = lerp(c, float3(0.030, 0.026, 0.024), 0.6); // scorched
    float ndl = saturate(dot(n, sunDir.xyz)) * sunDir.w;
    float3 lit = c * (ambient.rgb * (0.55 + 0.45 * n.y) * (0.45 + 0.55 * i.ao) + sunColor.rgb * ndl);
    float dist = length(i.wpos - eye.xyz);
    float f = saturate((dist - fogParams.x) * fogParams.y) * fogParams.z;
    return float4(lerp(lit, fogColor.rgb, f), 1.0);
}
)";

// ---- Meshes built from primitives (props, debris, the wanderer, effects) ----
// Same frame constants and lighting as the terrain; flat-shaded; colour per
// vertex. Alpha below one half marks a glowing surface, drawn unlit.
static const char* g_meshShaderSrc = R"(
cbuffer FrameCB : register(b0) {
    row_major float4x4 viewProj;
    float4 eye;
    float4 sunDir;
    float4 sunColor;
    float4 ambient;
    float4 fogColor;
    float4 fogParams;
};
struct VSIn { float3 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float3 wpos : TEXCOORD0; nointerpolation float4 col : COLOR0; };
VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0), viewProj);
    o.wpos = v.pos;
    o.col = v.col;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float dist = length(i.wpos - eye.xyz);
    float f = saturate((dist - fogParams.x) * fogParams.y) * fogParams.z;
    if (i.col.a < 0.5) return float4(lerp(i.col.rgb * 2.2, fogColor.rgb, f * 0.6), 1.0); // glowing: fire, embers, tracers
    float3 n = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
    if (dot(n, eye.xyz - i.wpos) < 0.0) n = -n;
    float ndl = saturate(dot(n, sunDir.xyz)) * sunDir.w;
    float3 lit = i.col.rgb * (ambient.rgb * (0.6 + 0.4 * n.y) * 0.85 + sunColor.rgb * ndl);
    return float4(lerp(lit, fogColor.rgb, f), 1.0);
}
)";

// ---- UI: pixel-space quads, textured from the text atlas (white = its top rows) ----
static const char* g_uiShaderSrc = R"(
cbuffer UICB : register(b0) { float4 screen; };  // xy = 2/width, 2/height
Texture2D atlas : register(t0);
SamplerState pointSampler : register(s0);
struct VSIn { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
VSOut VSMain(VSIn v) {
    VSOut o;
    o.pos = float4(v.pos.x * screen.x - 1.0, 1.0 - v.pos.y * screen.y, 0.0, 1.0);
    o.uv = v.uv;
    o.col = float4(pow(abs(v.col.rgb), 2.2), v.col.a); // sRGB in, linear out (the target is sRGB)
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    float a = atlas.Sample(pointSampler, i.uv).a;
    return float4(i.col.rgb, i.col.a * a);
}
)";
