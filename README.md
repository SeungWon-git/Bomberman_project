# 💣 Bomberman (봄버맨 게임)

## 🎮 개요
 - 게임 장르: 멀티 아케이드 게임
 - 플랫폼: PC
 - 플레이 인원: 2~4인
 - 3인 개발
 - **구현 내용**:
   > + **소캣 프로그래밍**을 활용한 멀티 플레이 구현
   > + 플레이어 이동 & 체력, 폭탄 생성, 폭발 효과, 블록 생성 & 파괴, 아이템 생성 & 획득와 같은 여러가지 게임 요소 동기화
 - 개발에 사용된 기술 스택:
   + WinAPI
   + TCP 소켓 프로그래밍
 - 시연 영상: [▶️ 유투브 영상 보기](https://www.youtube.com/)
<img width="750" height="414" alt="image" src="https://github.com/user-attachments/assets/0051c709-3dfc-48d6-b5c7-73f10142192a" />

---

## 📝 구현 내용 상세 설명

### TCP 소켓 통신을 이용하여 멀티 환경 조성
* 원할한 통신 방식을 설계하기 위해 자세한 Flow Chart를 작성
<details>
  <summary>서버 Flow Chart</summary>
<img width="1324" height="1362" alt="image" src="https://github.com/user-attachments/assets/8f6272e8-be0a-4917-8c92-446746fde1ca" />
</details>

<details>
  <summary>클라 Flow Chart</summary>
<img width="1338" height="841" alt="image" src="https://github.com/user-attachments/assets/2470ee5c-46b5-4207-8dd4-08afa84fb8dd" />
</details>

* 코드 보기: 

### 💥 2D 평면 상의 충돌체크 (플레이어-벽돌, 플레이어-아이템, 플레이어-폭발)
- 
* 코드 보기:
  
### ⚙️ Nagle 알고리즘 off → 패킷 전송 지연 최소화로 실시간 반응성 업
- 
* 코드 보기:
  
### 🗺️ json을 이용한 맵 생성기
- json을 이용하여 맵 수정, 새로 생성이 용이
* 코드 보기: 
