# 창 배경과 재질

Dreamscapes의 메인 창과 설정 창은 [공통 ApplicationWindow](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc?node-id=997-3)의
`Panel/window`를 창 바탕으로 사용한다. 창의 악센트가 바뀌어도 배경색은 바뀌지
않으며, 버튼·선택·포커스의 악센트는 기존처럼 유지한다. 레거시 LVRS 창의 배경색
입력만 앱에서 공통 토큰에 연결하며 LVRS 모듈 자체는 변경하지 않는다.

공통 창 재질의 Dense 75와 Glass 25는 각각 75%·25% 채색과 64·16 논리 px
블러를 유지한다. 현재 Dreamscapes의 레거시 LVRS 창은 불투명 경로이며, 이 재질
계약은 해당 창에서 운영체제 배경 블러가 작동한다는 뜻이 아니다. LVUI로 작성된
독립 결과 창은 공통 `Panel/window` 기본값을 사용한다.

메인 창 회귀 검사는 악센트를 바꾸어도 창 바탕이 `Panel/window`로 유지되는지와
컨트롤 악센트가 계속 바뀌는지를 별도로 확인한다. 테스트에서 운영체제 창을 실제로
표시한 결과와 창 없는 계약 검사는 구분한다.
