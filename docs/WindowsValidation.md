# Windows 실행 검증

Windows 빌드는 C++23, Qt 6.8.3 MinGW와 현재 iiSharedCanvas 0.28을 사용한다. 존재하지 않는 0.26 요구를 실제 SDK 버전으로 정정하였다. 정적 래스터/벡터 편집은 StaticBitmapLayer와 StaticVectorLayer를 사용하며 layerSource의 값 반환을 지역 변수에 보관하여 유효한 참조로 접근한다. 편집기 회귀 테스트와 실제 GUI 실행으로 확인한다.

FFmpeg는 https://ffmpeg.org/download.html 에 연결된 gyan.dev Windows 9.0.2 essentials를 사용한다. 배포 checksum과 SHA-256을 대조하였다. ffmpeg.exe와 ffprobe.exe 경로를 CMake와 실행 PATH 모두에 둔다.

HomeCanvas의 소멸자는 라이브러리에서 한 번 정의한다. MinGW LTO에서 GUI 테스트와 moc 가상 함수 테이블의 중복 약한 심볼이 제거되는 링크 오류를 방지하며 HomeCanvas 및 GUI 런타임 테스트로 확인한다.

리다이렉트·캐시 마이그레이션 회귀는 Windows .lnk 바로가기 대신 CreateSymbolicLinkW로 실제 NTFS 링크를 생성한다. 디렉터리 링크는 RemoveDirectoryW로 링크 자체만 제거한다.

Windows에서는 CMake 파일에 삽입하는 경로를 슬래시 형식으로 정규화하여 역슬래시의 CMake 이스케이프 해석을 방지한다. GUI 회귀 테스트는 실제 Windows QPA로 실행한다.

생성 통합 테스트는 네이티브 경로 구분자를 정규화하여 격리 작업 디렉터리를 검사한다. 여러 실제 Python worker를 실행하는 Generation 집계 테스트에는 Windows 초기 실행 시간을 반영한 300초 상한을 사용하며, 개별 작업 완료 제한은 유지한다.

이미지·영상 worker의 Windows 실행 가능 여부는 Python 스크립트와 실제 인터프리터를 함께 확인한다. 드라이브 문자로 시작하는 LoRA 및 참조 이미지 경로는 URL 스킴 판정 이전에 로컬 절대 경로로 해석한다. 구버전 작업 파일 정리는 NTFS 디렉터리 링크 자체를 RemoveDirectoryW로 제거하며 링크가 가리키는 Models를 순회하지 않는다. Generation의 영상 worker·LoRA·출력 경로·구버전 정리 회귀로 검증한다.

Windows에서 네이티브 checkpoint 엔진이 제공되면 기본 이미지 생성 경로로 사용한다. IILD_GENERATOR_EXECUTABLE로 지정한 사용자 worker는 우선하며, 모델 파일이 없는 환경에서는 실제 유료·다운로드 모델 추론을 성공으로 간주하지 않는다.
