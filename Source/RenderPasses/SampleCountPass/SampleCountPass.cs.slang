/***************************************************************************
 # SampleCountPass.cs.slang
 #
 # [변경 내용] 고정 임계값 방식 → 평균 중요도 기반 샘플 분배 방식으로 교체.
 #
 # 알고리즘:
 #   1. reduceMain  : 타일(16×16)별 importance 부분합을 gTilePartialSums 에 기록.
 #   2. finalizeMain: 부분합 전체를 합산하여 Avg_Imp 를 gAvgImpBuf[0] 에 기록.
 #   3. main        : S_p = clamp(round(I_p / Avg_Imp * S_target), 1, maxSamples)
 #
 # [디버그 시각화]
 # gSampleCountVis: sample count 를 (count / maxSamples) 로 정규화한 RGBA8Unorm 출력.
 # HAS_SAMPLE_COUNT_VIS 가 1 일 때만 기록한다.
 **************************************************************************/

// ---- guard defines (C++ 측에서 addDefine 으로 주입됨) -------------------
#ifndef HAS_SAMPLE_COUNT_VIS
#define HAS_SAMPLE_COUNT_VIS 0
#endif

// =========================================================================
// Constant buffer  (세 커널 모두 공유)
// =========================================================================
cbuffer CB
{
    uint2  frameDim;    ///< 렌더 해상도 (픽셀 단위)
    uint   sTarget;     ///< 목표 평균 spp  (예: 4)
    uint   maxSamples;  ///< 픽셀당 최대 spp (예: 16)
    uint   numTiles;    ///< 타일 총 개수 = ceil(W/16) * ceil(H/16)  (finalizeMain 전용)
    uint   totalPixels; ///< 전체 픽셀 수 = W * H                    (finalizeMain 전용)
};

// =========================================================================
// Resources
// =========================================================================

/// importance 입력 (ImportancePass 출력, R32Float).
Texture2D<float>    gImportance;

/// Pass 1 → Pass 2 용 타일별 부분합 버퍼 (RWStructuredBuffer<float>).
/// 크기 = numTiles (C++ 에서 할당).
RWStructuredBuffer<float> gTilePartialSums;

/// Pass 2 결과 : gAvgImpBuf[0] = Avg_Imp (float).
RWStructuredBuffer<float> gAvgImpBuf;

/// 실제 sample count 출력 (R8Uint) — PathTracer 가 Texture2D<uint> 로 읽는다.
RWTexture2D<uint>   gOutput;

/// [디버그 시각화] 정규화된 sample count 를 RGBA8Unorm 으로 기록.
RWTexture2D<float4> gSampleCountVis;

// =========================================================================
// Pass 1 : 타일별 부분합  (reduceMain)
// =========================================================================
// 그룹 크기 16×16 = 256 스레드.
// 각 그룹이 자신의 타일에 속하는 픽셀들의 importance 합을 계산한 뒤,
// gTilePartialSums[groupLinearId] 에 기록한다.
// =========================================================================

groupshared float gs_partialSum[256]; ///< 그룹 공유 메모리 (16×16 스레드)

[numthreads(16, 16, 1)]
void reduceMain(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupId          : SV_GroupID,
    uint  groupIndex       : SV_GroupIndex)   // 0..255
{
    // ---- 픽셀 importance 읽기 ----
    uint2 pixel = dispatchThreadId.xy;
    float val = 0.f;
    if (all(pixel < frameDim))
        val = gImportance[pixel];  // 범위 밖 픽셀은 0 으로 처리 (평균에 영향 최소화)

    gs_partialSum[groupIndex] = val;
    GroupMemoryBarrierWithGroupSync();

    // ---- 공유 메모리 내 parallel reduction (256→1) ----
    // 2의 거듭제곱 stride 로 절반씩 합산한다.
    [unroll]
    for (uint stride = 128u; stride >= 1u; stride >>= 1u)
    {
        if (groupIndex < stride)
            gs_partialSum[groupIndex] += gs_partialSum[groupIndex + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    // ---- 그룹 대표 스레드가 부분합을 전역 버퍼에 기록 ----
    if (groupIndex == 0u)
    {
        // 타일 인덱스 = groupId.y * 타일수_X + groupId.x
        // 타일수_X = ceil(frameDim.x / 16)
        uint tilesX = (frameDim.x + 15u) / 16u;
        uint tileIdx = groupId.y * tilesX + groupId.x;
        gTilePartialSums[tileIdx] = gs_partialSum[0];
    }
}

// =========================================================================
// Pass 2 : 전체 합산 → Avg_Imp 계산  (finalizeMain)
// =========================================================================
// 단일 그룹 1×1×1 로 dispatch 하여 모든 타일 부분합을 CPU 없이 합산한다.
// 단, numTiles 가 크면 루프가 길어진다(성능 트레이드오프).
// 일반적인 1080p(120×68 = 8160 타일) 정도에서는 허용 가능.
// =========================================================================

[numthreads(1, 1, 1)]
void finalizeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    float total = 0.f;
    for (uint i = 0u; i < numTiles; i++)
        total += gTilePartialSums[i];

    // 실제 픽셀 수로 나눠 평균 계산 (타일이 프레임 경계를 넘어 패딩된 부분은 이미 0).
    float avgImp = (totalPixels > 0u) ? (total / float(totalPixels)) : 1.f;

    // Avg_Imp 가 0 에 가까우면 나눗셈 오버플로 방지를 위해 최솟값을 보장한다.
    gAvgImpBuf[0] = max(avgImp, 1e-4f);
}

// =========================================================================
// Pass 3 : 픽셀별 샘플 수 계산  (main)
// =========================================================================

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= frameDim))
        return;

    // ---- Avg_Imp 읽기 (Pass 2 에서 기록한 값) ----
    float avgImp = gAvgImpBuf[0];  // max(avg, 1e-4) 보장됨

    // ---- 픽셀 importance ----
    float imp = gImportance[pixel];

    // ---- S_p = clamp( round(I_p / Avg_Imp * S_target), 1, maxSamples ) ----
    // round() 는 nearest-integer 반올림이다.
    // sTarget 과 maxSamples 는 uint 이므로 float 캐스팅 후 계산한다.
    float rawCount = (imp / avgImp) * float(sTarget);
    uint  count    = clamp(uint(round(rawCount)), 1u, maxSamples);

    // 실제 sample count 기록 (R8Uint — PathTracer 가 읽음).
    gOutput[pixel] = count;

    // [디버그 시각화] sample count 를 [0, 1] 로 정규화하여 밝기로 표현한다.
    //   count / maxSamples:  1spp → 어두움, maxSamples spp → 흰색
#if HAS_SAMPLE_COUNT_VIS
    float normalizedCount = float(count) / float(maxSamples);
    gSampleCountVis[pixel] = float4(normalizedCount, normalizedCount, normalizedCount, 1.f);
#endif
}
