from falcor import *
import os   # 캡처 폴더 자동 생성을 위해 추가

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
    m.frameCapture.outputDir = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis/baseline"   # 절대 경로
    os.makedirs(m.frameCapture.outputDir, exist_ok=True)   # 캡처 폴더가 없으면 생성
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
# [방법] 워밍업 후 500프레임 구간 5개를 연속 캡처(경계점 6장).
#        각 구간 = mtime 차이 / 500 → 5개 값으로 mean ± std 산출.
#        구간 끝 캡처 스톨은 다음 구간 시작점에서 상쇄되므로 구간 내 오차 ~13%.
# ============================================================
ENABLE_FRAMETIME_MEASUREMENT = False   # True로 바꾸면 타이밍 측정 활성화

WARMUP_FRAMES   = 100   # 초기 오버헤드 제외용 워밍업 프레임 수
MEASURE_FRAMES  = 500   # 구간당 프레임 수
NUM_INTERVALS   = 5     # 구간 수 (캡처 파일 = NUM_INTERVALS + 1 = 6장)

# 시나리오에 따라 변경: "aiming" 또는 "pointing"
SCENARIO = "aiming"
#SCENARIO = "pointing"

OUTPUT_BASE = "C:/Users/bg001/Desktop/Falcor/Results/frame_time_analysis"

try:
    if ENABLE_FRAMETIME_MEASUREMENT:
        # outputDir/baseFilename 설정을 if 블록 안으로 이동:
        # 비활성화 시 위 '캡처 설정'의 출력 경로/파일명을 덮어쓰지 않도록 함
        output_dir = OUTPUT_BASE + "/baseline_" + SCENARIO
        os.makedirs(output_dir, exist_ok=True)   # 캡처 폴더가 없으면 생성
        m.frameCapture.outputDir    = output_dir
        m.frameCapture.baseFilename = "timing_base"

        # m.clock.frame은 read-only라 리셋 불가 → 현재값 기준 절대 프레임 번호 계산
        base = m.clock.frame

        # 구간 경계점 6장 캡처: [W, W+500, W+1000, W+1500, W+2000, W+2500]
        capture_frames = [base + WARMUP_FRAMES + i * MEASURE_FRAMES
                          for i in range(NUM_INTERVALS + 1)]
        m.frameCapture.addFrames(PathTracerBaseline, capture_frames)
        m.clock.exitFrame = capture_frames[-1] + 1

except NameError:
    None

