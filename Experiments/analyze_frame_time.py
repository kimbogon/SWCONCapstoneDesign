"""
analyze_frame_time.py — frame time 측정 및 보고

[원리]
  각 시나리오 디렉터리에서 캡처된 6장의 EXR 파일을 프레임 번호 순으로 정렬하고,
  인접한 5개 구간(각 500프레임)의 mtime 차이로 구간별 평균 frame time을 계산한다.
  5개 구간의 mean ± std를 최종 결과로 제시한다.

[전제]
  PathTracerBaseline/Adaptive.py의 ENABLE_FRAMETIME_MEASUREMENT=True 로
  시나리오당 1회 실행하면 아래 구조로 파일이 생성된다.

  디렉터리 구조:
    {OUTPUT_BASE}/baseline_aiming/   timing_base.OverlayPass.output.{N}.exr      (6장)
    {OUTPUT_BASE}/baseline_pointing/ ...
    {OUTPUT_BASE}/test_aiming/       timing_adaptive.OverlayPass.output.{N}.exr  (6장)
    {OUTPUT_BASE}/test_pointing/     ...

[출력]
  - 콘솔: 시나리오별 구간별 frame time 및 mean ± std 표
  - frame_time_results.csv
  - frame_time_comparison.png (오차 막대 포함 bar chart)
"""

import os
import re
import csv
import glob
import statistics

# ============================================================
# 설정
# ============================================================
OUTPUT_BASE    = "C:/Users/bg001/Desktop/SWCONCapstoneDesign/Experiments/frame_time"
MEASURE_FRAMES = 500   # PathTracerBaseline/Adaptive.py의 MEASURE_FRAMES와 일치해야 함

RESULTS_DIR = OUTPUT_BASE
CSV_PATH    = os.path.join(RESULTS_DIR, "frame_time_results.csv")
PLOT_PATH   = os.path.join(RESULTS_DIR, "frame_time_comparison.png")


# ============================================================
# 파일 시퀀스 탐색
# ============================================================
def find_frame_sequence(run_dir: str, base_filename: str):
    """
    run_dir 안의 EXR 파일을 프레임 번호 오름차순으로 정렬해
    [(frame_no, filepath), ...] 리스트를 반환한다.
    """
    pattern   = os.path.join(run_dir, f"{base_filename}.*.exr")
    all_files = glob.glob(pattern)

    frame_map: dict[int, str] = {}
    for filepath in all_files:
        m = re.search(r'\.(\d+)\.exr$', os.path.basename(filepath))
        if m:
            frame_no = int(m.group(1))
            if frame_no not in frame_map or \
               os.path.getmtime(filepath) > os.path.getmtime(frame_map[frame_no]):
                frame_map[frame_no] = filepath

    return sorted(frame_map.items())


# ============================================================
# 구간별 frame time 계산 → mean ± std 반환
# ============================================================
def compute_avg_frame_time(run_dir: str, base_filename: str) -> tuple:
    """
    6장의 EXR로 5개 구간의 frame time(ms)을 계산하고 (mean_ms, std_ms)를 반환한다.
    파일이 2개 미만이면 (None, None) 반환.
    """
    sequence = find_frame_sequence(run_dir, base_filename)

    if len(sequence) < 2:
        print(f"  [경고] {run_dir}: 캡처 파일이 2개 미만 → 건너뜀")
        return None, None

    measurements = []
    for i in range(len(sequence) - 1):
        fn_start, path_start = sequence[i]
        fn_end,   path_end   = sequence[i + 1]
        frame_diff = fn_end - fn_start
        if frame_diff <= 0:
            continue

        elapsed_ms       = (os.path.getmtime(path_end) - os.path.getmtime(path_start)) * 1000.0
        interval_avg_ms  = elapsed_ms / frame_diff
        measurements.append(interval_avg_ms)

        print(f"  구간 {i + 1}: frame {fn_start}→{fn_end} ({frame_diff}f)"
              f"  {elapsed_ms:.1f} ms  →  {interval_avg_ms:.3f} ms/f"
              f"  ({1000.0 / interval_avg_ms:.1f} FPS)")

    if not measurements:
        return None, None

    mean_ms = statistics.mean(measurements)
    std_ms  = statistics.stdev(measurements) if len(measurements) > 1 else 0.0

    print(f"  {'─' * 53}")
    print(f"  N={len(measurements)}  mean={mean_ms:.3f} ms  std={std_ms:.3f} ms"
          f"  ({1000.0 / mean_ms:.1f} FPS)")
    return mean_ms, std_ms


# ============================================================
# 측정 실행
# ============================================================
RUNS = [
    # (파이프라인, 시나리오, 서브디렉터리, 기본파일명)
    ("Baseline", "Aiming",   "baseline_aiming",   "baseline"),
    ("Baseline", "Pointing", "baseline_pointing", "baseline"),
    ("IAPathTracer", "Aiming",   "proposed_aiming",   "proposed"),
    ("IAPathTracer", "Pointing", "proposed_pointing", "proposed"),
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
        mean_ms = std_ms = None
    else:
        mean_ms, std_ms = compute_avg_frame_time(run_dir, base_fn)

    results.append({
        "pipeline":           pipeline,
        "scenario":           scenario,
        "mean_frame_time_ms": round(mean_ms, 3) if mean_ms is not None else "N/A",
        "std_frame_time_ms":  round(std_ms,  3) if std_ms  is not None else "N/A",
        "fps":                round(1000.0 / mean_ms, 1) if mean_ms is not None else "N/A",
    })


# ============================================================
# 결과 출력
# ============================================================
print("\n" + "=" * 60)
print(f"{'Pipeline':<12} {'Scenario':<22} {'Mean Frame Time':>16}  {'Std':>10}  {'FPS':>8}")
print("-" * 60)
for r in results:
    ft  = f"{r['mean_frame_time_ms']} ms" if r['mean_frame_time_ms'] != "N/A" else "N/A"
    std = f"±{r['std_frame_time_ms']} ms" if r['std_frame_time_ms']  != "N/A" else "N/A"
    fps = str(r['fps'])
    print(f"{r['pipeline']:<12} {r['scenario']:<22} {ft:>16}  {std:>10}  {fps:>8}")
print("=" * 60)


# ============================================================
# CSV 저장
# ============================================================
os.makedirs(RESULTS_DIR, exist_ok=True)
with open(CSV_PATH, "w", newline="", encoding="utf-8") as f:
    writer = csv.DictWriter(f, fieldnames=[
        "pipeline", "scenario", "mean_frame_time_ms", "std_frame_time_ms", "fps"
    ])
    writer.writeheader()
    writer.writerows(results)
print(f"\nCSV 저장: {CSV_PATH}")


# ============================================================
# Bar chart 생성 (오차 막대 포함)
# ============================================================
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np

    scenarios = ["Aiming", "Pointing"]
    pipelines = ["Baseline", "IAPathTracer"]
    colors    = {"Baseline": "#4C72B0", "IAPathTracer": "#DD8452"}

    means = {}
    stds  = {}
    for r in results:
        key   = (r["pipeline"], r["scenario"])
        m_val = r["mean_frame_time_ms"]
        s_val = r["std_frame_time_ms"]
        means[key] = float(m_val) if m_val != "N/A" else 0.0
        stds[key]  = float(s_val) if s_val != "N/A" else 0.0

    x     = np.arange(len(scenarios))
    width = 0.35
    fig, ax = plt.subplots(figsize=(8, 5))

    for i, pipe in enumerate(pipelines):
        vals = [means.get((pipe, sc), 0.0) for sc in scenarios]
        errs = [stds.get((pipe, sc),  0.0) for sc in scenarios]
        bars = ax.bar(x + (i - 0.5) * width, vals, width,
                      label={"Baseline": "Baseline(PathTracer)", "IAPathTracer": "IAPathTracer"}[pipe],
                      color=colors[pipe], alpha=0.85,
                      yerr=errs, capsize=5, error_kw={"elinewidth": 1.5})
        for bar, v, e in zip(bars, vals, errs):
            if v > 0:
                ax.text(bar.get_x() + bar.get_width() / 2,
                        bar.get_height() + e + 0.3,
                        f"{v:.1f} ms", ha="center", va="bottom", fontsize=9)

    ax.set_xlabel("Scenario")
    ax.set_ylabel("Mean Frame Time (ms)")
    ax.set_title("Frame Time Comparison: Baseline(PathTracer) vs IAPathTracer\n"
                 "(error bars = ±1 std, 5 intervals × 500 frames per condition)")
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
