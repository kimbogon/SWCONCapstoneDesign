"""
analyze_frame_time.py — 1.2 frame time 측정 및 보고

[원리]
  Falcor가 캡처한 두 파일(워밍업 직후 프레임 / 측정 완료 프레임)의
  파일 시스템 mtime 차이를 MEASURE_FRAMES으로 나눠 평균 frame time을 계산한다.

[전제]
  PathTracerBaselineTiming.py / PathTracerAdaptiveTiming.py 실행 후
  아래 OUTPUT_BASE 아래에 캡처 파일이 생성되어 있어야 한다.

  디렉터리 구조 (프레임 번호는 캡처 오프셋에 따라 달라짐):
    {OUTPUT_BASE}/baseline_aiming/          timing_base.OverlayPass.output.{N}.exr
                                            timing_base.OverlayPass.output.{N+200}.exr
    {OUTPUT_BASE}/baseline_pointing/  ...
    {OUTPUT_BASE}/adaptive_aiming/          timing_adaptive.OverlayPass.output.{N}.exr
                                            timing_adaptive.OverlayPass.output.{N+200}.exr
    {OUTPUT_BASE}/adaptive_pointing/  ...

  정확한 프레임 번호(N)는 매 실행마다 달라질 수 있으며,
  MEASURE_FRAMES(200) 차이가 나는 쌍을 자동으로 탐색한다.

[출력]
  - 콘솔: 시나리오별 평균 frame time 표
  - frame_time_results.csv
  - frame_time_comparison.png (bar chart)
"""

import os
import re
import csv
import sys
import glob

# ============================================================
# 설정
# ============================================================
OUTPUT_BASE    = "C:/Users/bg001/Desktop/Falcor/Results/timing"
MEASURE_FRAMES = 200   # 두 캡처 파일 사이의 프레임 차이 (시작 프레임 번호는 자동 탐색)

# 결과물 저장 경로
RESULTS_DIR = OUTPUT_BASE
CSV_PATH    = os.path.join(RESULTS_DIR, "frame_time_results.csv")
PLOT_PATH   = os.path.join(RESULTS_DIR, "frame_time_comparison.png")

# ============================================================
# mtime 기반 평균 frame time 계산
# ============================================================
def find_frame_pair(run_dir: str, base_filename: str, frame_gap: int):
    """
    run_dir 안의 캡처 파일 전체를 스캔해 frame_gap 만큼 차이나는
    (f_start_path, f_end_path, start_no, end_no) 쌍을 반환한다.
    쌍이 없으면 (None, None, None, None) 반환.

    Falcor 캡처 파일명 패턴: {baseFilename}.{output_name}.{frame}.exr
    output_name에 점(.)이 포함될 수 있으므로 마지막 숫자 토큰을 프레임 번호로 파싱한다.
    """
    pattern = os.path.join(run_dir, f"{base_filename}.*.exr")
    all_files = glob.glob(pattern)

    # 파일명 끝의 숫자(.{frame}.exr)를 프레임 번호로 추출
    frame_map: dict[int, str] = {}
    for filepath in all_files:
        m = re.search(r'\.(\d+)\.exr$', os.path.basename(filepath))
        if m:
            frame_no = int(m.group(1))
            # 같은 프레임 번호가 여러 파일이면 가장 최근 것을 사용
            if frame_no not in frame_map or \
               os.path.getmtime(filepath) > os.path.getmtime(frame_map[frame_no]):
                frame_map[frame_no] = filepath

    # 오름차순으로 순회하며 frame_gap 차이 쌍을 찾음
    for start_no in sorted(frame_map):
        end_no = start_no + frame_gap
        if end_no in frame_map:
            return frame_map[start_no], frame_map[end_no], start_no, end_no

    return None, None, None, None


def compute_avg_frame_time(run_dir: str, base_filename: str,
                           frame_gap: int) -> float:
    """
    run_dir 안에서 frame_gap 만큼 차이나는 캡처 파일 쌍을 자동으로 찾아
    mtime 차이 / frame_gap 을 ms 단위로 반환한다.
    파일 쌍을 못 찾으면 None 반환.
    """
    f_start, f_end, start_no, end_no = find_frame_pair(run_dir, base_filename, frame_gap)

    if f_start is None or f_end is None:
        print(f"  [경고] {run_dir}: {frame_gap} 프레임 차이 쌍을 찾지 못했음 → 건너뜀")
        return None

    t_start = os.path.getmtime(f_start)
    t_end   = os.path.getmtime(f_end)
    elapsed_ms = (t_end - t_start) * 1000.0
    avg_ms     = elapsed_ms / frame_gap

    print(f"  시작 파일 : {os.path.basename(f_start)}  (frame {start_no})")
    print(f"  종료 파일 : {os.path.basename(f_end)}  (frame {end_no})")
    print(f"  경과 시간 : {elapsed_ms:.1f} ms / {frame_gap} 프레임")
    print(f"  평균 frame time : {avg_ms:.3f} ms  ({1000.0/avg_ms:.1f} FPS)")
    return avg_ms


# ============================================================
# 측정 실행
# ============================================================
RUNS = [
    # (파이프라인, 시나리오, 서브디렉터리, 기본파일명)
    ("Baseline",  "Aiming",            "baseline_aiming",            "timing_base"),
    ("Baseline",  "Pointing",          "baseline_pointing",          "timing_base"),
    ("Adaptive",  "Aiming",            "adaptive_aiming",            "timing_adaptive"),
    ("Adaptive",  "Pointing",          "adaptive_pointing",          "timing_adaptive"),
]

results = []
print("=" * 60)
print("Frame Time 분석")
print("=" * 60)

for pipeline, scenario, subdir, base_fn in RUNS:
    run_dir = os.path.join(OUTPUT_BASE, subdir)
    print(f"\n[{pipeline} / {scenario}]  →  {run_dir}")

    if not os.path.isdir(run_dir):
        print(f"  [경고] 디렉터리 없음: {run_dir}")
        avg_ms = None
    else:
        avg_ms = compute_avg_frame_time(run_dir, base_fn, MEASURE_FRAMES)

    results.append({
        "pipeline": pipeline,
        "scenario": scenario,
        "avg_frame_time_ms": round(avg_ms, 3) if avg_ms is not None else "N/A",
        "fps": round(1000.0 / avg_ms, 1) if avg_ms is not None else "N/A",
    })

# ============================================================
# 결과 출력
# ============================================================
print("\n" + "=" * 60)
print(f"{'Pipeline':<12} {'Scenario':<22} {'Avg Frame Time':>16}  {'FPS':>8}")
print("-" * 60)
for r in results:
    ft  = f"{r['avg_frame_time_ms']} ms" if r['avg_frame_time_ms'] != "N/A" else "N/A"
    fps = str(r['fps'])
    print(f"{r['pipeline']:<12} {r['scenario']:<22} {ft:>16}  {fps:>8}")
print("=" * 60)

# ============================================================
# CSV 저장
# ============================================================
os.makedirs(RESULTS_DIR, exist_ok=True)
with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=["pipeline", "scenario",
                                           "avg_frame_time_ms", "fps"])
    writer.writeheader()
    writer.writerows(results)
print(f"\nCSV 저장: {CSV_PATH}")

# ============================================================
# Bar chart 생성
# ============================================================
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    scenarios  = ["Aiming", "Pointing"]
    pipelines  = ["Baseline", "Adaptive"]
    colors     = {"Baseline": "#4C72B0", "Adaptive": "#DD8452"}

    # 유효한 측정값만 추출
    data = {}
    for r in results:
        key = (r["pipeline"], r["scenario"])
        val = r["avg_frame_time_ms"]
        data[key] = float(val) if val != "N/A" else 0.0

    x      = np.arange(len(scenarios))
    width  = 0.35
    fig, ax = plt.subplots(figsize=(8, 5))

    for i, pipe in enumerate(pipelines):
        vals = [data.get((pipe, sc), 0.0) for sc in scenarios]
        bars = ax.bar(x + (i - 0.5) * width, vals, width,
                      label=pipe, color=colors[pipe], alpha=0.85)
        # 막대 위에 수치 표시
        for bar, v in zip(bars, vals):
            if v > 0:
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + 0.3,
                        f"{v:.1f} ms", ha="center", va="bottom", fontsize=9)

    ax.set_xlabel("Scenario")
    ax.set_ylabel("Avg Frame Time (ms)")
    ax.set_title("Frame Time Comparison: Baseline vs Adaptive")
    ax.set_xticks(x)
    ax.set_xticklabels(scenarios)
    ax.legend()
    ax.set_ylim(bottom=0)
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    plt.tight_layout()
    plt.savefig(PLOT_PATH, dpi=150)
    print(f"Bar chart 저장: {PLOT_PATH}")

except ImportError:
    print("[알림] matplotlib 없음 — bar chart 생성 건너뜀 (pip install matplotlib)")
