from falcor import *

def render_graph_PathTracerBaseline():
    g = RenderGraph("PathTracerBaseline")

    # --- Passes -----------------------------------------------------------
    PathTracer = createPass("PathTracer", {'samplesPerPixel': 4})
    g.addPass(PathTracer, "PathTracer")

    GBufferRT = createPass("GBufferRT")
    g.addPass(GBufferRT, "GBufferRT")

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

    ErrorMeasurePass = createPass("ErrorMeasurePass", {
        'ComputeSquaredDifference': True,   # MSE(L2) 모드 사용
        'ComputeAverage': True,             # RGB 평균 오차 계산
        'IgnoreBackground': True,           # WorldPosition 입력으로 배경 픽셀 제외
        'UseLoadedReference': False,        # Reference 입력 채널 사용 (외부 파일 불필요)
    })
    g.addPass(ErrorMeasurePass, "ErrorMeasurePass")

    OverlayPass = createPass("OverlayPass")
    g.addPass(OverlayPass, "OverlayPass")

    # --- Edges ------------------------------------------------------------
    g.addEdge("GBufferRT.vbuffer", "PathTracer.vbuffer")
    g.addEdge("GBufferRT.viewW", "PathTracer.viewW")
    g.addEdge("GBufferRT.mvecW", "PathTracer.mvec")

    g.addEdge("PathTracer.color",      "SVGFPass.Color")
    g.addEdge("PathTracer.albedo",     "SVGFPass.Albedo")
    g.addEdge("GBufferRT.emissive",    "SVGFPass.Emission")             
    g.addEdge("GBufferRT.mvecW",       "SVGFPass.MotionVec")
    g.addEdge("GBufferRT.guideNormalW","SVGFPass.WorldNormal")
    g.addEdge("GBufferRT.posW",        "SVGFPass.WorldPosition")       
    g.addEdge("GBufferRT.linearZ",     "SVGFPass.LinearZ") 

    g.addEdge("SVGFPass.Filtered image", "ErrorMeasurePass.Source")
    g.addEdge("GBufferRT.linearZ",       "ErrorMeasurePass.WorldPosition")

    g.addEdge("ErrorMeasurePass.Output",  "OverlayPass.input")

    # --- Output -----------------------------------------------------------
    g.markOutput("OverlayPass.output")
    return g

PathTracerBaseline = render_graph_PathTracerBaseline()
try: m.addGraph(PathTracerBaseline)
except NameError: None

# ============================================================
# 캡처 설정
# ============================================================
ENABLE_AUTO_CAPTURE = False   # False로 바꾸면 자동 캡처 비활성화
CAPTURE_EVERY_N_FRAMES = 10   # 매 N프레임마다 캡처 (1 = 매 프레임)
CAPTURE_TOTAL_FRAMES = 600   # 총 프레임 수
FIXED_FRAMERATE = 60         # 카메라 애니메이션 고정 fps

try:
    m.frameCapture.outputDir = "C:/Users/bg001/Desktop/Falcor/Results/baseline"   # 절대 경로
    m.frameCapture.baseFilename = "base"

    if ENABLE_AUTO_CAPTURE:
        m.clock.framerate = FIXED_FRAMERATE
        m.clock.time = 0        # 시각 리셋
        m.clock.frame = 0       # 프레임 카운터 리셋
    
        # CAPTURE_TOTAL_FRAMES 도달 시 자동 종료
        m.clock.exitFrame = CAPTURE_TOTAL_FRAMES

        # 캡처할 프레임 번호 목록을 미리 등록
        frames_to_capture = list(range(0, CAPTURE_TOTAL_FRAMES, CAPTURE_EVERY_N_FRAMES))
        m.frameCapture.addFrames(PathTracerBaseline, frames_to_capture)

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
    output_dir = OUTPUT_BASE + "/baseline_" + SCENARIO
    m.frameCapture.outputDir   = output_dir
    m.frameCapture.baseFilename = "timing_base"

    if ENABLE_FRAMETIME_MEASUREMENT:
        # 현재 프레임 카운터 기준으로 절대 프레임 번호를 계산한다.
        # m.clock.frame은 read-only여서 리셋이 안 되므로, 현재값에서 오프셋으로 계산.
        base = m.clock.frame
        start_frame = base + WARMUP_FRAMES
        end_frame   = base + WARMUP_FRAMES + MEASURE_FRAMES

        # 워밍업 직후 프레임(측정 시작)과 측정 완료 프레임(측정 종료) 2장만 캡처
        # analyze_frame_time.py가 두 파일의 mtime 차이로 평균 frame time을 계산함
        m.frameCapture.addFrames(PathTracerBaseline, [start_frame, end_frame])

        # exitFrame을 현재 카운터 이후로 설정 → 즉시 종료 방지
        m.clock.exitFrame = end_frame + 1

except NameError:
    None