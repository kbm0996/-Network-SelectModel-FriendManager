# 네트워크 프로그래밍 Select 모델 - 친구 관리 예제(+RapidJSON)
## 📢 개요
 셀렉트(Select) 모델을 사용 하나의 서버에 다수의 플레이어가 참여할 수 있는 네트워크 프로그램. 각각의 클라이언트끼리는 친구 관계를 맺을 수 있다. 서버에서는 클라이언트 간의 친구 관계를 저장하고 불러오는 등 관리 역할을 한다.

  ![capture](https://github.com/kbm0996/Network-Programming-SelectModel/blob/master/run.gif)
  
  **figure 1. Star Control(animated)*
  
 네트워크 통신의 특성 상 패킷의 손실이나 각종 예기치 못한 일로 인해 서버와 각각의 클라이언트 간에 유저들의 위치 오차가 발생할 수 있다. 이는 추측 항법(推測航法, dead reckoning, dead reckoning navigation)이나 오차가 심해졌을 때 임의로 서버 측에서 정정하는 패킷을 보내서 해결해야만 한다.
 
 현재 이 프로그램에서는 단순히 셀렉트 모델의 동작 방식과 코드 구성을 알아보기 위한 간단한 소스코드이므로 그런 복잡한 코드가 없다. 하지만 실제로 온라인 서비스를 제공할 때에는 반드시 필요한 기능이다.

## 📌 동작 원리
 Select 모델은 사용하면 소켓 함수 호출이 성공할 수 있는 시점을 미리 알 수 있다. 따라서 소켓 함수 호출 시 조건이 만족되지 않아 생기는 문제를 해결할 수 있다. 소켓 모드에 따른 Select 모델 사용의 효과는 다음과 같다.

  · 블로킹 소켓 : 소켓 함수 호출 시 조건이 만족되지 않아 블로킹되는 상황을 막을 수 있다\
 
  · 넌블로킹 소켓 : 소켓 함수 호출 시 조건이 마족되지 않아 나중에 다시 호출해야 하는 상황을 막을 수 있다.

 다음 그림은 Select 모델의 동작 원리를 보여준다. Select 모델을 사용하려면 소켓 셋(socket set)을 준비해야 한다. 소켓 셋은 소켓 디스크립터의 집합을 의미하며, 호출할 함수의 종류에 따라 소켓을 적당한 셋에 넣어두어야 한다. 예를 들면, 어떤 소켓에 대해 recv() 함수를 호출하고 싶다면 읽기 셋에 넣고, send() 함수를 호출하고 싶다면 쓰기 셋에 넣으면 된다.
 
 ![capture](https://github.com/kbm0996/Network-Programming-SelectModel/blob/master/before%20calling%20select.JPG)
  
  **figure 2. Before select()*
  
  ![capture](https://github.com/kbm0996/Network-Programming-SelectModel/blob/master/after%20calling%20select.JPG)
  
  **figure 3. After select()*

>  소켓 셋을 세 개 준비하여 select() 함수를 호출하면(그림 9-3). select() 함수는 소켓 셋에 포함된 소켓이 입출력을 위한 준비가 될 떄까지 대기한다. 적어도 한 소켓이 준비되면 select(0 함수는 리턴한다(그림 9-4). 이때 소켓 셋에는 입출력이 가능한 소켓만 남고 나머지는 모두 제거된다. 그림 9-4는 두 소켓에 대해 읽기가 가능하고 한 소켓에 대해 쓰기가 가능함을 나타낸다. 따라서 이 세 소켓에 대해서는 소켓 함수를 호출하여 원하는 작업을 할 수 있다.
>
>  응용 프로그램은 소켓 셋을 통해 소켓 함수를 성공적으로 호출할 수 있는 지점을 알아낼 수 있고, 드물지만 소켓 함수의 호출 결과를 확인할 수도 있다. 다음은 소켓 셋의 역할을 정리한 것이다
  
   | 시점 | 읽기 셋(read set) |
   |:--------|:--------|
   | 함수 호출 시점	| - 클라이언트 접속 요청이 있으면 FD_ISSET()이 TRUE 리턴 → accept() 호출  <br/> - 소켓 수신 버퍼에 데이터가 있으면 TRUE 리턴 → recv(), recvfrom() 호출하여 데이터 읽기 <br/> - TCP 연결 종료시 TRUE 리턴 → recv(), recvfrom() 호출하여 연결 종료 감지	|

  **table 1. role of read set*

   | 시점 | 읽기 셋(write set) |
   |:--------|:--------|
   | 함수 호출 시점	| - 소켓 송신 버퍼의 여유 공간이 충분하면 send(), sendto()로 데이터 전송	| 
   | 함수 호출 결과 | - 넌블로킹 소켓을 사용한 connect() 함수 호출 성공	|

  **table 2. role of write set*
  
   | 시점 | 예외 셋(exception set) |
   |:--------|:--------|
   | 함수 호출 시점	| - OOB(Out-Of-Band) 데이터가 도착했으므로 recv(), recvfrom() 등의 함수를 <br/> 호출하여 OOB 데이터 받기 가능	| 
   | 함수 호출 결과 | - 넌블로킹 소켓을 사용한 connect() 함수 호출 실패	|

  **table 3. role of exception set*
  
- FD(File Descriptor; 파일 디스크립터) : 파일 또는 장치의 ID값. 중복되지 않으므로 unique 값으로 사용 가능


## 📌 주요 레퍼런스
### 1. int select(nfds, fd_set * readfds, fd_set * writefds, fd_set * exceptfds, const timeval * timeout);
* nfds : berkeley socket 호환성 (미사용)
* readfs : 읽기 가능 여부를 검사 할 소켓 세트
* writefds : 쓰기 가능 여부를 검사 할 소켓 세트
* timeout : select 함수 최대 대기 시간
* 리턴값 : 성공 시 준비된 fd 개수, 시간 만료 시 0, 오류 발생 시 -1

> 동기 I/O를 수행하기 위해 최대 FD_SETSIZE(default 값 64)개의 FD(File Descriptor)를 등록한다. FD_ISSET() 함수로 등록한 FD의 상태 변화를 감지할 수 있다

      FD_SET ReadSet;	 // FD_SET 구조체 : Select 모델에서 Socket들을 관리하는 구조체
      FD_ZERO(&ReadSet);	 // FD_ZERO() : FD_SET 구조체 초기화
      FD_SET(g_ListenSocket, &ReadSet);	// FD_SET() : FD_SET 구조체 내 SOCKET 배열에 g_ListenSocket 삽입
      for (int iCnt = 0; iCnt < df_USER_MAX; ++iCnt)
      {
        if (g_pPlayer[iCnt].iID != -1) // 접속이 된 유저
          FD_SET(g_pPlayer[iCnt].Socket, &ReadSet);
      }
      timeval Time;
      Time.tv_sec = 0; // 초
      Time.tv_usec = 0; // 마이크로초
      select(0, &ReadSet, NULL, NULL, &Time);

### 2. FD_ISSET(fd, *fdset);
* fd : 확인할 소켓
* fdset : 검사 할 소켓 세트
* 리턴값 : fd의 비트가 fdset 내에서 세트로 설정되어 있으면 0이 아닌 값, 그렇지 않으면 0


      // 서버 Accept 처리
      if (FD_ISSET(g_ListenSocket, &ReadSet))
       ProcAccept();
      // 클라이언트 수신 패킷 처리
      for (int iCnt = 0; iCnt < df_USER_MAX; ++iCnt)
      {
       if (FD_ISSET(g_pPlayer[iCnt].Socket, &ReadSet))
        ProcRecv(&g_pPlayer[iCnt]);
      }

