# SW-AI-W08 · PintOS Project 2: User Programs

크래프톤 정글 **Week8** 팀 레포예요 (๑•̀ㅂ•́)و
KAIST PintOS의 **Project 2: User Programs**를 같이 구현해요. 기간은 2026-10-16(금)부터 10-22(목)까지예요.

## 팀원

| GitHub | 역할 |
|---|---|
| [Shirokuma0704](https://github.com/Shirokuma0704) | 메인 레포 관리 |
| [1224kang](https://github.com/1224kang) | |
| [minjun0307](https://github.com/minjun0307) | |

레포를 포크한 계정 기준으로 적었어요. 빠진 분이나 역할은 PR로 채워 주세요.

## 이번 주 범위

- **이번 주**: 인자 전달(Argument Passing)부터 `fork` 직전까지예요.
- **다음 주로 미룸**: `fork` / `exec` / `wait`는 Project 3(VM)에서 다시 크게 다뤄요. 그래서 다음 주에 제대로 끝내요 (10-16 코치님 말씀).
- **Extra(`dup2`)**: 이번 주에는 하지 않아요.

## 숫자로 보는 Project 2

```
테스트          95개  = Project 2 77개 (userprog 63 + filesys 13 + multi-oom 1) + Project 1 18개
채점 비중        기능 40% · 견고성 30% · 파일 시스템 15% · multi-oom 10% · Project 1 5%
모범 답안 크기   파일 6개, +782 / -37 줄 (Extra 약 150줄 포함)
고칠 곳         userprog/process.c, userprog/syscall.c 가 대부분
```

모범 답안 크기는 KAIST 문서 FAQ에 나와 있어요. 테스트 개수와 비중은 `userprog/Make.vars`, `tests/userprog/Grading.no-extra`, `Rubric.*`를 직접 세서 적었어요.

## 링크

- 보드: [krafton_jungle_week8_project](https://github.com/users/Shirokuma0704/projects/5)
- 팀 위키(문서 한국어 요약): [SW-AI-W08 Wiki](https://github.com/Shirokuma0704/SW-AI-W08/wiki)
- KAIST 원문: https://casys-kaist.github.io/pintos-kaist/

위키는 원문을 요약한 거예요. 처음 감 잡을 때 보고, 중요한 건 원문에서 다시 확인해 주세요 (｡•̀ᴗ-)✧

## 협업 규칙

**Forking Workflow**로 일해요. 이 레포가 팀 공용 레포이고, 각자 포크해서 기능 브랜치에서 작업한 뒤 `develop`으로 PR을 보내요.

```
이슈 → 보드 Todo → 담당자 지정 + 브랜치 → In Progress → 커밋/푸시 → PR (In Review) → 리뷰·테스트 → 머지 → 이슈 닫힘 → Done
```

| 대상 | 형식 | 예시 |
|---|---|---|
| 브랜치 | `<type>/<이슈번호>-<설명>` | `feature/9-args-single`, `fix/21-priority-donation` |
| 커밋 | `<type>: <설명> (#이슈)` | `feat: 명령줄을 단어로 쪼개기 (#9)` |
| PR 제목 | 커밋과 같은 모양 | `feat: 명령줄을 단어로 쪼개기 (#9)` |

- 커밋 type은 `feat` / `fix` / `refactor` / `docs` / `chore` 중에서 골라요.
- 커밋 하나에는 논리적인 변경 하나만 담아요. 이슈 번호는 꼭 붙여요.
- PR 본문은 레포의 PR 템플릿을 채워요. 관련 이슈도 꼭 연결해요.
- **`main`과 `develop`에는 직접 푸시하지 않아요.** PR로만 들어가요.
- **머지는 22:00 코어타임에 다 같이 보고 해요.** 그 전에는 PR까지만 열어 둬요.
- 이슈는 커밋이 아니라 **PR이 머지될 때** 닫혀요.
- `thread.c`, `synch.c`처럼 같이 쓰는 파일을 바꾸면 자주 공유해요.
- `develop`이 기본 브랜치이고, `main`은 완성본을 올리는 브랜치예요.

## 하루 일정

| 시간 | 할 일 |
|---|---|
| 10:00 스크럼 | 보드에서 오늘 할 이슈 고르기, 담당 정하기, 최신 `develop` 받기, In Progress로 옮기기 |
| 16:00 중간 점검 | 진행 상황, 막힌 곳, 이슈 우선순위 다시 보기 |
| 22:00 코어타임 | PR 리뷰와 테스트 결과 보기, 통과한 PR 머지, 이슈 닫기, 내일 계획 |

## 빌드와 테스트

먼저 `pintos` 폴더에서 `activate`를 한 번 실행해요. `pintos/utils`를 PATH에 넣어서 `pintos` 명령을 쓸 수 있게 해 줘요.

```bash
cd pintos
source ./activate
```

Project 2는 `userprog` 폴더에서 빌드하고 테스트해요.

```bash
cd pintos/userprog
make
cd build
make check                                   # 전체 테스트
make tests/userprog/args-single.result       # 테스트 하나
```

- 테스트 하나를 돌리면 출력은 `.output`에, 판정은 `.result`에 저장돼요.
- "up to date"라며 다시 안 돌면 그 테스트의 `.output` 파일을 지우고 다시 돌려요.
- 디버깅은 `gdb`로 해요. 방법은 KAIST 원문의 Appendix: Debugging Tools에 있어요.

## 개발 환경 (Docker)

정글에서 준 Docker 환경(`ubuntu:22.04`, x86-64, qemu)을 그대로 써요 ( ˘ᵕ˘ )

1. [Docker Desktop](https://www.docker.com/products/docker-desktop)을 설치하고 실행해요.
2. 이 레포를 포크한 다음, 내 포크를 클론해요.
3. 클론한 폴더를 Dev Container로 열어요.
   - VSCode: `Ctrl+Shift+P` → `Dev Containers: Reopen in Container`
   - CLion: Dev Container로 열기
   - 처음 열 때는 이미지를 빌드하느라 시간이 좀 걸려요.
4. 터미널에서 `pintos` 명령을 못 찾으면, 위 "빌드와 테스트"처럼 `source ./activate`를 직접 실행해요.
   - `.devcontainer/Dockerfile`은 `/workspaces/pintos_22.04_lab_docker/pintos/activate`를 `.bashrc`에서 자동 실행해요. 우리 레포는 폴더 이름이 달라서 이 경로가 없을 수 있어요.

VSCode의 F5 디버깅은 지원하지 않아요. 디버깅은 `gdb`로 해요.
