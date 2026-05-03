from falcor import *
import math

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

    # 5. ErrorMeasurePass 연결
    # AccumulatePass.output -> ErrorMeasurePass.Source  (평가 대상 이미지)
    # GBufferRT.linearZ    -> ErrorMeasurePass.WorldPosition  (배경 제거용)
    # ErrorMeasurePass.Output -> ToneMapper.src  (파이프라인 유지)
    g.addEdge("AccumulatePass.output",  "ErrorMeasurePass.Source")
    g.addEdge("GBufferRT.linearZ",      "ErrorMeasurePass.WorldPosition")
    g.addEdge("ErrorMeasurePass.Output", "ToneMapper.src")

    # --- Output -----------------------------------------------------------
    # 기존 렌더링 결과 출력 유지 (ErrorMeasurePass 는 평가용으로만 사용)
    g.markOutput("ToneMapper.dst")
    # 레퍼런스 이미지 캡처용
    g.markOutput("AccumulatePass.output")
    # Importance Map 시각화 출력 등록
    g.markOutput("ImportancePass.importanceVis")
    # Sample Count Map 시각화 출력 등록
    g.markOutput("SampleCountPass.sampleCountVis")

    return g

AdaptiveGraph = render_graph_AdaptivePathTracer()
try: m.addGraph(AdaptiveGraph)
except NameError: None


'''
time = 0.0

def onFrameRender(ctx, state):
    global time
    time += ctx.frameTime

    scene = state.getScene()
    if scene is None:
        return

    enemy = scene.getNode("enemy")
    if enemy is not None:  
        x = math.sin(time) * 2.0
        enemy.setTransform(
            translate(float3(x, 0, 3))
        )
'''

time = 0.0

# 구 왕복 파라미터
# 카메라 fov 및 z=2 위치 기준으로 벽(x=±2.8) 안쪽에서 왕복
ENEMY_AMPLITUDE = 1.8   # 좌우 진폭 (단위: m), 벽 x=±3 안쪽
ENEMY_SPEED     = 1.2   # 왕복 속도 (rad/s), 값이 클수록 빠름
ENEMY_Y         = 1.7   # 카메라 눈높이와 동일하게 고정
ENEMY_Z         = 2.0   # 카메라(z=-5)와 target(z=0) 사이 정면

def onFrameRender(ctx, state):
    global time
    time += ctx.frameTime
    print(f"frame time={time:.2f}")   # 콘솔에 찍히는지 확인

    scene = state.getScene()
    if scene is None:
        return

    enemy = scene.getNode("enemy")
    if enemy is not None:
        # sin 파형으로 부드러운 좌우 왕복
        x = math.sin(time * ENEMY_SPEED) * ENEMY_AMPLITUDE
        enemy.setTransform(Transform(
            translation=float3(x, ENEMY_Y, ENEMY_Z))
        )