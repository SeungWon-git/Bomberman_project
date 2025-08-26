# 💣 Bomberman (봄버맨 게임)

## 🎮 개요
 - 게임 장르: 멀티 아케이드 게임
 - 플랫폼: PC
 - 플레이 인원: 2~4인
 - 3인 개발
 - **구현 내용**:
   > + **TCP 소캣 프로그래밍**을 활용한 멀티 플레이 구현
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

* 코드 보기: [서버 패킷 처리 함수](https://github.com/SeungWon-git/Bomberman_project/blob/557ffd930e9c68a7b74187989d04ce450457a3ae/Multi_game_server/Multi_game_server/Multi_Server.cpp#L800), [클라 패킷 처리 함수](https://github.com/SeungWon-git/Bomberman_project/blob/557ffd930e9c68a7b74187989d04ce450457a3ae/Multi_game_client/Bomberman_project/Bomberman_project.cpp#L1140)

### 💥 2D 평면 상의 충돌 체크
- 먼저, 충돌 체크는 정확한 동기화를 위해 서버에서 확인 → 클라로 결과 전송
  * 예를 들어 클라 A가 이동을 한다고 하면 '이동 패킷'을 서버에 전송하고, 서버는 해당 플레이어와 맵 정보를 이용해 해당 플레이어가 이동 가능한 방향으로 움직였는지 여부를 파악하고 결과를 다시 '이동 확인 패킷'에 넣어 각 클라들에게 전송해줌
- 이때, 서버는 맵 정보를 타일 형태로 기억하고 이를 이용해 플레이어와 다양한 오브젝트 간의 충돌체크를 계산 → 맵을 타일 형태로 기억하여 충돌 체크에 사용하는 히트박스 계산이 편리함
- 충돌 체크는 윈도우 API에 내장 함수인 'bool IntersectRect'함수를 사용하여 두 사각형(히트박스) 사이에 서로 겹치는 영역이 있는지 파악하여 판단
- 폭발의 경우에는 폭탄을 놓고 지연시간 이후에 폭발이 일어나기 때문에 타이머를 이용하여 폭발을 시기를 설정 → 폭발은 또한 일정시간 동안 머물러서 플레이어를 피격할 수 있음 (*폭발 당시에만 피해주는 방식이 아님)
  * 이때, 플레이어가 폭발 지역에 계속 머물러 여러번 피격 당하지는 않도록 하기 위해 피격 이후 일정 무적시간을 부여해 준다.
* 코드 보기:  [서버 맵 정보 타일 형태로 저장](), [서버 '플레이어-객체' 간 충돌 체크 확인 함수](), [서버 '폭발-객체' 간 충돌 체크 확인 함수](), [폭탄 지연시간 설정]()

### ⚙️ Nagle 알고리즘 off 
- Nagle 알고리즘 설정을 끔으로써, 작은 데이터라도 바로바로 네트워크로 전송 → 패킷 전송 지연 최소화로 실시간 반응성 향상
* 코드 보기: [서버 Nagle 알고리즘 설정](https://github.com/SeungWon-git/Bomberman_project/blob/557ffd930e9c68a7b74187989d04ce450457a3ae/Multi_game_server/Multi_game_server/Multi_Server.cpp#L129), [클라 Nagle 알고리즘 설정](https://github.com/SeungWon-git/Bomberman_project/blob/557ffd930e9c68a7b74187989d04ce450457a3ae/Multi_game_client/Bomberman_project/Bomberman_project.cpp#L152)
  
### 🗺️ json을 이용한 맵 생성기
- json을 이용하여 맵 수정, 새로 생성이 용이
* 코드 보기: [맵 로더 함수](https://github.com/SeungWon-git/Bomberman_project/blob/557ffd930e9c68a7b74187989d04ce450457a3ae/Multi_game_server/Multi_game_server/Multi_Server.cpp#L466), [맵1 저장 형태](), [맵2 저장 형태]()
