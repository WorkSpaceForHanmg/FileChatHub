# FileChatHub 명령어 상세 가이드

FileChatHub는 터미널에서 명령어를 직접 입력하여 서버와 상호작용합니다.

## 기본 명령어

| 명령어 | 설명 | 예시 |
|--------|------|------|
| `/ls [폴더]` | 현재 또는 지정 폴더 목록 보기 | `/ls`, `/ls myfolder` |
| `/mkdir <폴더>` | 새 폴더 생성 | `/mkdir myfolder` |
| `/upload <로컬파일> [서버경로]` | 파일 업로드 | `/upload test.txt`, `/upload test.txt backup/test.txt` |
| `/download <서버경로> [로컬파일]` | 파일 다운로드 | `/download server.txt`, `/download backup/server.txt local.txt` |
| `/rm <서버경로>` | 파일/폴더 삭제 | `/rm old.txt`, `/rm myfolder` |
| `/mv <원경로> <새경로>` | 파일/폴더 이름 변경/이동 | `/mv a.txt b.txt`, `/mv oldfolder newfolder` |
| `/share <경로> <상대유저>` | 파일/폴더 공유 | `/share doc.pdf alice` |
| `/unshare <경로> <상대유저>` | 공유 해제 | `/unshare doc.pdf alice` |
| `/sharedwithme` | 나에게 공유된 항목 목록 | `/sharedwithme` |
| `/search <키워드>` | 파일/폴더명 키워드 검색 | `/search report` |
| `/cd <폴더명>` | 폴더 이동 | `/cd myfolder` |
| `/pwd` | 현재 경로 표시 | `/pwd` |
| `/msg <상대유저> <메시지>` | 1:1 채팅 | `/msg alice 안녕하세요` |
| `/who` | 현재 접속자 목록 | `/who` |
| `/help`, `/?` | 명령어 도움말 출력 | `/help` |
| `/quit` | 프로그램 종료 | `/quit` |

---

## 사용 팁

- **명령어는 반드시 `/`로 시작**해야 합니다.
- **파일/폴더 경로**는 절대경로 또는 상대경로 모두 지원합니다.
- **업로드/다운로드** 시 실제 전송/수신 바이트가 다르면 경고가 표시되고, 서버에는 실패가 기록됩니다.
- **공유 받은 파일/폴더**는 `/sharedwithme`로 확인할 수 있습니다.
- **메시지 수신** 시에는 `[받은메시지]`로 안내가 표시됩니다.
- **채팅/명령 입력과 서버 메시지 수신**이 동시에 가능합니다.

---

## 서버 콘솔 안내 메시지 예시

- `[안내] 사용자 'kim' 로그인/접속`
- `[안내] 사용자 'lee' 파일 업로드: server_data/users/lee/report.pdf (10240 bytes)`
- `[경고] 사용자 'kim' 파일 업로드 실패: report.pdf (5000/8000 bytes)`
- `[안내] 사용자 'lee' 연결 종료`

---

## 예시 세션

```
서버 IP 입력: 127.0.0.1
[1:로그인] [2:회원가입] 중 번호 입력: 2
아이디 입력: kim
비밀번호 입력: mypass
OK|회원가입 및 로그인 성공

/ls
[DIR] myfolder
[FILE] hello.txt

/upload hello.txt
[안내] 업로드 시작 (1256 바이트)...
100% 완료
[안내] 업로드 완료
OK|업로드 성공

/share hello.txt alice
OK|공유 성공

/msg alice 안녕! 테스트야
OK|메시지 전송 완료

/quit
[안내] 프로그램을 종료합니다. 감사합니다!
```

---

자세한 사용법/에러/제약 등은 README.md를 참고하세요.