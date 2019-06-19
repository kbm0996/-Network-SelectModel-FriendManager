# 네트워크 프로그래밍 Select 모델 - 친구 관리 예제(+RapidJSON)
## 📢 개요
 친구 시스템은 여러 종류가 있다. 그 중 트위터처럼 신청 즉시 친구가 되는 대신 상대방의 정보를 볼 수 있을 뿐 상방향 컨텐츠는 별로 없는 팔로워 방식이 있고, 페이스북처럼 요청과 수락이라는 과정이 있으면서 쌍방향 컨텐츠가 있는 방식이 있다. 해당 예제는 소켓 프로그래밍의 셀렉트 모델을 이용하여 후자를 따라해본 것이다.

  ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/Capture.PNG)
  
  *\*figure 1. Server & Client & Dummy*
 
 서버 프로그램에서는 소켓 네트워크를 기반으로 클라이언트 간의 컨텐츠를 중계해주고 친구 관계에 대한 데이터를 JSON 형식으로 저장하여 관리한다. 그리고 간단하게 현재 접속자 수를 모니터링하는 기능을 제공한다.
 
 클라이언트 프로그램에서는 회원추가(회원가입), 로그인, 회원목록 조회, 친구목록 조회, 받은 친구 요청 목록 조회, 보낸 친구 요청 조회, 친구 요청 보내기, 친구 요청 취소, 친구 요청 수락, 친구 요청 거부, 친구 제거와 같은 컨텐츠 요청을 서버측으로 보낼 수 있다.
 
 더미 프로그램은 오직 서버의 스트레스 테스트를 위해 에코 패킷을 보내는 프로그램이다. 따라서 컨텐츠 테스트 기능은 따로 존재하지 않다. 

## 📐 친구 관계 테이블(구조체)
 ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/Table.PNG)
  
  *\*figure 2. Table*
  
1. 계정 ACCOUNT

        Account
          AccountNo	(64bit int)
          ID 		(WCHAR 20,  19글자까지)

2. 친구 FRIEND

        Friend
          FromAccountNo
          ToAccountNo
          Time

      A 과 B 가 친구라면 A↔B, B↔A 두 경우를 모두 저장한다. 관계를 끊을 때에도 둘 다 삭제한다.
  
 
3. 친구 요청 FRIEND_REQUESt

        FriendRequest

          FromAccountNo
          ToAccountNo
          Time



## 💡 프로토콜
1. 패킷 구조

  ![capture](https://github.com/kbm0996/-Network-SelectModel-FriendManager/blob/master/Friend_mail_Readme/PacketStructure.PNG)
  
  *\*figure 3. Packet Structure*
  
       struct st_PACKET_HEADER
       {
         BYTE	byCode;

         WORD	wMsgType;
         WORD	wPayloadSize;
       };


  
2. 메세지 타입 및 패이로드 데이터

- 회원가입 요청

       #define df_REQ_ACCOUNT_ADD				1
       WCHAR[df_NICK_MAX_LEN]	닉네임

- 회원가입 결과

       #define df_RES_ACCOUNT_ADD				2
       UINT64		AccountNo

- 회원로그인

       #define df_REQ_LOGIN					3
       UINT64		AccountNo

- 회원로그인 결과

       #define df_RES_LOGIN					4
       UINT64					AccountNo		// 0 이면 실패
       WCHAR[df_NICK_MAX_LEN]	NickName

- 회원리스트 요청

       #define df_REQ_ACCOUNT_LIST				10

- 회원리스트 결과

       #define df_RES_ACCOUNT_LIST				11
       UINT	Count		// 회원 수
       {
           UINT64					AccountNo
           WCHAR[df_NICK_MAX_LEN]	NickName
       }

- 친구목록 요청

       #define df_REQ_FRIEND_LIST				12

- 친구목록 결과

       #define df_RES_FRIEND_LIST				13
       UINT	FriendCount
       {
           UINT64					FriendAccountNo
           WCHAR[df_NICK_MAX_LEN]	NickName
       }	


- 친구요청 보낸 목록 요청

        #define df_REQ_FRIEND_REQUEST_LIST		14

- 친구목록 결과

        #define df_RES_FRIEND_REQUEST_LIST		15
        UINT	FriendCount
        {
            UINT64					FriendAccountNo
            WCHAR[df_NICK_MAX_LEN]	NickName
        }	

- 친구요청 받은거 목록  요청

        #define df_REQ_FRIEND_REPLY_LIST		16

- 친구목록 결과

        #define df_RES_FRIEND_REPLY_LIST		17
        UINT	FriendCount
        {
            UINT64					FriendAccountNo
            WCHAR[df_NICK_MAX_LEN]	NickName
        }	

- 친구관계 끊기

      #define df_REQ_FRIEND_REMOVE			20
      UINT64	FriendAccountNo
      
- 친구관계 끊기 결과

      #define df_RES_FRIEND_REMOVE			21
      UINT64	FriendAccountNo
      BYTE	Result

      #define df_RESULT_FRIEND_REMOVE_OK			1
      #define df_RESULT_FRIEND_REMOVE_NOTFRIEND	2
      #define df_RESULT_FRIEND_REMOVE_FAIL		3

- 친구요청

      #define df_REQ_FRIEND_REQUEST			22
      UINT64	FriendAccountNo

- 친구요청 결과

      #define df_RES_FRIEND_REQUEST			23
      UINT64	FriendAccountNo
      BYTE	Result

      #define df_RESULT_FRIEND_REQUEST_OK			1
      #define df_RESULT_FRIEND_REQUEST_NOTFOUND	2
      #define df_RESULT_FRIEND_REQUEST_AREADY		3


- 친구요청 취소

      #define df_REQ_FRIEND_CANCEL			24
      UINT64	FriendAccountNo

- 친구요청취소 결과

      #define df_RES_FRIEND_CANCEL			25
      UINT64	FriendAccountNo
      BYTE	Result

      #define df_RESULT_FRIEND_CANCEL_OK			1
      #define df_RESULT_FRIEND_CANCEL_NOTFRIEND	2
      #define df_RESULT_FRIEND_CANCEL_FAIL		3

- 친구요청 거부

      #define df_REQ_FRIEND_DENY				26
      UINT64	FriendAccountNo

- 친구요청 거부 결과

      #define df_RES_FRIEND_DENY				27
      UINT64	FriendAccountNo
      BYTE	Result

      #define df_RESULT_FRIEND_DENY_OK			1
      #define df_RESULT_FRIEND_DENY_NOTFRIEND		2
      #define df_RESULT_FRIEND_DENY_FAIL			3

- 친구요청 수락

      #define df_REQ_FRIEND_AGREE				28
      UINT64	FriendAccountNo

- 친구요청 수락 결과

      #define df_RES_FRIEND_AGREE				29
      UINT64	FriendAccountNo
      BYTE	Result

      #define df_RESULT_FRIEND_AGREE_OK			1
      #define df_RESULT_FRIEND_AGREE_NOTFRIEND	2
      #define df_RESULT_FRIEND_AGREE_FAIL			3

- 스트레스 테스트용 에코

      #define df_REQ_STRESS_ECHO				100
      WORD		Size
      Size		문자열 (WCHAR 유니코드)

- 스트레스 테스트용 에코응답

      #define df_RES_STRESS_ECHO				101
      WORD		Size
      Size		문자열 (WCHAR 유니코드)
