# 파일 생성 경계

실패 진단 보고서의 부모 경로, 게시 프레임 내보내기 경로와 생성 결과 일괄 내보내기
경로는 `iiFileProvider::native::createDirectory/createDirectories`로 생성한다.
실패 보고서 본문은 기존 `iiFileProvider::core::AtomicFile`로 원자적으로 게시한다.

단일 내보내기 디렉터리의 배타적 생성과 이름 충돌 오류, 부모가 파일인 경로의 실패,
UTF-16 네이티브 경로 처리를 유지한다. 보고서 저장 검사는 없는 다단계 부모와 한글
경로의 생성 및 일반 파일을 부모로 지정한 경우의 오류 보존을 확인한다.
Workspace 전체 생성 경계 검사도 SDK와 Product를 함께 검사한다.
