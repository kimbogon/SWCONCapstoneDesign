#!/usr/bin/env python3
"""
ROI PSNR/MSE 측정 스크립트 (요구사항 1.3 - 4~5단계)

사용 전 확인사항:
  - PathTracerAdaptive.py에 ImportancePass.importance markOutput 추가 후 재캡처
  - REF_DIR / BASELINE_DIR / ADAPTIVE_DIR / IMPORTANCE_DIR 경로 설정

파일명 형식 (Falcor 기본 캡처 명명 규칙):
  - reference:  {REF_DIR}/ref.OverlayPass.output.{NNNN}.exr
  - baseline:   {BASELINE_DIR}/baseline.OverlayPass.output.{NNNN}.exr
  - adaptive:   {ADAPTIVE_DIR}/test.OverlayPass.output.{NNNN}.exr
  - importance: {IMPORTANCE_DIR}/test.ImportancePass.importance.{NNNN}.exr
  실제 파일명이 다르면 아래 *_GLOB 패턴 수정
"""

import os
import re
import glob
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import OpenEXR
import Imath

# ──────────────────────────────────────────────
# 설정
# ──────────────────────────────────────────────
REF_DIR        = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis/reference"
BASELINE_DIR   = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis/baseline"
ADAPTIVE_DIR   = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis/test"
IMPORTANCE_DIR = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis/test"
OUTPUT_DIR     = "C:/Users/bg001/Desktop/Falcor/Results/ROI_PSNR_analysis"

# Falcor가 생성하는 실제 파일명 패턴 — NNNN은 4자리 프레임 번호
# 캡처 후 Results 폴더 확인하여 맞게 수정
REF_GLOB        = "ref.OverlayPass.output.*.exr"            # 레퍼런스
BASELINE_GLOB   = "base.OverlayPass.output.*.exr"           # 베이스라인
ADAPTIVE_GLOB   = "test.OverlayPass.output.*.exr"           # 어댑티브
IMPORTANCE_GLOB = "test.ImportancePass.importance.*.exr"    # raw importance

# ROI 임계값 (0~1). importance >= THRESHOLD 픽셀이 ROI
THRESHOLD = 0.3

# PSNR 계산 시 peak 값. None이면 프레임별 레퍼런스 최댓값 사용, float이면 고정
PEAK_VALUE = None

# 마스크 시각화 샘플 프레임 인덱스 (None이면 중간 프레임 자동 선택)
VIS_FRAME_IDX = None


# ──────────────────────────────────────────────
# EXR 로드
# ──────────────────────────────────────────────
def _read_channel(f: OpenEXR.InputFile, name: str, H: int, W: int) -> np.ndarray:
    pt  = Imath.PixelType(Imath.PixelType.FLOAT)
    raw = f.channel(name, pt)
    return np.frombuffer(raw, dtype=np.float32).reshape(H, W)


def load_exr_rgb(path: str) -> np.ndarray:
    """EXR → HxWx3 float32 (RGB)"""
    f  = OpenEXR.InputFile(path)
    dw = f.header()['dataWindow']
    W  = dw.max.x - dw.min.x + 1
    H  = dw.max.y - dw.min.y + 1
    chs = f.header()['channels'].keys()
    r = _read_channel(f, 'R', H, W) if 'R' in chs else np.zeros((H, W), np.float32)
    g = _read_channel(f, 'G', H, W) if 'G' in chs else np.zeros((H, W), np.float32)
    b = _read_channel(f, 'B', H, W) if 'B' in chs else np.zeros((H, W), np.float32)
    return np.stack([r, g, b], axis=-1)


def load_exr_r32(path: str) -> np.ndarray:
    """EXR R32Float 단채널 → HxW float32"""
    f  = OpenEXR.InputFile(path)
    dw = f.header()['dataWindow']
    W  = dw.max.x - dw.min.x + 1
    H  = dw.max.y - dw.min.y + 1
    return _read_channel(f, 'R', H, W)


# ──────────────────────────────────────────────
# 지표 계산
# ──────────────────────────────────────────────
def compute_psnr_mse(
    test: np.ndarray,           # HxWx3
    ref:  np.ndarray,           # HxWx3
    mask2d: np.ndarray | None,  # HxW bool, None이면 전체
    peak: float | None,
) -> tuple[float, float]:
    """
    mask2d가 있으면 해당 픽셀(×3 채널)에서만 MSE/PSNR 계산.
    반환: (psnr_dB, mse)
    """
    if mask2d is not None:
        t = test[mask2d]   # (N, 3)
        r = ref[mask2d]    # (N, 3)
    else:
        t = test.reshape(-1, 3)
        r = ref.reshape(-1, 3)

    mse = float(np.mean((t - r) ** 2))
    if mse < 1e-12:
        return float('inf'), mse

    p = peak if peak is not None else float(ref.max())
    if p < 1e-12:
        p = 1.0
    psnr = 10.0 * np.log10(p ** 2 / mse)
    return psnr, mse


# ──────────────────────────────────────────────
# 파일 목록 — 공통 프레임 번호 수집
# ──────────────────────────────────────────────
def _frame_num(path: str) -> int | None:
    # 4자리 패딩 없는 프레임 번호도 인식 (e.g., .0.exr, .10.exr, .590.exr)
    m = re.search(r'\.(\d+)\.exr$', os.path.basename(path))
    return int(m.group(1)) if m else None


def collect_common_frames() -> list[int]:
    """네 디렉터리에서 모두 존재하는 프레임 번호 목록 반환"""
    def frame_set(d, pattern):
        files = glob.glob(os.path.join(d, pattern))
        return {_frame_num(f) for f in files if _frame_num(f) is not None}

    frames = (
        frame_set(REF_DIR,        REF_GLOB)
        & frame_set(BASELINE_DIR, BASELINE_GLOB)
        & frame_set(ADAPTIVE_DIR, ADAPTIVE_GLOB)
        & frame_set(IMPORTANCE_DIR, IMPORTANCE_GLOB)
    )
    return sorted(frames)


def frame_path(directory: str, glob_pattern: str, frame: int) -> str:
    """glob_pattern의 * 위치에 4자리 프레임 번호가 들어간 경로 반환"""
    # glob으로 찾은 목록에서 일치하는 것 반환
    for p in glob.glob(os.path.join(directory, glob_pattern)):
        if _frame_num(p) == frame:
            return p
    raise FileNotFoundError(f"frame {frame:04d}: {glob_pattern} in {directory}")


# ──────────────────────────────────────────────
# 마스크 시각화 (샘플 프레임 1장)
# ──────────────────────────────────────────────
def save_mask_vis(ref: np.ndarray, mask2d: np.ndarray, frame: int):
    """레퍼런스 이미지 위에 ROI 마스크를 오버레이하여 저장"""
    # HDR → sRGB 근사 (감마 2.2)
    vis = np.clip(ref ** (1 / 2.2), 0, 1)

    # ROI 아닌 영역을 어둡게 처리
    overlay = vis.copy()
    overlay[~mask2d] *= 0.35

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    save_path = os.path.join(OUTPUT_DIR, f"mask_overlay_frame{frame:04d}.png")

    fig, axes = plt.subplots(1, 3, figsize=(15, 5))
    axes[0].imshow(vis,                  vmin=0, vmax=1); axes[0].set_title('Reference')
    axes[1].imshow(mask2d, cmap='hot');                   axes[1].set_title(f'ROI Mask (thr={THRESHOLD})')
    axes[2].imshow(overlay,              vmin=0, vmax=1); axes[2].set_title('Overlay')
    for ax in axes:
        ax.axis('off')
    plt.tight_layout()
    plt.savefig(save_path, dpi=150)
    plt.close()
    print(f"[저장] {save_path}")


# ──────────────────────────────────────────────
# 메인
# ──────────────────────────────────────────────
def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    frames = collect_common_frames()
    if not frames:
        print("[ERROR] 공통 프레임이 없습니다. 경로 및 파일명 패턴을 확인하세요.")
        return
    print(f"[INFO] 공통 프레임 수: {len(frames)}  ({frames[0]:04d} ~ {frames[-1]:04d})")

    records = []
    vis_frame = frames[VIS_FRAME_IDX if VIS_FRAME_IDX is not None else len(frames) // 2]
    vis_saved = False

    for fr in frames:
        # 파일 로드
        ref  = load_exr_rgb(frame_path(REF_DIR,        REF_GLOB,        fr))
        base = load_exr_rgb(frame_path(BASELINE_DIR,   BASELINE_GLOB,   fr))
        adap = load_exr_rgb(frame_path(ADAPTIVE_DIR,   ADAPTIVE_GLOB,   fr))
        imp  = load_exr_r32(frame_path(IMPORTANCE_DIR, IMPORTANCE_GLOB, fr))

        # ROI 마스크
        mask2d   = imp >= THRESHOLD         # HxW bool
        roi_px   = int(mask2d.sum())
        total_px = mask2d.size

        # PSNR/MSE 계산
        peak = PEAK_VALUE if PEAK_VALUE is not None else float(ref.max())

        full_base_psnr, full_base_mse = compute_psnr_mse(base, ref, None,   peak)
        full_adap_psnr, full_adap_mse = compute_psnr_mse(adap, ref, None,   peak)
        roi_base_psnr,  roi_base_mse  = compute_psnr_mse(base, ref, mask2d, peak)
        roi_adap_psnr,  roi_adap_mse  = compute_psnr_mse(adap, ref, mask2d, peak)

        records.append({
            'frame':          fr,
            'roi_px':         roi_px,
            'total_px':       total_px,
            'roi_ratio_pct':  roi_px / total_px * 100.0,
            'full_base_psnr': full_base_psnr,
            'full_adap_psnr': full_adap_psnr,
            'full_base_mse':  full_base_mse,
            'full_adap_mse':  full_adap_mse,
            'roi_base_psnr':  roi_base_psnr,
            'roi_adap_psnr':  roi_adap_psnr,
            'roi_base_mse':   roi_base_mse,
            'roi_adap_mse':   roi_adap_mse,
        })

        # 샘플 프레임 마스크 시각화
        if fr == vis_frame and not vis_saved:
            save_mask_vis(ref, mask2d, fr)
            vis_saved = True

    # CSV 저장
    df = pd.DataFrame(records)
    csv_path = os.path.join(OUTPUT_DIR, f"roi_psnr_thr{THRESHOLD:.2f}.csv")
    df.to_csv(csv_path, index=False)
    print(f"[저장] {csv_path}")

    # ── 시각화 ──────────────────────────────────
    fig, axes = plt.subplots(2, 2, figsize=(14, 8))
    fig.suptitle(f"ROI vs Full-Frame PSNR/MSE  (threshold={THRESHOLD})", fontsize=13)
    x = df['frame']

    # ROI PSNR
    ax = axes[0, 0]
    ax.plot(x, df['roi_base_psnr'], label='Baseline', color='steelblue', lw=1.5)
    ax.plot(x, df['roi_adap_psnr'], label='Adaptive',  color='tomato',   lw=1.5)
    ax.set_title('ROI PSNR (dB)');       ax.set_xlabel('Frame'); ax.set_ylabel('dB')
    ax.legend(); ax.grid(alpha=0.3)

    # Full-Frame PSNR
    ax = axes[0, 1]
    ax.plot(x, df['full_base_psnr'], label='Baseline', color='steelblue', lw=1.5, ls='--')
    ax.plot(x, df['full_adap_psnr'], label='Adaptive',  color='tomato',   lw=1.5, ls='--')
    ax.set_title('Full-Frame PSNR (dB)'); ax.set_xlabel('Frame'); ax.set_ylabel('dB')
    ax.legend(); ax.grid(alpha=0.3)

    # ROI MSE
    ax = axes[1, 0]
    ax.plot(x, df['roi_base_mse'], label='Baseline', color='steelblue', lw=1.5)
    ax.plot(x, df['roi_adap_mse'], label='Adaptive',  color='tomato',   lw=1.5)
    ax.set_title('ROI MSE');             ax.set_xlabel('Frame'); ax.set_ylabel('MSE')
    ax.legend(); ax.grid(alpha=0.3)

    # ROI size
    ax = axes[1, 1]
    ax.plot(x, df['roi_ratio_pct'], color='gray', lw=1.5)
    ax.set_title('ROI Size (% of Total Pixels)'); ax.set_xlabel('Frame'); ax.set_ylabel('%')
    ax.grid(alpha=0.3)

    plt.tight_layout()
    fig_path = os.path.join(OUTPUT_DIR, f"roi_psnr_thr{THRESHOLD:.2f}.png")
    plt.savefig(fig_path, dpi=150)
    plt.close()
    print(f"[저장] {fig_path}")

    # ── 요약 통계 출력 ───────────────────────────
    print("\n=== 평균 지표 ===")
    cols = [
        ('ROI  PSNR  Baseline', 'roi_base_psnr'),
        ('ROI  PSNR  Adaptive', 'roi_adap_psnr'),
        ('Full PSNR  Baseline', 'full_base_psnr'),
        ('Full PSNR  Adaptive', 'full_adap_psnr'),
        ('ROI  MSE   Baseline', 'roi_base_mse'),
        ('ROI  MSE   Adaptive', 'roi_adap_mse'),
    ]
    for label, col in cols:
        val = df[col].replace([float('inf')], np.nan).mean()
        print(f"  {label}: {val:.4f}")
    print(f"  ROI 비율 평균    : {df['roi_ratio_pct'].mean():.1f}%")


if __name__ == "__main__":
    main()
