# 삼국지 IV with 파워업키트 한글판 — 윈도우 10/11·Wine 실행 패치

📄 Project page: **[삼국지 IV with 파워업키트 한글판 — 윈도우 10/11·Wine 실행 패치](https://zirize.github.io/rotk4-patch/)** · More projects: **[zirize.github.io](https://zirize.github.io/)**

1996년에 나온 한글 Win95판 **삼국지 IV with 파워업키트**(`SAN4W95.EXE`)를 지금의 Windows와
Linux(Wine)에서 실행하는 무설치 실행 패치입니다. 게임 폴더에 `ddraw.dll` 하나와
[cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw)를 넣는 방식이라, 원본 EXE는 고치지 않고
레지스트리 설정, 실제 CD나 가상 CD 드라이브도 필요 없습니다.

**게임 파일은 들어 있지 않습니다.** 게임은 각자 준비해야 합니다 ([준비물](#2-준비물)).

삼국지 V도 있습니다 → [삼국지 V with 파워업키트 한글판 — 윈도우 10/11·Wine 실행 패치](https://zirize.github.io/rotk5-patch/)

## 1. 고치는 것

- 실행하면 "[색상표]를 256색으로…" 메시지가 나오거나, 아무 메시지 없이 바로 꺼진다
  (게임이 화면이 이미 256색이라고 가정함).
- "CD-ROM 드라이브가 없는 곳에서는…" 또는 "CD-ROM을 넣어 주십시오"가 반복된다.
- 창이 뜨지 않거나 화면이 아래로 밀려 잘리고, 마우스 클릭 위치가 어긋난다.
- 오프닝 뒤 "640×480 / 800×600" 화면 크기 선택이 나오지 않는다.
- 동영상이 작게 나오거나 색이 깨진다.
- 640×480 창이 지금 모니터에서는 너무 작다.

## 2. 준비물

- **게임 파일**: [삼국지 4 파워업키트 for Windows 95 (K)](https://nemo838.tistory.com/2575)
  글의 `Sam4PKK_Win95_220129.7z.001` ~ `.026`. 이 패치는 그 압축을 푼 게임 폴더
  (`SAN4W95.EXE`가 있는 폴더)와 CD 이미지(`Audio.cue`, `Audio.bin`)를 사용합니다.
- **이 패치**: [Releases](https://github.com/zirize/rotk4-patch/releases)의 `rotk4-patch.zip`
  (이 패치의 `ddraw.dll`, cnc-ddraw v7.1.0.0, 설정 파일 `ddraw.ini`, 안내서 `읽어보기.txt`).

## 3. 설치와 실행

### Windows 10/11

1. 받은 `.7z.001`을 7-Zip으로 풉니다.
2. 게임 폴더를 쓰기 가능한 곳에 둡니다 (예: `C:\Games` 아래).
   `C:\Program Files` 아래에서는 저장이 안 됩니다.
3. `rotk4-patch.zip`의 파일을 모두 게임 폴더에 풉니다.
4. 게임 폴더 안에 `CD` 폴더를 만들고 `Audio.cue`와 `Audio.bin`을 넣습니다.
   게임이 시작할 때 CD를 검사하므로 꼭 필요합니다.
5. `SAN4W95.EXE`를 실행합니다.

메뉴의 **화면** → x1 / x2 / x3으로 창을 정수배로 키웁니다. Alt+Enter는 전체 화면 전환입니다.
그 밖의 사용법과 문제 해결은 `읽어보기.txt`에 있습니다.

### Linux (Wine)

위 1~4와 같이 준비한 뒤, Wine prefix에 다음을 해 둡니다.

- 한글 로캘과 코드페이지: `HKCU\Control Panel\International`에 `Locale=00000412`,
  `LocaleName=ko-KR`; `HKCU\Software\Wine\Fonts`에 `Codepages=949,949`.
- 한글 글꼴을 `drive_c/windows/Fonts`에 넣습니다 (확인은 굴림 `gulim.ttc`로 했습니다).
- CD-ROM 종류의 드라이브 하나: `HKLM\Software\Wine\Drives`에 `"e:"="cdrom"`
  (빈 디렉터리를 연결해도 됩니다). 게임이 CD-ROM 드라이브가 있는지만 봅니다.

```sh
cd /path/to/game             # 게임 폴더에서 실행해야 합니다
WINEDLLOVERRIDES=ddraw=n,b LANG=ko_KR.UTF-8 wine SAN4W95.EXE
```

### CD 음악을 MP3로 줄이기 (선택)

`Audio.bin`은 약 480 MB입니다. `tools/cd2mp3.sh`(Linux, `lame` 필요)로 음악 트랙을 MP3로
바꾸면 약 63 MB가 되고 `Audio.bin`은 지워도 됩니다.

```sh
tools/cd2mp3.sh /path/to/Audio.cue /path/to/game/CD
```

## 4. 동작 원리

게임 폴더의 `ddraw.dll`(이 패치)이 먼저 불리고, 실제 화면 처리는 `cnc-ddraw.dll`이 맡습니다.
이 패치는 그 사이에서 다음을 처리합니다.

- 256색 검사와 화면 모드: cnc-ddraw가 게임보다 먼저 올라오게 해서 256색 화면으로 보이게
  하고, 게임이 하지 않는 8비트 모드 설정을 대신 합니다.
- 창 활성화: Win95 DirectDraw가 해 주던 창 표시·활성화를 대신 합니다.
- CD: CD 이미지(BIN/CUE, 또는 MP3)로 MCI CD 오디오를 흉내 내어 CD 검사와 배경 음악을 처리합니다.
- 화면 크기: 640×480 / 800×600 선택이 나오게 하고, 창을 게임 화면의 정수배로 맞춥니다.
- 동영상: 게임 화면 크기로 맞추고, 동영상마다 만든 팔레트로 색을 맞춥니다.

역공학으로 찾은 주소와 자세한 내용은
[docs/san4w95-internals.md](https://github.com/zirize/rotk4-patch/blob/main/docs/san4w95-internals.md)에 있습니다.

### 직접 빌드

```sh
sudo apt install gcc-mingw-w64-i686 p7zip-full
make -C shim                                   # -> shim/ddraw.dll
tools/make-patch.sh cnc-ddraw.zip rotk4-patch.zip
```

`cnc-ddraw.zip`은 [cnc-ddraw v7.1.0.0](https://github.com/FunkyFr3sh/cnc-ddraw/releases/tag/v7.1.0.0)의
`cnc-ddraw.zip`입니다.

## 5. 시험한 환경

- Windows 11 (1920×1200, 배율 125%)
- Ubuntu 26.04 LTS, Wine 10.0

두 환경에서 게임 진행, 음악, 동영상, 640×480 / 800×600 선택, 창 정수배 확대와 마우스 가두기
메뉴를 확인했습니다. 윈도우에서는 MP3 음악을 넣은 배포본으로 시험했습니다. CD 이미지(BIN)를 그대로 넣는 방식은 Wine에서만 시험했습니다. Windows 10에서는 시험하지 않았습니다.

## 6. 라이선스

- 이 패치: MIT ([LICENSE](LICENSE))
- [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw): MIT, Releases의 zip에 함께 들어 있습니다
- [minimp3](https://github.com/lieff/minimp3): CC0 (`shim/minimp3/`)

이 프로젝트는 KOEI TECMO와 관계가 없습니다. 게임 파일과 게임의 음악·동영상은 포함하지
않으며 배포하지 않습니다.
