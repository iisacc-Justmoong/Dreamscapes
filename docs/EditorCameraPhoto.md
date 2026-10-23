# Camera / Photo editor panel

The desktop dock and mobile tool sheet present the same LVRS panel. The source is the current Figma Camera, Capture and Gallery frames in `bn8O4AHKr1X9DWnhR1TgEy`, under `775:80914`: `823:87253` (432 × 844), `823:86627` (432 × 772), and `823:85995` (432 × 583). Content is 398 px wide at the 432 px dock size, with a 16 px inset plus the border, 12 px groups, 8 px control spacing, a 78 × 30 gray navigation control and 18 px icons. Narrow/mobile panels scale input widths and scroll vertically. Preview images, captured thumbnails and gallery originals are runtime data. The checkerboard preview background and black preview camera icon are original Figma assets; the other vectors are original 18 × 18 Figma component exports matching the installed LVRS library.

Camera settings use stable named keys rather than the earlier scan/RAW field numbers. Camera lists are live Qt Multimedia GUI capture devices. Preview starts without another OS prompt if permission has already been granted; clicking the preview or a capture action requests camera access otherwise. No device or denied permission produces a truthful empty state. Photo capture obtains full resolution `imageAvailable` frames, applies a centered aspect crop and quarter-turn rotation, and supports a 0–60 second timer and 1/3/5 images. Mirror applies to preview only. Grid and composition guides overlay live preview. Captures stay outside the document until Import. Undo removes imported content while retaining Camera/Photo acquisition settings and the current view as UI drafts. Silent video recording uses the native media recorder, displays elapsed recording duration and finalizes its output before import. Desktop video applies the selected aspect crop and rotation through the existing FFmpeg runtime; Photos saving is serialized for every burst image and completed video. Imported sources become ordinary image/video layers through the existing canvas transaction path. When the generation runtime has an active Society container, explicit imports use that container for native image wrappers. Test sources reside in an isolated real `.societycontainer`.

On macOS 14 or newer, Capture uses ScreenCaptureKit for display/window images, cursor inclusion, logical/native resolution and transparent window backgrounds. Region capture uses a cancellable selection overlay, removes that window before capture, and crops using the selected display's pixel scale. Permission, closed-window and invalid-region errors remain visible. Other desktop backends use their GUI screen capture path; native window/cursor/alpha parity must be verified on each platform. Screen capture is unavailable on mobile hosts without a platform screen capture provider.

Gallery connects to the installed `iiPhotoLibrary::PhotoLibrary` native adapter for the user's allowed Photos library (including local/iCloud originals), or a Pictures/chosen-folder catalog. Scans and original exports run on one serialized photo worker. Only visible native thumbnails are requested, keeping iCloud latency out of initial enumeration. Search matches names; sort orders are newest, oldest, name and pixel area. Up to 400 images are presented per source with an explicit limit message. Single selection, multiple selection, filtered Select all and Import operate on real records. Import exports native originals rather than thumbnail previews, retains their filenames, and normalizes EXIF orientation before creating pixels for the canvas. Apple ImageIO decodes original HEIC and other native image codecs at original resolution, with EXIF orientation, when a Qt image plugin cannot decode them. Missing permission or an empty directory never creates sample records. Request epochs discard obsolete scans, capture callbacks and imports. Closing a panel cancels delayed capture and finalizes active recording.

`Dreamscapes.CameraPhoto` tests crop/rotation pixels, native-provider preview versus original import, search/sort/selection rules, denied access, import staging, stale capture callbacks and timer cancellation. `Dreamscapes.EditorCameraPhoto` verifies all three views, geometry, LVRS selection tone, desktop/mobile bounds and folder-source import into the actual canvas. Fixture tests do not prove physical camera/video/Photos authorization or live native screen capture; those require installed-app runtime checks. Video layer import retains the existing SDK decoder limits of 512 frames and 256 MiB of decoded data; recordings are retained and can be saved to Photos even when a recording exceeds those canvas import limits.


The installed Society 0.16 read-only Container mirror remains owned by iiSocietyHelper. It is manually registered as an uncreatable QML type: Qt 6.8 generated foreign registration otherwise attempts to derive QQmlElement from the final SDK class. The GUI test confirms the type exists and refuses application-side construction. Generic panel regression checks reacquire presenters after visiting dedicated loaders and scroll their reset control into view before clicking.

Entering Gallery clears acquisition errors from other views. An undetermined Photos permission shows an explicit Allow Photos access action instead of starting enumeration or displaying an indefinite loading state. Repeated clicks cannot create overlapping authorization requests; callbacks from an earlier library selection do not reload the current source. Denied access continues to show the System Settings instruction. Backend tests cover the authorization wait and its transition to the actual catalog.
LVRS option controls expose named accessibility actions and support Tab focus with Return/Space activation. The four-size GUI test opens an actual option menu through keyboard input.
Choice buttons and timer presets keep the Default tone through hover, press and
selection. Their native checked state follows the accepted Camera/Photo draft,
and a selected value holds Default's pressed surface. Off is selected when a
timer is zero; changing a preset or shutter mode does not start acquisition.
The Figma static views and prototype use the same fixed-Kind/state-axis contract
documented in [EditorButtonStates.md](EditorButtonStates.md).
Shared numeric-contract checks load the dedicated Text, Camera/Photo, File and Graphic definition modules before evaluating the editor catalog, matching the QML module imports.

Installed-app validation exposed repeated disk-image discovery during Society presence checks. The macOS `iiSocietyContainer::DiskImage::mountedAt` path now rejects ordinary directories with `statfs` before invoking `hdiutil`. Actual image mount roots still use the original ownership checks. The SDK regression covers repeated package lookups and existing isolated disk-image rejection/data-preservation tests.

## 카메라 장치와 출력 저장소의 초기화 시점

`EditorCanvas`는 카메라·사진 획득 객체를 보유하지만, 객체 생성과 일반 상태 조회만으로 카메라 장치 감시나 출력 폴더 생성을 시작하지 않는다. 장치 목록·기본 장치·카메라 상태를 실제로 조회하거나 카메라를 사용하는 시점에 장치 감시를 한 번 연결한다. 이후 장치 변경 신호는 기존 목록 갱신 경로로 전달된다. 미리보기 연결 해제, 취소, 갤러리 검색·정렬·선택은 카메라 장치를 초기화하지 않는다. 장치 감시 생성 함수를 주입하는 생성자는 이 경계를 검사하기 위한 기존 획득 객체의 의존성 주입 지점이다.

출력 캐시는 설치된 `iiFileProvider::native::TemporaryDirectory`를 사용하며 C++ 표준 동기화로 실제 파일 출력 시점에 한 번 생성한다. 캡처 저장, 네이티브 썸네일, Photos 원본 내보내기, EXIF 방향 정규화 또는 네이티브 디코딩 결과를 저장할 때만 필요하다. 목록 조회와 변환이 필요 없는 로컬 이미지 가져오기는 캐시 폴더를 생성하지 않는다. 원본 획득과 후속 배치 작업은 캐시의 공유 수명을 전달하며 마지막 작업까지 파일을 유지한다. FIFO 작업은 앱 작업 풀의 값형 연속 작업으로 실행하고, 획득 객체를 닫을 때 전용 풀 종료를 기다리지 않는다. 실행 중인 SDK 연산과 프로세스 전체 종료 시간은 별도로 검증한다.

캐시 생성 실패는 캡처 또는 갤러리 상태의 오류로 전달된다. 실패한 경로로 내보내기·썸네일·영상 처리를 이어가지 않으며, 기존 파일은 보존한다. 썸네일 경로 준비에 실패한 항목은 다시 요청할 수 있다. 동일한 실패 상태는 다시 발행하지 않아 갤러리 관찰자의 재진입이나 타일 생성이 반복 오류 통지로 이어지지 않게 한다. 저장소 복구 후 재시도는 계속 가능하다. 취소된 캡처 콜백은 캐시를 생성하기 전에 폐기한다. 캡처·사진 원본은 계속 명시적인 Import 이후에만 캔버스 문서로 반영된다.

`Dreamscapes.CameraPhoto` 회귀 검사는 생성·수동 상태 조회·갤러리 조작 시 장치 감시 및 출력 폴더 생성이 없는지, 실제 카메라 조회 시 감시가 한 번 연결되고 변경 신호가 전달되는지 검사한다. 또한 출력 캐시의 지연 생성·정리, 한글 경로, 변환 없는 로컬 원본 가져오기, 저장소 오류 후 재시도와 취소된 콜백의 무출력을 검사한다. 실제 하드웨어 캡처 및 OS 권한 화면 검증과는 별도의 근거이다.

## 원본 가져오기와 문서 배치의 연결

명시적인 Import는 수락 시점의 문서·아트보드·저장소 경로를 캡처한다. 원본 준비가 끝난 뒤 현재 문서에 동기 가져오기를 실행하던 콜백은 제거한다. 이미지·영상 준비는 작업자에서 수행하고, 원래 대상이 유효할 때만 한 번의 문서 편집과 동기 저장으로 배치한다. `importing`은 원본 획득부터 배치 완료까지의 상태이며 같은 배치 진행 중 재클릭을 거부한다. 기존 `importReady`는 배치 후 관찰 알림이다. 상세 수명·취소·보기 상태·저장 계약과 검사는 [PhotoImportCommands.md](PhotoImportCommands.md)를 따른다.

## 목록 새로고침과 불필요한 대기 작업

각 갤러리 목록의 취소 플래그는 목록과 네이티브 썸네일에만 연결한다. 새 목록을 요청한 뒤 아직 시작되지 않은 이전 보기 작업은 SDK 호출을 생략한다. 원본 가져오기·Photos 저장과 유효한 현재 썸네일의 직렬 순서는 유지한다. 진행 중 SDK 호출의 즉시 취소, 일반 패널의 공유 캡처 취소와 갤러리 getter의 재정렬 비용은 이 변경과 별도의 경계이다. 상세 실행 순서 검사는 [PhotoImportCommands.md](PhotoImportCommands.md)를 따른다.

## 갤러리의 행 모델과 조회 캐시

실제 Gallery GridView는 기존 가상화를 유지하고, 일정한 수명을 갖는 `galleryModel`을 표시한다. `EditorPhotoGalleryModel`은 Qt GUI 모델 어댑터이며 원본 획득·Photos·캔버스 파일 작업을 호출하지 않는다. 카탈로그와 표시 순서를 소유하고, 새 카탈로그 또는 검색·정렬 변경 때만 표시 순서를 계산한다. 기존 `gallery()`는 같은 키와 순서의 캐시된 값 목록을 계속 반환한다. 선택 개수도 이미 계산된 값을 조회한다.

타일은 모델의 `entry` 역할을 읽는다. 썸네일·선택 상태는 변경 행에만 `dataChanged`를 발행하므로 타일 객체와 스크롤 위치를 유지한다. 여러 선택 행은 상태를 먼저 일괄 반영한 다음 한 범위로 알린다. 동일한 검색·정렬 요청은 모델이나 기존 목록 알림을 다시 발행하지 않는다. Select all의 표시 행 여부는 인덱스 표로 처리하여 행마다 표시 목록을 다시 검색하지 않는다. 카탈로그 원본과 정렬·검색 키의 변경은 카탈로그 교체·필터 API를 사용하며, 행 갱신은 현재 카탈로그의 표시 상태만 바꾼다.

새 모델 알림을 받을 때 필터 변경·목록 교체·소유자 삭제가 가능하므로 획득 객체의 생존성과 목록 세대를 확인한다. 카탈로그의 처리 상태는 모델을 게시하기 전에 반영하여, 게시 중 발생한 썸네일 준비 오류를 뒤에서 덮지 않는다. 마스크·도구·문서·파일 리비전은 이 보기 모델의 상태에 포함하지 않는다.

## Photos 권한 요청과 현재 보기의 연속 작업

권한 요청은 목록 조회 세대와 독립적인 요청 번호를 갖는다. 요청 소유자·제공자·번호를 상태 알림 전에 캡처하고, 알림 후에도 현재 Photos 읽기 의도가 유효한지 확인한다. 그 사이 라이브러리가 바뀌거나 보기가 종료되면 SDK 권한 호출을 시작하지 않는다. 상태가 이미 승인·거부로 바뀌었을 때도 불필요한 권한 호출을 생략한다. SDK 권한 호출은 GUI 스레드에서 수행하며, 완료는 항상 소유 스레드의 큐로 전달한다.

완료는 같은 요청이 아직 진행 중일 때 한 번만 수락한다. 현재도 Photos 읽기가 필요하면 그 시점의 목록을 조회한다. 승인 대기 중 다른 라이브러리를 다녀왔더라도 최신 Photos 보기를 이어 읽으며, 현재 로컬 목록은 교체하지 않는다. 거부·미지원 상태는 각각 설정 안내·기기에서 사용할 수 없다는 상태로 전달한다. SDK 예외는 갤러리 상태에 표시하여 재시도할 수 있게 하고, 늦은 완료가 새로운 재시도를 종료하지 못하도록 요청 번호를 확인한다.

Gallery 패널은 자신을 `attachGallery`로 연결하고 숨김·다른 상태 뷰·파괴 시 `detachGallery`로 읽기 의도를 해제한다. 현재 연결한 객체만 해제할 수 있으므로 이전 패널의 종료가 새 패널을 취소하지 않는다. 대기 중 목록·썸네일만 취소하며, 수락된 원본·문서 배치·Photos 저장은 기존 수명을 유지한다. 연결 해제 시 미완료 썸네일의 요청 표시를 되돌리고, 재연결에는 새 취소 수명을 사용하여 실제 파일이 없는 URL을 완료로 발행하지 않는다. 제공자 종료 감시는 현재 연결 하나만 유지한다.
