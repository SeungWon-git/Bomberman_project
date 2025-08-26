#include "stdafx.h"
#include "json/json.h"
#include "protocol.h"
#include "Session.h"
#include "Object.h"

///////////////////////////////////////////////////////////

//플레이어
array<Session, MAX_USER> clients;

//플레이어 DB
vector<Session_DB> clients_DB;

char g_id_buf[BUFSIZE] = " ";

//맵
template<typename T, size_t X, size_t Y>
using tileArr = array<array<T, X>, Y>;

tileArr<int, tile_max_w_num, tile_max_h_num>	map_1;
tileArr<int, tile_max_w_num, tile_max_h_num>	map_2;
tileArr<int, tile_max_w_num, tile_max_h_num>	selectedMap;

int map_num;	//몇 번 맵 선택?

HANDLE hThread[MAX_USER + 1];

//폭탄
std::deque <Bomb>	bombs;
std::deque <vector<pair<int, int>>>	explosionVecs;  //폭발 맵위치 벡터큐

bool g_shutdown = false;

int g_b_count = 0;

//타이머
HANDLE htimerEvent; // 타이머 쓰레드 시작용

struct timer_event {
	int obj_id;
	chrono::system_clock::time_point	start_time;
	EVENT_TYPE order;
	int target_id;
	constexpr bool operator < (const timer_event& _Left) const
	{
		return (start_time > _Left.start_time);
	}

};

concurrency::concurrent_priority_queue <timer_event> timer_queue;

//////////////////////////////////////////////////////////

void Err_quit(const char* msg);
bool Get_status(int client_index, char* id);
void Init_client(int client_index);
bool Check_all_ready();
void Send_all_play_start();
void Process_packet(int client_index, char* p);
int Get_new_index();
void Load_Map(tileArr<int, tile_max_w_num, tile_max_h_num>& map, const char* map_path);
void Setting_Map();
int Check_Collision(int source_type, int source_index);
int Check_Expl_Collision(int source_type, int source_index, vector<pair<int, int>>& expl);
void Timer_Event(int _obj_id, EVENT_TYPE ev, std::chrono::milliseconds ms);
void Disconnect(int c_id);
void Send_change_player(int _index);
void SendExplosionEnd(int ix, int iy);
void SendCreateBlock(int ix, int iy, char id[], bool isSuccess);
void PrintMap();

DWORD WINAPI Do_timer(LPVOID arg);
DWORD WINAPI Thread(LPVOID arg);

std::pair<int, int> MapIndexToWindowPos(int ix, int iy);
std::pair<int, int> WindowPosToMapIndex(int x, int y);

//////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	//플레이어 DB 읽기
	clients_DB.reserve(MAX_USER);

	ifstream in("플레이어_정보.txt");
	if (!in) {
		cout << "DB 파일 읽기 실패" << endl;
		getchar();
		exit(1);
	}

	for (int i = 0; i < MAX_USER; ++i) {                         //v_id의 벡터는 비워져 있고 i의 카운트당 원소가 채워지므로 i값을 벡터의 인덱스로 생각하며 두개의 map에 v_id[i]의 값을 넣어줌 
		clients_DB.push_back(Session_DB(in));
	}

	//맵 읽기
	Load_Map(map_1, "maps_json/map_1.json");
	Load_Map(map_2, "maps_json/map_2.json");

	while (TRUE) {
		cout << "몇번 맵을 플레이 하실껀가요?(1, 2 중 선택): ";
		scanf("%d", &map_num);

		if (map_num == 1 || map_num == 2) {
			cout << map_num << " 번 맵을 선택하였습니다." << endl << endl;
			break;
		}
		else {
			cout << "잘못 입력하셨습니다. (1, 2 중 하나를 선택하여 주세요.)" << endl << endl;
		}
	}

	Setting_Map();

	//타이머 스레드 스위치용
	htimerEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	//윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return 1;

	//리슨 소켓 생성
	SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_socket == INVALID_SOCKET) Err_quit("socket()");

	//Nagle 알고리즘 적용X
	bool optval = TRUE;
	int retval = setsockopt(listen_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
	if (retval == SOCKET_ERROR) Err_quit("connect()");

	//bind
	SOCKADDR_IN server_addr;
	ZeroMemory(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(SERVER_PORT);
	bind(listen_socket, (SOCKADDR*)&server_addr, sizeof(server_addr));
	listen(listen_socket, SOMAXCONN);

	//타이머 쓰레드 만들기
	hThread[0] = CreateThread(NULL, 0, Do_timer, NULL, 0, NULL);

	for (int i = 1; i < MAX_USER + 1; ++i) {
		// 데이터 통신에 사용할 변수
		SOCKET client_sock;
		SOCKADDR_IN clientaddr;
		int addrlen;
		addrlen = sizeof(clientaddr);
		client_sock = accept(listen_socket, (SOCKADDR*)&clientaddr, &addrlen);

		//Nagle 알고리즘 적용X
		optval = TRUE;
		retval = setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (char*)&optval, sizeof(optval));
		if (retval == SOCKET_ERROR) Err_quit("connect()");

		// 접속한 클라이언트 정보 출력
		std::cout << "[TCP 서버] 클라이언트 접속: IP 주소 " <<
			inet_ntoa(clientaddr.sin_addr) << "  포트 번호 : " << ntohs(clientaddr.sin_port) << endl;


		hThread[i] = CreateThread(NULL, 0, Thread, (LPVOID)client_sock, 0, NULL);

	}

	WaitForMultipleObjects(MAX_USER + 1, hThread, TRUE, INFINITE);

	for (int i = 0; i < MAX_USER + 1; ++i)
		CloseHandle(hThread[i]);

	CloseHandle(htimerEvent);
	closesocket(listen_socket);
	WSACleanup();

	for (auto& cl : clients) {
		if (NO_ACCEPT != cl._state)
			Disconnect(cl._index);
	}

	return 0;
}

DWORD WINAPI Do_timer(LPVOID arg) {
	WaitForSingleObject(htimerEvent, INFINITE);

	while (true) {
		
		timer_event ev;
		bool ret = timer_queue.try_pop(ev);
		if (ret == false) continue;
		int _id = ev.obj_id;
		if (ev.start_time <= chrono::system_clock::now()) {
			if (ev.order == START_EXPL) 
			{
				//1. 폭발 시작 - 맵 업데이트(폭발 범위 측정)
				bombs.front().Explode(selectedMap, clients);
				//2. 폭탄이 삭제되기 전 전역큐에 폭발범위에 해당하는 맵인덱스들을 넣는다.
				explosionVecs.push_back(bombs.front().explosionMapIndexs);
				
				//3. 폭발 시작 시 정지해 있는 플레이어 체크(움직일때는 따로 충돌체크함)
				for (auto& cl : clients) {
					if (cl.in_use == false) continue;
					if(cl._state != PLAY) continue;
				     Check_Expl_Collision(0, cl._index,bombs.front().explosionMapIndexs);
				}
				
				//확인용 출력
				//PrintMap();

				//4. 폭탄 삭제전 플레이어 현재 폭탄 갯수 갱신
				int bt = clients[_id]._current_bomb_count;
				if (bt > 0) { 
					--clients[_id]._current_bomb_count;
				};
				
				bombs.pop_front();
			}
			else if (ev.order == END_EXPL) // 폭발 끝
			{
				// 전역큐의 첫번째 원소에는 폭발범위가 있고
				for (auto& explosionMapIndex : explosionVecs.front()) {
					auto [ix, iy] = explosionMapIndex;
					// 폭발을 끝내기 위해 맵 인덱스를 하나씩 클라로 보낸다. -> 클라에서 폭발 효과 끔
					SendExplosionEnd(ix, iy);
					selectedMap[iy][ix] = EMPTY;
				}
				// 폭발삭제
				explosionVecs.pop_front();

				//확인용 출력
				//PrintMap();

			}
			else if (ev.order == TURN_Damage)
			{
				cout << "플레이어 폭발 피격!!" << endl;
				clients[ev.obj_id]._heart--;
				cout << "피격 플레이어: " << clients[ev.obj_id]._id << ", 체력:" << clients[ev.obj_id]._heart << endl;

				if (clients[ev.obj_id]._heart <= 0) {
					clients[ev.obj_id]._state = DEAD;
					cout << "플레이어: " << clients[ev.obj_id]._id << " 사망!!!" << endl;
				}

				clients[ev.obj_id].no_damage = false;
				Send_change_player(ev.obj_id);
			}
		}
		else {
			timer_queue.push(ev);
			this_thread::sleep_for(10ms);
		}
	}

}

void SendExplosionEnd(int ix, int iy) {
	for (auto& pl : clients) {
		if (pl._state != PLAY) continue;
		if (true == pl.in_use)
		{
			CHECK_EXPLOSION_packet check_explosion_packet;
			check_explosion_packet.size = sizeof(check_explosion_packet);
			check_explosion_packet.type = CHECK_EXPLOSION;
			check_explosion_packet.ix = ix;
			check_explosion_packet.iy = iy;
			check_explosion_packet.isActive = false;
			pl.Do_send(sizeof(check_explosion_packet), &check_explosion_packet);
		}
	}
}

void Send_change_player(int _index) {

	Session& cl = clients[_index];
	for (auto& pl : clients) {
		if (pl._state == NO_ACCEPT) continue;
		if (true == pl.in_use)
		{
			PLAYER_CHANGE_STATE_packet packet;
			packet.size = sizeof(PLAYER_CHANGE_STATE_packet);
			packet.type = CHANGE_STATE;
			packet.x = cl._x;
			packet.y = cl._y;
			packet.state = cl._state;
			packet.hp = cl._heart;
			strcpy_s(packet.id, cl._id);
			pl.Do_send(sizeof(packet), &packet);
		}
	}
}

void SendCreateBlock(int ix, int iy, char id[], bool isSuccess) {
	for (auto& pl : clients) {
		if (pl._state != PLAY) continue;
		if (true == pl.in_use)
		{
			CREATE_ROCK_packet create_rock_packet;
			create_rock_packet.size = sizeof(create_rock_packet);
			create_rock_packet.type = CREATE_ROCK;
			create_rock_packet.ix = ix;
			create_rock_packet.iy = iy;
			create_rock_packet.isSuccess = isSuccess;
			strcpy_s(create_rock_packet.id, id);
			pl.Do_send(sizeof(create_rock_packet), &create_rock_packet);
		}
	}
}

//맵 상태 전체 출력
void PrintMap() {
	for (int i = 0; i < tile_max_h_num; ++i) {
		for (int j = 0; j < tile_max_w_num; ++j)
			cout << selectedMap[i][j] << ' ';
		cout << endl;
	}
	cout << endl;
}

void Err_quit(const char* msg)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	MessageBox(NULL, (LPCTSTR)lpMsgBuf, (LPCWSTR)msg, MB_ICONERROR);
	LocalFree(lpMsgBuf);
	exit(1);
}

bool Get_status(int client_index, char* id)
{
	//아이디 검색
	strcpy_s(g_id_buf, id);
	auto b_n = find_if(clients_DB.cbegin(), clients_DB.cend(), [](const Session_DB& a) {
		return strcmp(a._id, g_id_buf) == 0;
		});
	if (b_n == clients_DB.end()) {
		return false;
	}

	//레벨, 경험치 DB용 데이터 초기화
	strcpy_s(clients[client_index]._id, id);
	clients[client_index]._level = b_n->_level;
	clients[client_index]._exp = b_n->_exp;

	//기타 인게임 데이터 초기화
	Init_client(client_index);

	return true;
}

//인게임 데이터 초기화
void Init_client(int client_index)
{
	//맵별 위치 지정
	if (map_num == 1) {
		switch (client_index) {
		case 0:
			clients[client_index]._x = outer_wall_start + tile_size + 10;
			clients[client_index]._y = outer_wall_start + tile_size + 10;
			break;

		case 1:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 12;
			clients[client_index]._y = outer_wall_start + tile_size + 10;
			break;

		case 2:
			clients[client_index]._x = outer_wall_start + tile_size + 10;
			clients[client_index]._y = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			break;

		case 3:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 12;
			clients[client_index]._y = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			break;
		}
	}
	else if (map_num == 2) {
		switch (client_index) {
		case 0:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			clients[client_index]._y = outer_wall_start + tile_size + 10;
			break;

		case 1:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 7;
			clients[client_index]._y = outer_wall_start + tile_size + 10;
			break;

		case 2:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			clients[client_index]._y = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			break;

		case 3:
			clients[client_index]._x = outer_wall_start + tile_size + 10 + (block_size + 1) * 7;
			clients[client_index]._y = outer_wall_start + tile_size + 10 + (block_size + 1) * 5;
			break;
		}
	}

	clients[client_index]._dir = 0;
	clients[client_index]._power = 1;
	clients[client_index]._heart = 3;
	clients[client_index]._bomb_max_count = 2;
	clients[client_index]._rock_count = 0;
	clients[client_index]._state = ACCEPT;
}

//모든 플레이어가 READY 상태인지 검사
//모두 READY 상태라면 PLAY 상태로 변경
bool Check_all_ready()
{
	for (auto& cl : clients)
	{
		if (cl.in_use == TRUE && cl._state != READY)
			return false;
	}

	cout << endl;
	cout << "<<게임 스타트>>" << endl;

	for (auto& cl : clients)
	{
		if (cl.in_use == TRUE) {
			cout << "클라이언트 \'" << cl._id << "\' - 플레이 상태" << endl;
			//인게임 데이터 초기화 - 위치 등등...
			Init_client(cl._index);
			cl._state = PLAY;
		}
	}

	return true;
}

void Send_all_play_start()
{
	for (auto& other : clients)
	{
		if (other.in_use) {
			PLAYER_CHANGE_STATE_packet state_packet;
			state_packet.size = sizeof(state_packet);
			state_packet.type = CHANGE_STATE;

			for (auto& another : clients) {
				if (another.in_use) {
					state_packet.x = another._x;
					state_packet.y = another._y;
					state_packet.state = another._state;
					state_packet.hp = another._heart;
					strcpy_s(state_packet.id, another._id);

					other.Do_send(sizeof(state_packet), &state_packet);
				}
			}
		}
	}
}

void Load_Map(tileArr<int, tile_max_w_num, tile_max_h_num>& map, const char* map_path)
{
	ifstream json_map;
	Json::CharReaderBuilder builder;
	builder["collectComments"] = false;
	Json::Value value;
	JSONCPP_STRING errs;
	bool ok;

	json_map.open(map_path);

	ok = parseFromStream(builder, json_map, &value, &errs);

	if (ok == true) {
		for (int i = 0; i < tile_max_h_num; ++i) {
			for (int j = 0; j < tile_max_w_num; ++j) {
				switch (value[i][j].asInt()) {
				case 0:
					map[i][j] = EMPTY;
					break;

				case 1:
					map[i][j] = BLOCK;
					break;

				case 2:
					map[i][j] = ROCK;
					break;

				case 3:
					map[i][j] = SPECIALROCK;
					break;
				}
			}
		}
	}
	else {
		char msg[256]{ "" };
		char _msg[]{ " 맵을 불러오지 못하였습니다." };
		strcat(msg, map_path);
		strcat(msg, _msg);
		MessageBox(NULL, (LPCWSTR)msg, L"ERROR - Parse failed", MB_ICONERROR);
		json_map.close();
		exit(0);
	}

	json_map.close();
}

//맵 세팅
void Setting_Map()
{
	int bl_indx = 0;
	int r_indx = 0;

	switch (map_num) {
	case 1:
		selectedMap = map_1;
		break;

	case 2:
		selectedMap = map_2;
		break;
	}

	for (int i = 0; i < nTiles; ++i) {
		if (selectedMap[i / tile_max_w_num][i % tile_max_w_num] == BLOCK) {
			int X = outer_wall_start + (i % tile_max_w_num) * tile_size;
			int Y = outer_wall_start + (i / tile_max_w_num) * tile_size;

			bl_indx++;
		}
		else if (selectedMap[i / tile_max_w_num][i % tile_max_w_num] == ROCK || selectedMap[i / tile_max_w_num][i % tile_max_w_num] == SPECIALROCK) {
			int X = outer_wall_start + (i % tile_max_w_num) * tile_size;
			int Y = outer_wall_start + (i / tile_max_w_num) * tile_size;

			r_indx++;
		}
	}
}

//===== 충돌체크 함수
//충돌 발생시 1 리턴 / 충돌이 없으면 0 리턴

//--- '폭발-정지한 플레이어' 간 충돌체크용 함수 (밑에 충돌체크 함수보다 부하 적음)
int Check_Expl_Collision(int source_type, int source_index, vector<pair<int, int>>& expl)
{
	int s_x{ 0 }, s_y{ 0 };
	int s_x_bias{ 0 }, s_y_bias{ 0 };

	switch (source_type) {
	case 0:	//플레이어
		s_x = clients[source_index]._x;
		s_y = clients[source_index]._y;
		s_x_bias = p_size;
		s_y_bias = p_size;
		break;
	}

	RECT temp;
	RECT source_rt{ s_x, s_y, s_x + s_x_bias, s_y + s_y_bias };

	if (s_x >= bg_w - outer_wall_start - p_size / 3)
		return 1;
	if (s_x <= outer_wall_start - p_size / 3)
		return 1;
	if (s_y >= bg_h - outer_wall_start - p_size / 3)
		return 1;
	if (s_y <= outer_wall_start - p_size / 3)
		return 1;

	for (auto& explosionMapIndex : expl) {
			
			auto [ix, iy] = explosionMapIndex;
		    //윈도우 상 좌표
			auto [window_x, window_y] = MapIndexToWindowPos(ix, iy);

			RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

			if (IntersectRect(&temp, &source_rt, &target_rt)) {
				if (clients[source_index].no_damage == false) {
					clients[source_index].no_damage = true;
					Timer_Event(source_index, TURN_Damage, 500ms);
					return 1;
				}
			}
		};

	return 0;	//충돌X
}

//--- '움직이는 플레이어-객체' 간 충돌체크용 함수
int Check_Collision(int source_type, int source_index)
{
	int s_x{ 0 }, s_y{ 0 };
	int s_x_bias{ 0 }, s_y_bias{ 0 };

	switch (source_type) {
	case 0:	//플레이어
		s_x = clients[source_index]._x;
		s_y = clients[source_index]._y;
		s_x_bias = p_size;
		s_y_bias = p_size;
		break;
	}

	RECT temp;
	RECT source_rt{ s_x, s_y, s_x + s_x_bias, s_y + s_y_bias };

	if (s_x >= bg_w - outer_wall_start - p_size / 3)
		return 1;
	if (s_x <= outer_wall_start - p_size / 3)
		return 1;
	if (s_y >= bg_h - outer_wall_start - p_size / 3)
		return 1;
	if (s_y <= outer_wall_start - p_size / 3)
		return 1;

	for (int iy = 0; iy < tile_max_h_num; ++iy)
		for (int ix = 0; ix < tile_max_w_num; ++ix) {
			//윈도우 상 좌표
			auto [window_x, window_y] = MapIndexToWindowPos(ix, iy);

			//오브젝트 그리기
			switch (selectedMap[iy][ix]) {
			case BLOCK:			//블록
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt))
					return 1;

				break;
			}
			case ROCK:			//돌
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt))
					return 1;

				break;
			}
			case SPECIALROCK:			//아이템나오는 돌
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt))
					return 1;

				break;
			}
			//case BOMB:			//폭탄
			//{
			//	RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

			//	if (IntersectRect(&temp, &source_rt, &target_rt))
			//		return 1;

			//	break;
			//}
			case EXPLOSION:		//폭발
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt)){
					if (clients[source_index].no_damage == false) {
						clients[source_index].no_damage = true;
						Timer_Event(source_index, TURN_Damage, 500ms);
						return 1;
					}
					return 1;
				}
				break;
			}
			case ITEM_HEART:
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt)) {
					//패킷 보내기
					PLAYER_ITEM_BUFF_packet buff_packet;
					buff_packet.size = sizeof(buff_packet);
					buff_packet.type = ITEM_BUFF;
					buff_packet.item_type = ITEM_HEART;
					buff_packet.ix = ix;
					buff_packet.iy = iy;
					strcpy_s(buff_packet.id, clients[source_index]._id);
					if (clients[source_index]._heart < 5) ++clients[source_index]._heart;
					selectedMap[iy][ix] = EMPTY;
					for (auto& cl : clients) {
						if (cl.in_use == false) continue;
						if (cl._state != PLAY) continue;
						cl.Do_send(sizeof(buff_packet), &buff_packet);
					}
					return 0;
				}

				break;
			}
			case ITEM_MORE_BOMB:
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt)) {
					//패킷 보내기
					PLAYER_ITEM_BUFF_packet buff_packet;
					buff_packet.size = sizeof(buff_packet);
					buff_packet.type = ITEM_BUFF;
					buff_packet.item_type = ITEM_MORE_BOMB;
					buff_packet.ix = ix;
					buff_packet.iy = iy;
					strcpy_s(buff_packet.id, clients[source_index]._id);
					if (clients[source_index]._bomb_max_count < 5) ++clients[source_index]._bomb_max_count;
					selectedMap[iy][ix] = EMPTY;
					for (auto& cl : clients) {
						if (cl.in_use == false) continue;
						if (cl._state != PLAY) continue;
						cl.Do_send(sizeof(buff_packet), &buff_packet);
					}
					
					return 0;
				}

				break;
			}
			case ITEM_MORE_POWER:
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt)) {
					//패킷 보내기
					PLAYER_ITEM_BUFF_packet buff_packet;
					buff_packet.size = sizeof(buff_packet);
					buff_packet.type = ITEM_BUFF;
					buff_packet.item_type = ITEM_MORE_POWER;
					buff_packet.ix = ix;
					buff_packet.iy = iy;
					strcpy_s(buff_packet.id, clients[source_index]._id);
					if (clients[source_index]._power < 5) ++clients[source_index]._power;
					selectedMap[iy][ix] = EMPTY;
					for (auto& cl : clients) {
						if (cl.in_use == false) continue;
						if (cl._state != PLAY) continue;
						cl.Do_send(sizeof(buff_packet), &buff_packet);
					}
					return 0;
				}

				break;
			}
			case ITEM_ROCK:
			{
				RECT target_rt{ window_x + adj_obstacle_size_tl, window_y + adj_obstacle_size_tl, window_x + tile_size - adj_obstacle_size_br, window_y + tile_size - adj_obstacle_size_br };

				if (IntersectRect(&temp, &source_rt, &target_rt)) {
					//패킷 보내기
					PLAYER_ITEM_BUFF_packet buff_packet;
					buff_packet.size = sizeof(buff_packet);
					buff_packet.type = ITEM_BUFF;
					buff_packet.item_type = ITEM_ROCK;
					buff_packet.ix = ix;
					buff_packet.iy = iy;
					strcpy_s(buff_packet.id, clients[source_index]._id);
					if (clients[source_index]._rock_count < 5) ++clients[source_index]._rock_count;
					selectedMap[iy][ix] = EMPTY;
					for (auto& cl : clients) {
						if (cl.in_use == false) continue;
						if (cl._state != PLAY) continue;
						cl.Do_send(sizeof(buff_packet), &buff_packet);
					}
					return 0;
				}

				break;
			}
			default:
				break;
			}
		};

	return 0;	//충돌X
}

void Timer_Event(int _obj_id, EVENT_TYPE ev, std::chrono::milliseconds ms)
{
	timer_event t;
	t.obj_id = _obj_id;
	t.order = ev;
	t.start_time = chrono::system_clock::now() + ms;
	timer_queue.push(t);
}

void Process_packet(int client_index, char* p)
{
	Session& cl = clients[client_index];

	char packet_type = p[1];

	switch (packet_type) {

	case LOGIN: {
		LOGIN_packet* packet = reinterpret_cast<LOGIN_packet*>(p);


		if (!Get_status(client_index, packet->id)) {
			LOGIN_ERROR_packet login_error_packet;
			login_error_packet.type = LOGIN_ERROR;
			cl.Do_send(sizeof(login_error_packet), &login_error_packet);
			break;
		}

		for (auto& other : clients) {
			// 플레이어가 로그인 요청
			if (other._index == client_index) {
				LOGIN_OK_packet L_packet;
				L_packet.type = LOGIN_OK;
				L_packet.size = sizeof(packet);
				L_packet.x = cl._x;
				L_packet.y = cl._y;
				L_packet.level = cl._level;
				L_packet.index = cl._index;
				L_packet.exp = cl._exp;
				L_packet.map = map_num;
				cl.Do_send(sizeof(L_packet), &L_packet);

				continue;
			};
			if (NO_ACCEPT == other._state) continue;

			// 현재 접속한 플레이어에게 이미 접속해 있는 타 플레이어들의 정보 전송
			INIT_PLAYER_packet IN_Player;
			IN_Player.size = sizeof(INIT_PLAYER_packet);
			IN_Player.type = INIT_PLAYER;
			IN_Player.x = other._x;
			IN_Player.y = other._y;
			IN_Player.dir = other._dir;
			IN_Player.state = other._state;
			IN_Player.index = other._index;
			IN_Player.level = other._level;
			IN_Player.exp = other._exp;
			strcpy_s(IN_Player.id, other._id);
			cl.Do_send(sizeof(IN_Player), &IN_Player);

			// 이미 접속해 있는 플레이어들에게 현재 접속한 플레이어의 정보 전송
			INIT_PLAYER_packet IN_Other;
			IN_Other.size = sizeof(INIT_PLAYER_packet);
			IN_Other.type = INIT_PLAYER;
			IN_Other.x = cl._x;
			IN_Other.y = cl._y;
			IN_Other.dir = cl._dir;
			IN_Other.state = cl._state;
			IN_Other.index = cl._index;
			IN_Other.level = cl._level;
			IN_Other.exp = cl._exp;
			strcpy_s(IN_Other.id, cl._id);
			other.Do_send(sizeof(IN_Other), &IN_Other);

		}

		cout << "[수신 성공] \'" << cl._id << "\' (" << client_index + 1 << " 번째 플레이어) 로그인 요청" << endl;

		break;
	}

	case MOVE: {
		MOVE_PLAYER_packet* packet = reinterpret_cast<MOVE_PLAYER_packet*>(p);

		int x_bias{ 0 }, y_bias{ 0 };

		switch (packet->dir) {
		case 4: y_bias = pl_speed * (-1); break;
		case 3: y_bias = pl_speed * (+1); break;
		case 2: x_bias = pl_speed * (-1); break;
		case 1: x_bias = pl_speed * (+1); break;
		default:
			cout << "Invalid value recieved for move in client " << cl._id << endl;
			getchar();
			exit(-1);
		}

		cl._x += x_bias;
		cl._y += y_bias;
		cl._dir = packet->dir;

		//블록과 충돌체크
		if (Check_Collision(0, cl._index)) {
			cl._x -= x_bias;
			cl._y -= y_bias;
		}

		for (auto& pl : clients) {
			if (true == pl.in_use)
			{
				MOVE_OK_packet Move_Player;
				Move_Player.size = sizeof(Move_Player);
				Move_Player.type = MOVE_OK;
				Move_Player.x = cl._x;
				Move_Player.y = cl._y;
				Move_Player.dir = cl._dir;
				strcpy_s(Move_Player.id, cl._id);
				pl.Do_send(sizeof(Move_Player), &Move_Player);

			}
		}
		break;
	}

	case INIT_BOMB: {	//1. 폭탄 받음

		INIT_BOMB_packet* packet = reinterpret_cast<INIT_BOMB_packet*>(p);

		if (cl._state != PLAY) break;

		//cout << "플레이어: " << packet->owner_id << "     " << packet->x << ", " << packet->y << " 위치에 폭탄 설치" << endl;

		int bc = cl._current_bomb_count;
		auto [bomb_ix, bomb_iy] = WindowPosToMapIndex(packet->x, packet->y);
		if (bc >= cl._bomb_max_count) break;
		else {
			if (selectedMap[bomb_iy][bomb_ix] == EMPTY) ++cl._current_bomb_count;
			else break;
		}

		selectedMap[bomb_iy][bomb_ix] = BOMB;
		int obj_id = g_b_count;
		//2. 폭탄 큐에 넣음
		bombs.push_back(Bomb(packet->x, packet->y, obj_id, packet->power, packet->owner_id));

		packet->indx = obj_id;

		//3. 폭탄 생성 명령 모든 플레이어에게 보냄
		for (auto& pl : clients) {
			if (pl._state != PLAY) continue;
			if (true == pl.in_use)
			{
				pl.Do_send(sizeof(INIT_BOMB_packet), packet);
			}
		};

		//4. 타이머 큐에 3초짜리 타이머 넣음
		Timer_Event(cl._index, START_EXPL, 3000ms);
		//5. 3.5초 후에는 폭발 끝내는 타이머 넣음
		Timer_Event(cl._index, END_EXPL, 3500ms);

		//6. 타이머 이벤트를 signaled 상태로 전환
		SetEvent(htimerEvent);

		break;
	}

	case CHANGE_STATE: {
		PLAYER_CHANGE_STATE_packet* packet = reinterpret_cast<PLAYER_CHANGE_STATE_packet*>(p);
		switch (packet->state) {

		case READY: {
			cl._state = packet->state;
			cout << "클라이언트 \'" << cl._id << "\' - 준비 상태" << endl;

			if (Check_all_ready()) {
				Send_all_play_start();
				break;
			}

			for (auto& other : clients) {
				if (true == other.in_use) {
					if (strcmp(other._id, cl._id) != 0)
					{
						PLAYER_CHANGE_STATE_packet state_packet;
						state_packet.size = sizeof(state_packet);
						state_packet.type = CHANGE_STATE;
						state_packet.x = cl._x;
						state_packet.y = cl._y;
						state_packet.hp = cl._heart;
						state_packet.state = cl._state;
						state_packet.hp = cl._heart;
						strcpy_s(state_packet.id, cl._id);
						other.Do_send(sizeof(state_packet), &state_packet);
					}
					else
					{
						PLAYER_CHANGE_STATE_packet state_packet;
						state_packet.size = sizeof(state_packet);
						state_packet.type = CHANGE_STATE;
						state_packet.x = other._x;
						state_packet.y = other._y;
						state_packet.hp = other._heart;
						state_packet.state = other._state;
						state_packet.hp = cl._heart;
						strcpy_s(state_packet.id, other._id);
						cl.Do_send(sizeof(state_packet), &state_packet);
					}
				}
			}

			break;
		}

		case ACCEPT: {
			cl._state = packet->state;
			cout << "클라이언트 \'" << cl._id << "\' - 준비 취소 상태" << endl;

			for (auto& other : clients) {
				if (true == other.in_use) {
					if (strcmp(other._id, cl._id) != 0)
					{
						PLAYER_CHANGE_STATE_packet state_packet;
						state_packet.size = sizeof(state_packet);
						state_packet.type = CHANGE_STATE;
						state_packet.x = cl._x;
						state_packet.y = cl._y;
						state_packet.hp = cl._heart;
						state_packet.state = cl._state;
						state_packet.hp = cl._heart;
						strcpy_s(state_packet.id, cl._id);
						other.Do_send(sizeof(state_packet), &state_packet);
					}
					else
					{
						PLAYER_CHANGE_STATE_packet state_packet;
						state_packet.size = sizeof(state_packet);
						state_packet.type = CHANGE_STATE;
						state_packet.x = other._x;
						state_packet.y = other._y;
						state_packet.hp = other._heart;
						state_packet.state = other._state;
						state_packet.hp = cl._heart;
						strcpy_s(state_packet.id, other._id);
						cl.Do_send(sizeof(state_packet), &state_packet);
					}
				}
			}

			break;
		}


		default: {
			cout << "Invalid state in client: \'" << cl._id << "\'" << endl;
			cout << "packet state number: " << packet->state << endl;
			getchar();
			exit(-1);
			break;
		}

		}
		break;
	}

	case PRESS_SHIFT: {
		PRESS_SHIFT_packet* packet = reinterpret_cast<PRESS_SHIFT_packet*>(p);

		auto [cl_ix, cl_iy] = WindowPosToMapIndex(cl._x + 10, cl._y + 10);

		switch (cl._dir) {

		case UP:
		{
			if (cl_iy - 1 == -1) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//빈곳 여부
			if (selectedMap[cl_iy - 1][cl_ix] != EMPTY) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//플레이어 여부
			for (auto& pl : clients) {
				if (true == pl.in_use)
				{
					auto [pl_ix, pl_iy] = WindowPosToMapIndex(pl._x + 10, pl._y + 10);
					if (cl_ix == pl_ix && cl_iy - 1 == pl_iy) {
						SendCreateBlock(NULL, NULL, cl._id, FALSE);
						return;
					}
				}
			};

			SendCreateBlock(cl_ix, cl_iy - 1, cl._id, TRUE);
			selectedMap[cl_iy - 1][cl_ix] = ROCK;
			return;
		}

		case DOWN:
		{
			if (cl_iy + 1 == tile_max_h_num) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//빈곳 여부
			if (selectedMap[cl_iy + 1][cl_ix] != EMPTY) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//플레이어 여부
			for (auto& pl : clients) {
				if (true == pl.in_use)
				{
					auto [pl_ix, pl_iy] = WindowPosToMapIndex(pl._x + 10, pl._y + 10);
					if (cl_ix == pl_ix && cl_iy + 1 == pl_iy) {
						SendCreateBlock(NULL, NULL, cl._id, FALSE);
						return;
					}
				}
			};

			SendCreateBlock(cl_ix, cl_iy + 1, cl._id, TRUE);
			selectedMap[cl_iy + 1][cl_ix] = ROCK;
			return;
		}

		case LEFT:
		{
			if (cl_ix - 1 == -1) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//빈곳 여부
			if (selectedMap[cl_iy][cl_ix - 1] != EMPTY) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//플레이어 여부
			for (auto& pl : clients) {
				if (true == pl.in_use)
				{
					auto [pl_ix, pl_iy] = WindowPosToMapIndex(pl._x + 10, pl._y + 10);
					if (cl_ix - 1 == pl_ix && cl_iy == pl_iy) {
						SendCreateBlock(NULL, NULL, cl._id, FALSE);
						return;
					}
				}
			};

			SendCreateBlock(cl_ix - 1, cl_iy, cl._id, TRUE);
			selectedMap[cl_iy][cl_ix - 1] = ROCK;
			return;
		}

		case RIGHT:
		{
			if (cl_ix + 1 == tile_max_w_num) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//빈곳 여부
			if (selectedMap[cl_iy][cl_ix + 1] != EMPTY) {
				SendCreateBlock(NULL, NULL, cl._id, FALSE);
				return;
			}

			//플레이어 여부
			for (auto& pl : clients) {
				if (true == pl.in_use)
				{
					auto [pl_ix, pl_iy] = WindowPosToMapIndex(pl._x + 10, pl._y + 10);
					if (cl_ix + 1 == pl_ix && cl_iy == pl_iy) {
						SendCreateBlock(NULL, NULL, cl._id, FALSE);
						return;
					}
				}
			};

			SendCreateBlock(cl_ix + 1, cl_iy, cl._id, TRUE);
			selectedMap[cl_iy][cl_ix + 1] = ROCK;
			return;
		}

		case 0:		//처음 시작시 방향 0인 상태 (만약 이 상태에서 블록아이템 사용을 할 시, 그냥 무시) 
		{
			return;
		}

		default:
			cout << "Invalid dir value  in client: \'" << cl._id << "\'" << endl;
			cout << "client's dir: " << cl._dir << endl;
			getchar();
			exit(-1);
			return;
		}

		break;
	}


	default: {
		cout << "[에러] UnKnown Packet" << endl;
		Err_quit("UnKnown Packet");
		break;
	}

	}

}

int Get_new_index()
{
	static int g_id = 0;

	for (int i = 0; i < MAX_USER; ++i)
		if (false == clients[i].in_use) {
			clients[i].use_lock.lock();
			clients[i].in_use = true;
			clients[i].use_lock.unlock();
			return i;
		}

	return -1;
}

//연결종료
void Disconnect(int c_id)
{
	Session& cl = clients[c_id];
	clients[c_id]._state = NO_ACCEPT;
	Send_change_player(c_id);
	closesocket(clients[c_id]._cl);

	cout << "[플레이어 접속종료] \'" << clients[c_id]._id << "\' (" << c_id + 1 << " 번째 플레이어)" << endl;
}

DWORD WINAPI Thread(LPVOID arg)
{
	SOCKET client_sock = (SOCKET)arg;
	int index = Get_new_index();
	Session& player = clients[index];
	player._cl = client_sock;
	player._index = index;

	while (1) {
		// 데이터 받기
		player.Do_recv();
		
		if (clients[index].in_use == false)
		{
			Disconnect(index);
			return 0;
		}
		char* packet_start = clients[index]._recv_buf;
		char packet_size = packet_start[0];
		Process_packet(index, packet_start);
	
	}
}

std::pair<int, int> MapIndexToWindowPos(int ix, int iy)
{
	int window_x = ix * tile_size + outer_wall_start;
	int window_y = iy * tile_size + outer_wall_start;
	return std::make_pair(window_x, window_y);
}

std::pair<int, int> WindowPosToMapIndex(int x, int y)
{
	int map_x = (x - outer_wall_start) / tile_size;
	int map_y = (y - outer_wall_start) / tile_size;
	return std::make_pair(map_x, map_y);
}
