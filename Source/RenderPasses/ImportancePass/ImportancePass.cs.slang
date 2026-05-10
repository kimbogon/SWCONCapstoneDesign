/***************************************************************************
 # ImportancePass.cs.slang
 # 1단계: max(albedoVar, geoNormVar, detailNormVar, metalness, glossiness)
 # 2단계: task override -> center weight -> luminance penalty -> motion penalty
 ***************************************************************************/
#ifndef HAS_DIFFUSE_OPACITY
#define HAS_DIFFUSE_OPACITY 0
#endif
#ifndef HAS_GUIDE_NORMAL
#define HAS_GUIDE_NORMAL 0
#endif
#ifndef HAS_FACE_NORMAL
#define HAS_FACE_NORMAL 0
#endif
#ifndef HAS_SPEC_ROUGH
#define HAS_SPEC_ROUGH 0
#endif
#ifndef HAS_MOTION_VECTOR_W
#define HAS_MOTION_VECTOR_W 0
#endif
#ifndef HAS_LINEAR_Z
#define HAS_LINEAR_Z 0
#endif
#ifndef HAS_SHADOW_COUNT
#define HAS_SHADOW_COUNT 0
#endif
#ifndef HAS_OBJ_ID
#define HAS_OBJ_ID 0
#endif
#ifndef HAS_LUMINANCE
#define HAS_LUMINANCE 0
#endif
#ifndef HAS_CENTER_DIST
#define HAS_CENTER_DIST 0
#endif
#ifndef HAS_METALNESS
#define HAS_METALNESS 0
#endif
#ifndef HAS_IMPORTANCE_VIS
#define HAS_IMPORTANCE_VIS 0
#endif

cbuffer CB
{
    uint2  frameDim;
    float2 invFrameDim;
    uint   activeTask;          // 0:None 1:Aim 2:Point 3:Steer
    uint   debugView;           // 0:Imp 1:TaskOverlay 2:ImgOnly 3:TaskOnly
    uint   hasImportanceVis;
    uint   _pad0;
    float2 centerUV;
    float  centerRadiusUV;
    float  taskImportance;
    float  enemyRedThreshold;
    float  itemGBThreshold;
    float  lumLow;
    float  lumHigh;
    float  motionA;
    float  motionB;
    float  motionC;
    float  centerWeightMin;
    float  steerFloorY;
    float  _pad1;
    float  _pad2;
    float  _pad3;
};

Texture2D<float4> gDiffuseOpacity;
Texture2D<float4> gGuideNormalW;
Texture2D<float4> gFaceNormalW;
Texture2D<float4> gSpecRough;
Texture2D<float4> gMotionVectorW;
Texture2D<float4> gLinearZ;
Texture2D<uint>   gShadowCount;
Texture2D<uint>   gObjectID;
Texture2D<float>  gLuminanceTex;
Texture2D<float>  gCenterDist;
Texture2D<float>  gMetalness;

RWTexture2D<float>  gImportance;
RWTexture2D<float4> gImportanceVis;
RWStructuredBuffer<uint> gCenterCategory; // 1 element: 0/1/2

// ---------- helpers ----------
int2 clampP(int2 p)
{
    return clamp(p, int2(0, 0), int2(frameDim) - int2(1, 1));
}

float lum601(float3 c) { return dot(c, float3(0.299f, 0.587f, 0.114f)); }

float3 sampleAlbedo(int2 p)
{
#if HAS_DIFFUSE_OPACITY
    return gDiffuseOpacity[clampP(p)].rgb;
#else
    return float3(0.f, 0.f, 0.f);
#endif
}

float3 sampleGuideN(int2 p)
{
#if HAS_GUIDE_NORMAL
    return gGuideNormalW[clampP(p)].xyz;
#else
    return float3(0.f, 0.f, 1.f);
#endif
}

float3 sampleFaceN(int2 p)
{
#if HAS_FACE_NORMAL
    return gFaceNormalW[clampP(p)].xyz;
#else
    return float3(0.f, 0.f, 1.f);
#endif
}

float sampleRough(int2 p)
{
#if HAS_SPEC_ROUGH
    return saturate(gSpecRough[clampP(p)].a);
#else
    return 1.f;
#endif
}

float sampleMet(int2 p)
{
#if HAS_METALNESS
    return saturate(gMetalness[clampP(p)]);
#else
    return 0.f;
#endif
}

float sampleLum(int2 p)
{
#if HAS_LUMINANCE
    return gLuminanceTex[clampP(p)];
#elif HAS_DIFFUSE_OPACITY
    return lum601(gDiffuseOpacity[clampP(p)].rgb);
#else
    return 0.f;
#endif
}

float sampleMotion(int2 p)
{
#if HAS_MOTION_VECTOR_W
    return length(gMotionVectorW[clampP(p)].xyz);
#else
    return 0.f;
#endif
}

float sampleCDist(int2 p)
{
#if HAS_CENTER_DIST
    return gCenterDist[clampP(p)];
#else
    float2 uv = (float2(p) + 0.5f) * invFrameDim;
    return length(uv - float2(0.5f, 0.5f));
#endif
}

// 3x3 scalar variance (population)
float scalarVar3x3(int2 p, bool useAlbedo)
{
    float s = 0.f;
    float ss = 0.f;
    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            float v = useAlbedo
                ? lum601(sampleAlbedo(p + int2(dx, dy)))
                : sampleMet(p + int2(dx, dy));
            s  += v;
            ss += v * v;
        }
    }
    float mean = s / 9.f;
    float var  = max(ss / 9.f - mean * mean, 0.f);
    return saturate(var * 4.f); // scale to [0,1] range
}

// 3x3 normal variance: mean(1 - |dot(n_i, n_center)|)
float normalVar3x3(int2 p, bool useGuide)
{
    float3 nc = useGuide ? sampleGuideN(p) : sampleFaceN(p);
    nc = normalize(nc + float3(0.f, 0.f, 1e-6f)); // guard zero-length
    float acc = 0.f;
    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            if (dx == 0 && dy == 0) continue;
            float3 ni = useGuide
                ? sampleGuideN(p + int2(dx, dy))
                : sampleFaceN(p + int2(dx, dy));
            ni = normalize(ni + float3(0.f, 0.f, 1e-6f));
            acc += 1.f - abs(dot(nc, ni));
        }
    }
    return saturate(acc / 8.f);
}

// task-aware overlay color heuristic (per pixel, GPU side)
bool isEnemyColor(float3 c)
{
    float maxC = max(c.r, max(c.g, c.b)) + 1e-6f;
    return (c.r > enemyRedThreshold) && ((c.r / maxC) > 0.9f) && (c.g < (c.r * 0.4f));
}

bool isItemColor(float3 c)
{
    float maxC = max(c.r, max(c.g, c.b)) + 1e-6f;
    bool greenDom = (c.g > itemGBThreshold) && ((c.g / maxC) > 0.9f) && (c.r < (c.g * 0.4f));
    bool blueDom  = (c.b > itemGBThreshold) && ((c.b / maxC) > 0.9f) && (c.r < (c.b * 0.4f));
    return greenDom || blueDom;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim)) return;
    int2 p = int2(pixel);
    float2 uv = (float2(pixel) + 0.5f) * invFrameDim;

    // ---- 중심 픽셀 색상 카테고리 기록 (스레드 (0,0) 만) ----
    if (all(pixel == uint2(0u, 0u)))
    {
        int2 cp = int2(centerUV * float2(frameDim));
        float3 cc = sampleAlbedo(cp);
        uint cat = 0u;
        if (isEnemyColor(cc))      cat = 1u;
        else if (isItemColor(cc))  cat = 2u;
        gCenterCategory[0] = cat;
    }

    // =========================================================
    // 1단계 (1): Image-space max-variance importance
    // =========================================================
    float albedoVar  = scalarVar3x3(p, true);
    float metVar     = scalarVar3x3(p, false);
    float guideNVar  = normalVar3x3(p, true);
    float faceNVar   = normalVar3x3(p, false);
    float glossiness = 1.f - sampleRough(p);  // rough=1 -> gloss=0

    float imageImp = max(albedoVar,
                    max(metVar,
                    max(guideNVar,
                    max(faceNVar, glossiness))));

    // =========================================================
    // 1단계 (2): Task-Object space importance
    // =========================================================
    float taskImp = 0.f;

    if (activeTask == 1u) // Aiming: center region
    {
        float dist = length(uv - centerUV);
        if (dist < centerRadiusUV) taskImp = taskImportance;
    }
    else if (activeTask == 2u) // Pointing: center region (item)
    {
        float dist = length(uv - centerUV);
        if (dist < centerRadiusUV) taskImp = taskImportance;
    }
    else if (activeTask == 3u) // Steering: lower screen region
    {
        bool steerRegion = (uv.y > steerFloorY);
        if (steerRegion) taskImp = taskImportance;
    }

    // ---- max(image, task) ----
    float importance = max(imageImp, taskImp);

    // =========================================================
    // 2단계 (A): Center weighting  (center=1.0, edge=min)
    //   centerDist in [0, ~0.707]
    // =========================================================
    float cd = sampleCDist(p);
    float t  = saturate(cd / 0.7071068f);
    float centerW = lerp(1.f, centerWeightMin, t);
    importance *= centerW;

    // =========================================================
    // 2단계 (B): Luminance penalty
    // =========================================================
    float L = sampleLum(p);
    if (L < lumLow)
        importance *= saturate(L / max(lumLow, 1e-4f));
    else if (L > lumHigh)
        importance *= saturate((1.f - L) / max(1.f - lumHigh, 1e-4f));

    // =========================================================
    // 2단계 (C): Motion penalty
    //   w = (1 / (1 + (A*v)^B))^C
    // =========================================================
    float v   = sampleMotion(p);
    float av  = max(motionA * v, 0.f);
    float denom = 1.f + pow(av + 1e-6f, motionB);
    float mw  = pow(max(1.f / denom, 1e-6f), motionC);
    importance *= mw;

    importance = saturate(importance);

    // ---- debug view selection ----
    float outScalar = importance;
    if (debugView == 2u)      outScalar = saturate(imageImp);
    else if (debugView == 3u) outScalar = saturate(taskImp);

    gImportance[pixel] = outScalar;

#if HAS_IMPORTANCE_VIS
    if (hasImportanceVis != 0u)
    {
        float3 col = float3(outScalar, outScalar, outScalar);
        if (debugView == 1u) // task overlay
        {
            float3 tint = float3(1.f, 1.f, 1.f);
            if      (activeTask == 1u) tint = float3(1.0f, 0.2f, 0.2f); // Aim: red
            else if (activeTask == 2u) tint = float3(0.2f, 1.0f, 0.4f); // Point: green
            else if (activeTask == 3u) tint = float3(0.3f, 0.6f, 1.0f); // Steer: blue
            col = lerp(float3(outScalar, outScalar, outScalar), tint, taskImp * 0.7f);
        }
        gImportanceVis[pixel] = float4(col, 1.f);
    }
#endif
}
