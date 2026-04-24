from falcor import *

def render_graph_AdaptivePathTracer():
    g = RenderGraph("AdaptivePathTracer")

    # --- Passes -----------------------------------------------------------
    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")

    ImportancePass = createPass("ImportancePass")
    g.addPass(ImportancePass, "ImportancePass")

    SampleCountPass = createPass("SampleCountPass")
    g.addPass(SampleCountPass, "SampleCountPass")

    PathTracer = createPass("PathTracer", {'samplesPerPixel': 1})
    g.addPass(PathTracer, "PathTracer")

    AccumulatePass = createPass("AccumulatePass")
    g.addPass(AccumulatePass, "AccumulatePass")

    # [요구사항 1] ErrorMeasurePass 추가: PSNR 기반 정량 평가를 위해 그래프에 통합
    # ComputeSquaredDifference=True 로 MSE 모드 활성화, IgnoreBackground=True 로 배경 제외
    ErrorMeasurePass = createPass("ErrorMeasurePass", {
        'ComputeSquaredDifference': True,   # MSE(L2) 모드 사용
        'ComputeAverage': True,             # RGB 평균 오차 계산
        'IgnoreBackground': True,           # WorldPosition 입력으로 배경 픽셀 제외
        'UseLoadedReference': False,        # Reference 입력 채널 사용 (외부 파일 불필요)
    })
    g.addPass(ErrorMeasurePass, "ErrorMeasurePass")

    ToneMapper = createPass("ToneMapper")
    g.addPass(ToneMapper, "ToneMapper")

    # --- Edges ------------------------------------------------------------
    # 1. Primary Visibility
    g.addEdge("GBufferRT.vbuffer", "PathTracer.vbuffer")

    # 2. Importance Pipeline (GBuffer -> Importance -> SampleCount)
    g.addEdge("GBufferRT.guideNormalW",   "ImportancePass.guideNormalW")
    g.addEdge("GBufferRT.diffuseOpacity", "ImportancePass.diffuseOpacity")
    g.addEdge("GBufferRT.linearZ",        "ImportancePass.linearZ")
    g.addEdge("GBufferRT.shadowCount",        "ImportancePass.shadowCount")

    g.addEdge("ImportancePass.importance",  "SampleCountPass.importance")
    g.addEdge("SampleCountPass.sampleCount", "PathTracer.sampleCount")

    # 3. PathTracer Connections
    g.addEdge("GBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("GBufferRT.mvecW", "PathTracer.mvec")

    # 4. Accumulation
    g.addEdge("PathTracer.color",      "AccumulatePass.input")

    # 5. [요구사항 1] ErrorMeasurePass 연결
    #    - AccumulatePass.output -> ErrorMeasurePass.Source  (평가 대상 이미지)
    #    - GBufferRT.linearZ    -> ErrorMeasurePass.WorldPosition  (배경 제거용)
    #    - ErrorMeasurePass.Output -> ToneMapper.src  (파이프라인 유지)
    g.addEdge("AccumulatePass.output",  "ErrorMeasurePass.Source")
    g.addEdge("GBufferRT.linearZ",      "ErrorMeasurePass.WorldPosition")
    g.addEdge("ErrorMeasurePass.Output", "ToneMapper.src")

    # --- Output -----------------------------------------------------------
    # 기존 렌더링 결과 출력 유지 (ErrorMeasurePass 는 평가용으로만 사용)
    g.markOutput("ToneMapper.dst")
    # Importance Map 시각화 출력 등록
    g.markOutput("ImportancePass.importanceVis")
    # Sample Count Map 시각화 출력 등록
    g.markOutput("SampleCountPass.sampleCountVis")

    return g

AdaptiveGraph = render_graph_AdaptivePathTracer()
try: m.addGraph(AdaptiveGraph)
except NameError: None
