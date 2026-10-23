삼국지 IV with 파워업키트 (한글 Win95판) 실행 패치 — Windows 10/11용

설치 프로그램 없이 게임 폴더에 파일만 넣으면 실행됩니다. 레지스트리 설정이나 실제 CD·가상
드라이브는 필요 없습니다 (CD 이미지 파일만 있으면 됩니다). 게임 파일은 이 패치에 들어 있지 않습니다.

■ 설치
1. 게임 폴더(SAN4W95.EXE가 있는 폴더)를 쓰기 가능한 곳에 둡니다 (예: C:\Games 아래).
   "C:\Program Files" 아래는 저장이 안 되므로 피해 주세요.
2. 이 패치의 파일을 모두 게임 폴더에 복사합니다.
3. 게임 폴더 안에 CD 폴더를 만들고 게임 CD 이미지(Audio.cue, Audio.bin)를 넣습니다.
   게임이 시작할 때 CD를 검사하므로 꼭 필요합니다. Audio.bin 대신 MP3로 줄이는 방법은
   https://github.com/zirize/rotk4-patch 를 보세요.
4. SAN4W95.EXE를 실행합니다.

■ 화면 크기
오프닝이 끝나면 게임이 "640×480 / 800×600" 중 화면 크기를 묻습니다. 800×600을 고르면
지도 등이 더 넓게 보입니다 (실제 크기는 800×592). 창 크기와는 별개입니다.

■ 음악
CD 음악은 CD 폴더의 이미지(또는 MP3)로 재생됩니다. 게임 안 배경음악은 게임의 옵션에서
BGM을 켜야 나옵니다 (타이틀 음악은 항상 나옴).

■ 창 크기와 위치
- 창 크기는 메뉴의 "화면" → x1 / x2 / x3 으로 고릅니다 (게임 화면을 정수배로 확대해서
  픽셀이 고르게 보입니다). 처음에는 화면에 들어가는 가장 큰 배율로 열립니다.
  모니터에 들어가지 않는 배율은 회색으로 표시되고, 테두리를 끌어서 바꿀 수는 없습니다.
- 마우스는 창 밖으로 자유롭게 나갈 수 있습니다. "화면" → "마우스 가두기"에 체크하면
  다음 실행부터 창 안을 클릭할 때 마우스가 창 안에 갇힙니다. 그때 메뉴를 누를 수
  없으면 Alt+V로 "화면" 메뉴를 열거나 Ctrl+Tab으로 마우스를 풀어 주세요.
- 창 위치와 크기는 종료할 때 저장되고 다음 실행 때 그대로 열립니다.
- 창이 화면 밖에 있거나 너무 크거나, 다른 PC·모니터로 옮겨 해상도가 바뀌면
  자동으로 화면 가운데 알맞은 크기로 돌아갑니다.
- 직접 되돌리려면 Shift 키를 누른 채로 게임을 시작하세요.
- Alt+Enter로 전체 화면과 창 모드를 전환합니다 (모니터 해상도는 바뀌지 않음).
  전체 화면도 정수배로 확대하므로 남는 부분은 검은 테두리로 남습니다.
- 화면은 픽셀이 또렷한 nearest neighbor로 확대합니다. 부드러운 화면이나 CRT·스캔라인
  셰이더를 쓰려면 "cnc-ddraw config.exe" 또는 ddraw.ini의 [ddraw]에서 바꿉니다
  (Windows: d3d9_filter=1이 bilinear. 셰이더 파일은 renderer=opengl과 shader=<파일>,
  파일은 cnc-ddraw 릴리스의 Shaders 폴더. 자세한 것은
  https://github.com/zirize/rotk4-patch 참고).
- 그 밖의 화면 설정도 "cnc-ddraw config.exe" 또는 ddraw.ini에서 바꿀 수 있습니다.

■ 문제가 생기면
- 백신(Windows Defender 등)이 ddraw.dll을 막으면 이 폴더를 허용해 주세요.
  ddraw.dll은 옛 게임을 현대 Windows에서 돌리기 위한 호환 레이어입니다.

■ 포함된 구성 요소
- ddraw.dll: rotk4 호환 shim (256색 화면, 창 활성화, CD 음악 처리, 창 배율)
- cnc-ddraw.dll, ddraw.ini, cnc-ddraw config.exe: cnc-ddraw
  (https://github.com/FunkyFr3sh/cnc-ddraw, MIT 라이선스)
- MP3 재생: minimp3 (https://github.com/lieff/minimp3, CC0)
- 원본 게임 파일(SAN4W95.EXE 등)은 수정하지 않습니다. 패치 자체에는 게임 파일이
  들어 있지 않습니다.
- 라이선스 전문은 licenses 폴더에 있습니다 (이 패치: MIT).
