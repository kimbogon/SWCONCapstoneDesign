# ============================================================
# camera_aiming.py  —  Aiming 시나리오 카메라 keyframe
#
# [사용 방법]
#   FPS.pyscene의 "# 8. 카메라 자동 궤적" 섹션에 아래 코드를 붙여넣기.
#   camera 변수는 섹션 5에서 이미 정의되어 있어야 한다.
#
# [시나리오]
#   0 ~ 4초 : 카메라 (0,1.7,-9) → (0,1.7,-1) 전진하며 적 추적
#   4 ~10초 : 카메라 (0,1.7,-1) 고정, 적 지속 추적
#
# [적 오브젝트 이동 궤적 (FPS.pyscene 기준)]
#   x: 0 → 2 → 0 → -2 → 0, 주기 8초, Linear Cycle, y=1.7, z=2.0
#   enemy_x(t), t' = t mod 8:
#     t' ∈ [0, 2] → x =  t'
#     t' ∈ [2, 4] → x =  4 - t'
#     t' ∈ [4, 6] → x = -(t' - 4)
#     t' ∈ [6, 8] → x =  t' - 8
#
# [keyframe 설계 원칙]
#   - interpolationMode = Linear 사용
#   - 방향전환점(t=2, 4, 6, 8)과 카메라 정지점(t=4)에 keyframe 배치
#   - Linear 보간이므로 target이 선형 구간에서 추적 오차 없음
#   - postInfinity = Constant (10초 시나리오가 캡처 윈도우를 충분히 커버)
# ============================================================

_up = float3(0, 1, 0)

_cam_anim = sceneBuilder.createAnimation(camera, 'aiming_trajectory', 10.0)
_cam_anim.postInfinityBehavior = Animation.Behavior.Constant
_cam_anim.interpolationMode = Animation.InterpolationMode.Linear

# ---------- 0~4초: 전진 구간 (카메라 z: -9 → -1) ----------
# t=0: enemy_x= 0.0, cam_z=-9
_cam_anim.addKeyframe(0.0, Transform(
    position=float3(0, 1.7, -9),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# t=1: enemy_x= 1.0, cam_z=-7
_cam_anim.addKeyframe(1.0, Transform(
    position=float3(0, 1.7, -7),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# t=2: enemy_x= 2.0, cam_z=-5  ← 적 방향전환 (우→중앙)
_cam_anim.addKeyframe(2.0, Transform(
    position=float3(0, 1.7, -5),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# t=3: enemy_x= 1.0, cam_z=-3
_cam_anim.addKeyframe(3.0, Transform(
    position=float3(0, 1.7, -3),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# t=4: enemy_x= 0.0, cam_z=-1  ← 카메라 전진 완료 / 적 방향전환 (중앙→좌)
_cam_anim.addKeyframe(4.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# ---------- 4~10초: 고정 구간 (카메라 (0,1.7,-1) 유지) ----------
# t=5: enemy_x=-1.0
_cam_anim.addKeyframe(5.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3(-1.0, 1.7, 2.0),
    up=_up))

# t=6: enemy_x=-2.0  ← 적 방향전환 (좌→중앙)
_cam_anim.addKeyframe(6.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3(-2.0, 1.7, 2.0),
    up=_up))

# t=7: enemy_x=-1.0
_cam_anim.addKeyframe(7.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3(-1.0, 1.7, 2.0),
    up=_up))

# t=8: enemy_x= 0.0  ← 적 8초 주기 재시작 (중앙→우)
_cam_anim.addKeyframe(8.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3( 0.0, 1.7, 2.0),
    up=_up))

# t=9: enemy_x= 1.0
_cam_anim.addKeyframe(9.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3( 1.0, 1.7, 2.0),
    up=_up))

# t=10: enemy_x= 2.0  ← 적 방향전환 (우→중앙), 1주기 종료
_cam_anim.addKeyframe(10.0, Transform(
    position=float3(0, 1.7, -1),
    target=float3( 2.0, 1.7, 2.0),
    up=_up))

sceneBuilder.addAnimation(_cam_anim)
