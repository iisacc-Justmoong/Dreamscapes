# Native generation view models

Dreamscapes' native views bind observable, owner-thread C++ presentation state
to LVUI and forward user edits to the model. This follows the same boundary as
Congregation: views do not own inference or document state, and a missing
service is never reported as a successful operation.

The Home composer shares a session-local `ComposerState` with its LVUI controls.
Prompt, negative prompt, media, size, count, duration, frame rate and seed
changes must update the displayed draft without writing a document or starting
generation. The generation button is unavailable until a real generation
handler is supplied. The Advanced command opens the corresponding native
Image or Video view while retaining the draft. On that command, the applicable
Home dimensions, output count, seed and Video duration and frame rate are
copied into the matching advanced draft. Reopening Advanced without changing
those Home settings preserves edits made in the advanced draft. Later changes
to an individual Home setting replace only the corresponding advanced value.

The compact Home generation controls keep the attachment and Generate actions
at fixed widths. Model, aspect ratio, resolution, output count and seed use
their current label's measured LVUI dropdown width. The controls wrap when
needed, with an oversized label constrained to the available width. Paint,
negative prompt, presets, advanced options and video parameters remain
available in the following controls rather than being removed.

The Image and Video screens use native parameter models rather than the static
migration text. Image defaults, types, choices and validation come from
`iiLocalDiffusion::imageParameterSpecs` and `ImageParameters`; Video uses
Dreamscapes' existing `VideoDraft` edit and validation rules. A rejected
numeric edit retains the user's input and reports the validation state without
overwriting the accepted parameter. Model selection and generation submission
remain unavailable when no provider is connected. View state is separate from
files and is not persisted by merely changing a control. Reference image,
ControlNet and LoRA collections are not yet exposed in these native views.

## 이미지 크기와 HiRes 기본값

Home 이미지의 해상도 목록은 선택한 종횡비에 따라 `가로 × 세로`를 표시하며,
표시된 크기가 생성 요청에 전달된다. 기본 짧은 변은 1536픽셀이다.
512·576·768·1024·1280·1536·1792·2048픽셀 프리셋은 짧은 변을 기준으로
반대쪽 변을 16픽셀 격자에 맞춘다. 3:4에서 짧은 변이 1024픽셀이면
`1024 × 1360`, 16:9에서 짧은 변이 2048픽셀이면 `3648 × 2048`이다.
4K 프리셋은 긴 변을 4096픽셀로 맞춘다. 1:1은 `4096 × 4096`, 4:3은
`4096 × 3072`, 3:4는 `3072 × 4096`, 16:9는 `4096 × 2304`, 9:16은
`2304 × 4096`이다. 고급 이미지 초안을 처음 열면 Home의 크기를 사용하며,
각 축을 4096픽셀까지 직접 입력할 수 있다. 비디오 크기는 종횡비에 따라 별도로 정해진다.

정지 이미지의 HiRes Fix는 기본으로 켜진다. 요청 크기는 최종 크기이며,
지원되는 iiLocalDiffusion 경로는 작은 기본 이미지와 보정 단계를 수행한다.
고급 설정에서 명시적으로 끄면 `--no-hires-fix` 또는 네이티브 `hires=false`가
전달된다. 데스크톱 이미지 워커는 선택한 보간 업스케일러와 보정 강도도 전달한다.
저장된 설정에서 `hiresFix`가 없거나 `null`이면 기본 활성 상태를 사용하며,
JSON 불리언 `false`만 비활성화한다. 문자열·숫자는 설정 입력 시 거부한다.
간단 생성 요청에 저장된 `hiresFix`도 선택 값으로 받아 같은 규칙을 적용한다.
이전 작업 기록을 읽는 경로에서도 누락되거나 불리언이 아닌 값을 false로
변환하지 않는다.
모델·장치의 지원과 메모리에 따라 실제 생성이 실패할 수 있고, MOSS 등 별도의
범용 Diffusers 경로는 그 경로가 제공하지 않는 HiRes를 지원한다고 간주하지 않는다.
합성 계약 테스트는 크기·옵션 전달을 검사하며 사전 학습 모델의 출력 품질이나
큰 이미지의 실제 메모리 사용량을 입증하지 않는다.

The result route uses the existing native review `Session` and gallery. Review
selection and shortlist changes are session-local and survive navigation. The
history provider is not connected; the screen says so and starts without
fabricated results. Edit, export and preview remain unavailable without their
respective handlers and providers.

The host passes its actual content size to the native root on first mount and
on resize, so changing pages cannot collapse the interactive viewport. The
parameter list uses LVUI's measured, virtualized collection rather than nesting
an oversized retained list inside another scroll view.

The additional editor, storage, publishing and preference model seams and
their explicit integration limits are described in
[Native editor and workspace view models](NativeDynamicWorkspaces.md). Other
migrated views remain static, and the product service connections are not
established merely by those models. Tests use in-memory models and windowless
iiGUI widget trees; they do not run generation or launch the application.

## Model catalogue

The native Society catalogue recognizes safetensors and GGUF files from a
bounded content probe when their filename uses another extension. Existing
recognized extensions remain visible, while ONNX and unrelated files remain
excluded. GGUF architecture metadata and tensor signatures distinguish known
video models from image models; Society modality overrides take precedence.
Known Wan, Hunyuan Video, CogVideoX, Mochi and Stable Video Diffusion pipeline
identities are listed as video models. Classification is independent of the
worker's supported pipeline and required components.

Standalone safetensors shard indexes, including mixed-case index suffixes, are
presented as a single model, with their indexed shard files captured in its Society source identity. Only a
complete validated group is collapsed; a changed, missing or replaced shard
invalidates the selected source before generation. The underlying weights are
not rewritten by catalogue discovery. Classification identifies the intended
modality; the generation backend still checks architecture and required
components. An indexed native video checkpoint without embedded LLM tensors
requires a compatible text encoder.

For legacy checkpoints with no known classification, the existing image
fallback remains. Discovery and classification do not establish that an
inference backend supports every architecture or quantization.

The native catalogue test compiles the production storage and inspection
sources without linking the UI. It exercises renamed files, content types,
explicit overrides, exclusion rules and model identity revalidation with
synthetic fixtures; it does not run inference. The Society generation publication test also accepts generic Diffusers image records without inventing per-image seeds.

Video 생성은 Society에서 선택한 패키지의 경로와 캡처된 파일 식별을 워커에
전달한다. 최신 iiLocalDiffusion 런타임은 인식된 Wan 패키지의 `video_vae`가
하나의 구성 요소로 확인되면 이를 검증·복원하여 네이티브 엔진에 연결한다.
주 모델이 GGUF인 패키지도 내장 GGUF의 텐서 구조를 검사해 Video 목록에 표시하며,
워커가 주 모델을 검증 복원해 네이티브 엔진에 전달한다.
기본 Diffusers Wan의 완전한 텐서 서명이 있는 독립 체크포인트도 최신 모델 검사
SDK에서 video로 분류하며, 같은 검사로 선택한 원본 경로를 확인한다.
별도 외부 파일의 자동 탐색이나 다중 전문가 선택은 제공하지 않는다. 네이티브
Society 비디오 테스트는 합성 Wan 패키지가 Video 모델로 표시되고 선택한 경로가
워커에 전달되는지 검사한다. 이 테스트는 실제 모델 추론을 수행하지 않는다.

### Video image conditions

The advanced Video view exposes the first and last image conditions shown in
the existing Figma design. A user may choose, replace, or clear each condition;
the fields belong to the video draft and do not change the canvas or source
file. A canceled chooser leaves the prior selection unchanged. The queued
request retains the selected paths and the preparation worker snapshots each
input before invoking the backend. The backend receives `--first-frame` and
`--last-frame` only for supplied images. Missing, redirected, empty, oversized,
or changing inputs fail the request instead of silently producing an
unconditioned video. Older requests without either optional field retain their
existing behavior. Each backend still validates whether its specific model
supports the supplied conditions. These guarantees are tested with synthetic
fixtures and do not establish successful inference for any real model.

### Renamed standalone shard indexes

The model catalogue also groups a standalone Safetensors index when the index
or shard filenames use other extensions. The shared SDK verifies the index
structure and shard headers before the group is offered. A selection captures
the index and each shard, so replacing a member invalidates the selection.
This is a source-identity and routing check, not an inference result.

### PyTorch 체크포인트 목록

Society의 단일 PyTorch 체크포인트 후보는 확장자와 무관하게 모델 목록에 표시한다.
목록에 보이는 것은 후보 발견을 뜻하며 실제 실행 호환성을 보증하지 않는다.
확인되지 않은 유형은 기존 이미지 후보 규칙과 사용자 분류를 따르고, 선택할 때
원본 식별자를 캡처하며 변경된 파일은 사용하지 않는다. 이미지 작업은 기존의
안전한 weights-only 검사 및 변환 경로를 통과해야 한다. 일반 ZIP·pickle과 ONNX는 모델로 확인한 것이 아니다.

### PyTorch 분할 체크포인트

완전한 PyTorch 분할 인덱스는 개별 조각 대신 모델 하나로 표시한다. 선택은 인덱스와
모든 샤드의 식별자를 포함하며 샤드가 누락되거나 바뀌면 작업 준비를 거부한다.
이미지·비디오 워커는 원본 경로를 받아 weights-only 검사와 순차 변환을 수행한다.
이 검사와 변환은 실제 아키텍처 실행 가능성이나 생성 품질의 증거가 아니다.
