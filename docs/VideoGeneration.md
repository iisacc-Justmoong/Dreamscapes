# 비디오 생성

Home의 기존 `Video` 바로가기 또는 QuickGenerate의 `Image / Video` 선택에서 비디오를 생성한다. 동일한 프롬프트, 화면비, 개수, Generate 버튼과 작업 결과 화면을 사용한다. Video를 선택하면 로컬 LTX 모델 선택, 재생 시간(1/3/5/10초), FPS(12/24/30)가 추가로 나타난다. 기본값은 5초·24 FPS·30 샘플링 단계이다.

## 모델과 런타임

데스크톱의 설치된 `iild-generate --backend video`를 사용한다. macOS에서 이미지 생성은 기존 C++ 네이티브 엔진을 유지하며 비디오는 관리형 Python 워커를 사용한다. 유료 서비스나 자동 모델 다운로드는 없다. iOS/Android 바이너리는 로컬 비디오 워커를 제공하지 않으므로 명확한 플랫폼 안내로 제출을 거부한다.

Society의 Models 폴더에 `model_index.json`과 transformer, 시간축 VAE, T5 인코더, tokenizer, scheduler를 포함한 완전한 LTX Diffusers 디렉터리가 필요하다. 지원 선언은 `LTXPipeline`, `LTXConditionPipeline`, `LTXImageToVideoPipeline`이다. 일반 이미지 체크포인트를 비디오 모델로 오인하지 않으며 이미지 모델과 비디오 모델 선택을 별도로 유지한다. 패키지를 추가한 후 기존 Refresh models로 목록을 갱신한다. 구체적인 컴포넌트 호환성과 누락 파일 검증은 iiLocalDiffusion의 비디오 런타임이 수행한다.

관리형 Python 환경은 iiLocalDiffusion의 `reference/diffusers/requirements-video.txt`도 충족해야 한다. FFmpeg와 FFprobe는 구성 시 확인한 실행 파일 경로로 호출하며 GUI에서 실행했을 때의 PATH에 의존하지 않는다. 기존 SDK는 GPU 필수 auto 장치 선택을 적용하며 CPU로 조용히 폴백하지 않는다. T5 텍스트 인코딩은 CPU에서 수행하고 샘플링 전에 해제하여 GPU 메모리를 절약한다.

## 입력과 작업

텍스트만 제출하면 text-to-video로 실행한다. 기존 첨부 또는 그림 캔버스 입력이 하나 있으면 first-frame 조건으로 전달한다. 두 개 이상은 명확한 오류로 거부한다. 입력은 대기열 제출 전에 Society Asset Library의 GenerationInputs에 복사하여 원본 삭제나 다음 초안 변경으로 제출된 작업이 달라지지 않게 한다. 작업별 시드와 duration, FPS, frames를 기록하며 복수 요청은 기존 직렬 큐로 모두 생성한다.

| 화면비 | 최종 크기 |
| --- | --- |
| 1:1 | 512 × 512 |
| 4:3 | 640 × 480 |
| 3:4 | 480 × 640 |
| 16:9 | 1024 × 576 |
| 9:16 | 576 × 1024 |

모든 크기는 정확한 화면비를 유지하며 LTX의 32px 격자를 충족한다. 이미지 생성의 기존 크기는 변경하지 않는다. 24/30 FPS는 LTX 생성 뒤 SDK의 프레임 보간을 거치며 12 FPS는 직접 LTX 경로를 사용한다.

## 결과, 취소와 저장

완료 시 SDK 보고서의 complete 상태, verified_decode, H.264, 해상도, FPS, 프레임 수, 재생 시간, 파일 바이트 수와 SHA-256을 확인한다. 첫 출력 프레임도 실제 이미지로 읽을 수 있어야 한다. 모두 통과한 결과만 Society의 Generation History에 `<job>-0001.mp4`와 `<job>-poster.png`로 저장한다. 보고서와 작업 메타데이터는 기존 세션 메모리 정책을 유지하며 중간 프레임과 임시 보고서는 작업 종료 시 제거한다.

결과 화면은 Qt Multimedia를 사용하는 앱 플레이어로 Play/Pause/Replay와 Save video를 제공한다. 다수 결과는 기존 갤러리의 포스터로 표시하여 전부 선택할 수 있다. Home의 저장된 생성 기록에서도 포스터 카드를 비디오로 구분하고 대응 MP4를 결과 플레이어로 다시 연다. 비디오를 이미지 캔버스 입력이나 이미지 전용 Photos 저장으로 오인하지 않는다. Save video는 검증된 원본 MP4 바이트를 파일 선택기로 복사한다. 취소는 워커 프로세스 그룹과 codec 자식까지 중단한다.

SDK의 `IILD_VIDEO_PROGRESS` 이벤트를 로딩, 프롬프트 인코딩, 시간축 샘플링, 디코딩, 보간, MP4 인코딩 상태로 표시한다. 진행 중인 단계나 검증된 샘플링 단계 변화는 기존 무진행 감시 시간을 갱신하며 단순 heartbeat는 진행으로 취급하지 않는다.

## 검증

`DreamscapesGenerationTests videoQueuePreservesInputsAndPublishesAllResults videoRejectsBadOutputAndCancelsWorker`는 macOS 네이티브 이미지 설정과 비디오 워커의 공존, 복수 결과, 입력 복사, 모델 구분, 잘못된 보고서/크기/해시 거부와 취소를 확인한다. `Dreamscapes.Video`는 기존 Home 바로가기 → Video 설정 → 결과 화면 → 실제 MP4 플레이어 및 원본 내보내기를 확인한다. iiLocalDiffusion의 `VideoRuntimeTests`는 검증된 샘플링 진행 이벤트를 확인한다.

검증용 작은 무작위 LTX 모델은 실제 시간축 추론과 MP4 출력을 확인하는 실행 증거이며 학습된 모델의 품질을 증명하지 않는다. 사용자 Society에 학습된 호환 LTX 모델이 없으면 UI가 모델 추가를 안내한다.
