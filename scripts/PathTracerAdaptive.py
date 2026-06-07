from falcor import *
# import math

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

    # AccumulatePass 삭제: 오브젝트 애니메이션으로 매 프레임 누적이 초기화되어 효과 없음
    # 대신 SVGFPass로 시간적 디노이징 수행 (motion vector 기반 재투영)
    SVGFPass = createPass("SVGFPass", {
        'Enabled': True,
        'Iterations': 4,
        'FeedbackTap': 1,
        'VarianceEpsilon': 1e-4,
        'PhiColor': 10.0,
        'PhiNormal': 128.0,
        'Alpha': 0.05,
        'MomentsAlpha': 0.5,
    })
    g.addPass(SVGFPass, "SVGFPass")

    # [요구사항 1] ErrorMeasurePass 추가: PSNR 기반 정량 평가를 위해 그래프에 통합
    # ComputeSquaredDifference=True 로 MSE 모드 활성화, IgnoreBackground=True 로 배경 제외
    ErrorMeasurePass = createPass("ErrorMeasurePass", {
        'ComputeSquaredDifference': True,   # MSE(L2) 모드 사용
        'ComputeAverage': True,             # RGB 평균 오차 계산
        'IgnoreBackground': True,           # WorldPosition 입력으로 배경 픽셀 제외
        'UseLoadedReference': False,        # Reference 입력 채널 사용 (외부 파일 불필요)
    })
    g.addPass(ErrorMeasurePass, "ErrorMeasurePass")

    # ToneMapper 삭제: 최종 출력을 float32 HDR(.exr)로 보존하기 위해 제거

    # OverlayPass: 화면 중앙 크로스헤어 오버레이 (ImGui DrawList 방식)
    OverlayPass = createPass("OverlayPass")
    g.addPass(OverlayPass, "OverlayPass")

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

    # 4. SVGF 연결: PathTracer 출력 + GBuffer 보조 데이터
    g.addEdge("PathTracer.color",      "SVGFPass.Color")
    g.addEdge("PathTracer.albedo",     "SVGFPass.Albedo")
    g.addEdge("GBufferRT.emissive",    "SVGFPass.Emission")             
    g.addEdge("GBufferRT.mvecW",       "SVGFPass.MotionVec")
    g.addEdge("GBufferRT.guideNormalW","SVGFPass.WorldNormal")
    g.addEdge("GBufferRT.posW",        "SVGFPass.WorldPosition")       
    g.addEdge("GBufferRT.linearZ",     "SVGFPass.LinearZ")              

    # 5. ErrorMeasurePass 연결
    # SVGFPass.Filtered image -> ErrorMeasurePass.Source  (평가 대상 이미지)
    # GBufferRT.linearZ       -> ErrorMeasurePass.WorldPosition  (배경 제거용)
    g.addEdge("SVGFPass.Filtered image", "ErrorMeasurePass.Source")
    g.addEdge("GBufferRT.linearZ",       "ErrorMeasurePass.WorldPosition")

    # 6. OverlayPass 연결: ErrorMeasurePass 출력을 받아 크로스헤어 오버레이 후 최종 출력
    g.addEdge("ErrorMeasurePass.Output",  "OverlayPass.input")

    # --- Output -----------------------------------------------------------
    # 최종 출력: OverlayPass.output (크로스헤어 오버레이 적용)
    g.markOutput("OverlayPass.output")
    # Raw importance map (R32Float) — ROI 마스크 추출용
    g.markOutput("ImportancePass.importance")
    # Importance Map 시각화 출력 등록
    g.markOutput("ImportancePass.importanceVis")
    # Sample Count Map 시각화 출력 등록
    g.markOutput("SampleCountPass.sampleCountVis")

    return g

AdaptiveGraph = render_graph_AdaptivePathTracer()
try: m.addGraph(AdaptiveGraph)
except NameError: None

# ============================================================
# 캡처 설정
# ============================================================
ENABLE_AUTO_CAPTURE = False   # False로 바꾸면 자동 캡처 비활성화
CAPTURE_EVERY_N_FRAMES = 10   # 매 N프레임마다 캡처 (1 = 매 프레임)
CAPTURE_TOTAL_FRAMES = 600   # 총 프레임 수
FIXED_FRAMERATE = 60         # 카메라 애니메이션 고정 fps

try:
    m.frameCapture.outputDir = "C:/Users/bg001/Desktop/Falcor/Results/test"   # 절대 경로
    m.frameCapture.baseFilename = "test"

    if ENABLE_AUTO_CAPTURE:
        m.clock.framerate = FIXED_FRAMERATE
        m.clock.time = 0        # 시각 리셋
        m.clock.frame = 0       # 프레임 카운터 리셋
    
        # CAPTURE_TOTAL_FRAMES 도달 시 자동 종료
        m.clock.exitFrame = CAPTURE_TOTAL_FRAMES

        # 캡처할 프레임 번호 목록을 미리 등록
        frames_to_capture = list(range(0, CAPTURE_TOTAL_FRAMES, CAPTURE_EVERY_N_FRAMES))
        m.frameCapture.addFrames(AdaptiveGraph, frames_to_capture)

except NameError:
    None  # 렌더 그래프 에디터에서 단독 로드 시 m이 없을 때 무시

# ============================================================
# 타이밍 측정 설정
# ============================================================
ENABLE_FRAMETIME_MEASUREMENT = False   # False로 바꾸면 타이밍 측정 비활성화
WARMUP_FRAMES  = 100   # 초기 오버헤드 제외용 워밍업 프레임 수
MEASURE_FRAMES = 200   # 실제 측정 구간 프레임 수

# 시나리오에 따라 변경: "aiming" 또는 "pointing"
#SCENARIO = "aiming"
SCENARIO = "pointing"

OUTPUT_BASE = "C:/Users/bg001/Desktop/Falcor/Results/timing"

try:
    output_dir = OUTPUT_BASE + "/adaptive_" + SCENARIO
    m.frameCapture.outputDir    = output_dir
    m.frameCapture.baseFilename = "timing_adaptive"

    if ENABLE_FRAMETIME_MEASUREMENT:
        # 현재 프레임 카운터 기준으로 절대 프레임 번호를 계산한다.
        # m.clock.frame은 read-only여서 리셋이 안 되므로, 현재값에서 오프셋으로 계산.
        base = m.clock.frame
        start_frame = base + WARMUP_FRAMES
        end_frame   = base + WARMUP_FRAMES + MEASURE_FRAMES

        # 워밍업 직후 프레임(측정 시작)과 측정 완료 프레임(측정 종료) 2장만 캡처
        # analyze_frame_time.py가 두 파일의 mtime 차이로 평균 frame time을 계산함
        m.frameCapture.addFrames(AdaptiveGraph, [start_frame, end_frame])

        # exitFrame을 현재 카운터 이후로 설정 → 즉시 종료 방지
        m.clock.exitFrame = end_frame + 1

    '''
    if ENABLE_FRAMETIME_MEASUREMENT:
        # 워밍업 직후 프레임(측정 시작)과 측정 완료 프레임(측정 종료) 2장만 캡처
        # analyze_frame_time.py가 두 파일의 mtime 차이로 평균 frame time을 계산함
        start_frame = WARMUP_FRAMES
        end_frame   = WARMUP_FRAMES + MEASURE_FRAMES
        m.frameCapture.addFrames(AdaptiveGraph, [start_frame, end_frame])

        # 측정 완료 후 자동 종료 (end_frame 렌더링 보장을 위해 +1)
        m.clock.exitFrame = end_frame + 1

        # 고정 프레임레이트 비활성화: 실제 GPU 렌더 속도로 동작
        # (m.clock.framerate 미설정 = 실시간 모드)
        m.clock.time  = 0
        m.clock.frame = 0
    '''

except NameError:
    None