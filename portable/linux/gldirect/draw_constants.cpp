// Portable extraction of SDK d3d_common.cpp constant packing.
// Copyright (c) 2026 TGP482; SDK helper changes by hk21x.
// Distributed under MIT; see SDK-MIT-LICENSE.txt.
// Kept outside sdk/: no Direct3D types or rendering-state interpreter is duplicated.
#include "draw_constants.h"
#include <bit>
#include <cmath>
#include <cstring>
namespace pgr::gldirect {
namespace {
using namespace gpu;
struct GuestExtent { unsigned w,h; };
float Bits(unsigned v) { return std::bit_cast<float>(v); }
unsigned SimpleState(const State& s,unsigned m) { return s.simple[m/4]; }
unsigned Cmp(unsigned v) { return v>=0x200 && v<=0x207 ? v-0x200+1 : 8; }
constexpr float kAlphaTestAlways=7, kFogTableLinear=3;
constexpr unsigned kFogTableNone=0;
void Color(unsigned v,float* out) { out[0]=float((v>>16)&255)/255;out[1]=float((v>>8)&255)/255;out[2]=float(v&255)/255;out[3]=float(v>>24)/255; }
void FillFogConstants(const State& s, bool vertex_program, DrawConstants& c) {
    if (!s.rs[kRsFogEnable]) return;
    if (vertex_program && s.rs[kRsFogTableMode] == kFogTableNone) {
        c.fog[0] = 1.0f;
        c.fog[1] = 0.0f;
        c.fog[2] = 0.0f;
        c.fog[3] = kFogTableLinear;
        return;
    }
    c.fog[0] = Bits(s.rs[kRsFogStart]);
    c.fog[1] = Bits(s.rs[kRsFogEnd]);
    c.fog[2] = Bits(s.rs[kRsFogDensity]);
    c.fog[3] = float(s.rs[kRsFogTableMode]);
}

void FillSurfaceConstants(const State& s, GuestExtent size, DrawConstants& c) {
    c.surf[0] = float(size.w);
    c.surf[1] = float(size.h);
    c.surf[2] = 1.0f / s.zscale;
    Color(s.rs[kRsFogColor], c.fog_color);
    c.misc[0] = float(SimpleState(s, kMAlphaRef) & 0xFF);
    c.misc[1] = SimpleState(s, kMAlphaTestEnable) ? float(Cmp(SimpleState(s, kMAlphaFunc)) - 1) : kAlphaTestAlways;
    c.misc[2] = float(Cmp(s.rs[kRsShadowFunc]) - 1);
    c.misc[3] = s.rs[kRsSpecularEnable] ? 1.0f : 0.0f;
    // point sprites: the geometry shader makes each point a square (PointHlsl)
    c.pts[0] = s.rs[kRsPointSpriteEnable] ? 1.0f : 0.0f;
    c.pts[1] = Bits(s.rs[kRsPointSizeMin]);
    c.pts[2] = Bits(s.rs[kRsPointSizeMax]);
    c.pts[3] = Bits(s.rs[kRsPointSize]);
}

void FillCombinerConstants(const State& s, DrawConstants& c) {
    for (int i = 0; i < 8; i++) {
        Color(SimpleState(s, kMPsConstant0 + i * 4), c.pc[i * 2]);
        Color(SimpleState(s, kMPsConstant1 + i * 4), c.pc[i * 2 + 1]);
    }
    Color(SimpleState(s, kMPsFinalConstant0), c.pc[16]);
    Color(SimpleState(s, kMPsFinalConstant1), c.pc[17]);
    Color(s.rs[kRsTextureFactor], c.tfactor);
}

void MatMul(const float* x, const float* y, float* o) {
    for (int r = 0; r < 4; r++) {
        for (int col = 0; col < 4; col++) {
            o[r * 4 + col] = x[r * 4] * y[col] + x[r * 4 + 1] * y[4 + col] + x[r * 4 + 2] * y[8 + col] +
                             x[r * 4 + 3] * y[12 + col];
        }
    }
}

// HLSL mul(row vector, matrix) with column major packing wants the transpose
void Transpose(const float* m, float* o) {
    for (int r = 0; r < 4; r++) {
        for (int col = 0; col < 4; col++) o[col * 4 + r] = m[r * 4 + col];
    }
}

void InverseTranspose3x3(const float* wv, float* n) {
    float det = wv[0] * (wv[5] * wv[10] - wv[6] * wv[9]) - wv[1] * (wv[4] * wv[10] - wv[6] * wv[8]) +
                wv[2] * (wv[4] * wv[9] - wv[5] * wv[8]);
    float id = det != 0 ? 1.0f / det : 0.0f;
    for (int r = 0; r < 3; r++) {
        for (int col = 0; col < 3; col++) {
            int r1 = (r + 1) % 3;
            int r2 = (r + 2) % 3;
            int c1 = (col + 1) % 3;
            int c2 = (col + 2) % 3;
            n[r * 4 + col] = (wv[r1 * 4 + c1] * wv[r2 * 4 + c2] - wv[r1 * 4 + c2] * wv[r2 * 4 + c1]) * id;
        }
    }
}

// D3DTRANSFORMSTATETYPE indices into s.transforms
constexpr size_t kTsView = 2;
constexpr size_t kTsProjection = 3;
constexpr size_t kTsTexture0 = 16;
constexpr size_t kTsWorld = 256;

void FillTransformConstants(const State& s, GuestExtent size, DrawConstants& c) {
    uint32_t gw = size.w;
    uint32_t gh = size.h;
    // world, view, projection, then the viewport inside the target
    float wv[16];
    float wvp[16];
    MatMul(s.transforms[kTsWorld].data(), s.transforms[kTsView].data(), wv);
    MatMul(wv, s.transforms[kTsProjection].data(), wvp);
    const auto& v = s.viewport;
    float sx = float(v.w) / gw;
    float sy = float(v.h) / gh;
    float ox = (2.0f * v.x + v.w) / gw - 1;
    float oy = 1 - (2.0f * v.y + v.h) / gh;
    float vpm[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, v.max_z - v.min_z, 0, ox, oy, v.min_z, 1};
    float full[16];
    MatMul(wvp, vpm, full);
    Transpose(full, c.wvp);
    Transpose(wv, c.wv);
    // normals take the inverse transpose of the upper 3x3
    float n[16] = {};
    InverseTranspose3x3(wv, n);
    Transpose(n, c.wvn);
    for (int m = 0; m < 3; m++) {
        float bv[16], bn[16] = {};
        MatMul(s.transforms[kTsWorld + 1 + size_t(m)].data(), s.transforms[kTsView].data(), bv);
        Transpose(bv, c.bwv[m]);
        InverseTranspose3x3(bv, bn);
        Transpose(bn, c.bwvn[m]);
    }
    float pv[16];
    MatMul(s.transforms[kTsProjection].data(), vpm, pv);
    Transpose(pv, c.vpj);
    for (int t = 0; t < 4; t++) Transpose(s.transforms[kTsTexture0 + size_t(t)].data(), c.tm[t]);
}

// D3DLIGHT8 as floats: Type, Diffuse[4], Specular[4], Ambient[4], Position[3], Direction[3], Range, Falloff,
// Attenuation0 to 2, Theta, Phi
constexpr int kLightFloats = 26;
constexpr int kLightDiffuse = 1;
constexpr int kLightSpecular = 5;
constexpr int kLightAmbient = 9;
constexpr int kLightPosition = 13;
constexpr int kLightDirection = 16;
constexpr int kLightRange = 19;
constexpr int kLightFalloff = 20;
constexpr int kLightAttenuation0 = 21;
constexpr int kLightAttenuation1 = 22;
constexpr int kLightAttenuation2 = 23;
constexpr int kLightTheta = 24;
constexpr int kLightPhi = 25;

void LightToView(const std::array<uint32_t, kLightFloats>& l, const float* vm, float (&o)[7][4]) {
    float f[kLightFloats];
    std::memcpy(f, l.data(), sizeof(f));
    for (int j = 0; j < 4; j++) {
        o[0][j] = f[kLightDiffuse + j];
        o[1][j] = f[kLightSpecular + j];
        o[2][j] = f[kLightAmbient + j];
    }
    const float* pos = f + kLightPosition;
    const float* dir = f + kLightDirection;
    for (int j = 0; j < 3; j++) {
        o[3][j] = pos[0] * vm[j] + pos[1] * vm[4 + j] + pos[2] * vm[8 + j] + vm[12 + j];
        o[4][j] = dir[0] * vm[j] + dir[1] * vm[4 + j] + dir[2] * vm[8 + j];
    }
    o[3][3] = f[kLightRange];
    o[4][3] = f[kLightFalloff];
    o[5][0] = f[kLightAttenuation0];
    o[5][1] = f[kLightAttenuation1];
    o[5][2] = f[kLightAttenuation2];
    o[6][0] = std::cos(f[kLightTheta] * 0.5f);
    o[6][1] = std::cos(f[kLightPhi] * 0.5f);
}

void FillLightConstants(const State& s, const FixedKey& fk, DrawConstants& c) {
    std::memcpy(c.mat, s.material.data(), sizeof(float) * 16);
    c.mat[4][0] = s.material[16];
    Color(s.rs[kRsAmbient], c.amb);
    // lights to view space
    const float* vm = s.transforms[kTsView].data();
    for (int i = 0; i < 8; i++) {
        if (!((fk.types >> (i * 2)) & 3)) continue;
        LightToView(s.lights[size_t(i)], vm, c.lt[i]);
    }
}


}
gpu::DrawConstants Constants(const gpu::State& s,unsigned width,unsigned height,bool program) {
    gpu::DrawConstants c{};
    FillSurfaceConstants(s,{width,height},c);
    FillFogConstants(s,program,c);
    FillCombinerConstants(s,c);
    FillTransformConstants(s,{width,height},c);
    FillLightConstants(s,gpu::MakeFixedKey(s),c);
    c.hud[0]=1; c.hud[1]=float(width)*.5f; c.hud[2]=s.rs[gpu::kRsZEnable] && s.simple[gpu::kMZFunc/4]!=0x207;
    c.hud[3]=s.rs[gpu::kRsZEnable]==2;
    if(s.simple[gpu::kMSolidOffsetEnable/4]) { c.wdepth[0]=Bits(s.simple[gpu::kMDepthOffset/4]); c.wdepth[1]=Bits(s.simple[gpu::kMSlopeScale/4]); }
    c.wdepth[2]=1.0f/s.zscale;
    c.wdepth[3]=program ? 1.0f : s.zscale*s.inv_wfar;
    for(unsigned i=0;i<4;i++) {
        const auto& t=s.tss[i];
        c.bump[i][0]=Bits(t[gpu::kTssBumpEnvMat00]); c.bump[i][1]=Bits(t[gpu::kTssBumpEnvMat01]);
        c.bump[i][2]=Bits(t[gpu::kTssBumpEnvMat10]); c.bump[i][3]=Bits(t[gpu::kTssBumpEnvMat11]);
        c.bumpl[i][0]=Bits(t[gpu::kTssBumpEnvLScale]); c.bumpl[i][1]=Bits(t[gpu::kTssBumpEnvLOffset]);
    }
    return c;
}
}
