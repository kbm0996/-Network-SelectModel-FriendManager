#include "NetworkProcess.h"
#include "CLFMemoryPool.h"
#include <ctime>
#include <cstdio>

#define dfBUFFSIZE 1024

using namespace mylib;
using namespace std;

map<SOCKET, st_CLIENT*>			g_ClientMap;
map<UINT64, st_ACCOUNT*>		g_AccountMap;
//////////////////////////////////////////////
//  multimap을 사용해야 하는 경우
// - 저장된 데이터들을 정렬해야 할 경우
// - 많은 자료들을 저장하고, 특정 자료에 대해 검색을 빠르게 해야 할 경우
// - map과 동일하나, key 값이 중복된 원소를 가질 수 있는 경우
//////////////////////////////////////////////
multimap<UINT64, st_FRIEND*>			g_FriendMap;
multimap<UINT64, st_FRIEND_REQUEST*>	g_FriendReqMap;
multimap<UINT64, UINT64>				g_FriendReqIndex_To;
multimap<UINT64, UINT64>				g_FriendReqIndex_From;

SOCKET		g_ListenSock = INVALID_SOCKET;
UINT64		g_Connect;

UINT64		g_ClientNo;
UINT64		g_AccountNo = 1;
UINT64		g_FriendDataNo;
UINT64		g_FriendReqDataNo;


bool NetworkInit()
{
	WSADATA		wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	g_ListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_ListenSock == INVALID_SOCKET)
	{
		int err = WSAGetLastError();
		return false;
	}

	BOOL bOptval = TRUE;
	int iResult = setsockopt(g_ListenSock, IPPROTO_TCP, TCP_NODELAY, (char *)&bOptval, sizeof(bOptval));
	if (iResult == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		return false;
	}

	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	InetPton(AF_INET, INADDR_ANY, &serveraddr.sin_addr);
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(df_NETWORK_PORT);

	iResult = bind(g_ListenSock, (SOCKADDR *)&serveraddr, sizeof(serveraddr));
	if (iResult == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		return false;
	}

	iResult = listen(g_ListenSock, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		return false;
	}

	LoadData();

	timeBeginPeriod(1);
	wprintf(L"Server Open...\n");
	return true;
}

void NetworkClean()
{
	closesocket(g_ListenSock);
	WSACleanup();
	timeEndPeriod(1);

	// Map 정리
	SaveData();
}

void NetworkProcess()
{
	SOCKET	UserTable_SOCKET[FD_SETSIZE];	// FD_SET에 등록된 소켓 저장
	int		iSockCnt = 0;
	//-----------------------------------------------------
	// FD_SET은 FD_SETSIZE만큼씩만 소켓 검사 가능
	//-----------------------------------------------------
	FD_SET ReadSet;
	FD_SET WriteSet;
	FD_ZERO(&ReadSet);
	FD_ZERO(&WriteSet);
	memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) *FD_SETSIZE);

	// Listen Socket Setting
	FD_SET(g_ListenSock, &ReadSet);
	UserTable_SOCKET[iSockCnt] = g_ListenSock;
	++iSockCnt;
	//-----------------------------------------------------
	// ListenSocket 및 모든 클라이언트에 대해 Socket 검사
	//-----------------------------------------------------
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end();)
	{
		st_CLIENT * pClient = iter->second;
		++iter;	// SelectSocket 함수 내부에서 ClientMap을 삭제하는 경우가 있어서 미리 증가
		//-----------------------------------------------------
		// 해당 클라이언트 ReadSet 등록
		// SendQ에 데이터가 있다면 WriteSet 등록
		//-----------------------------------------------------
		UserTable_SOCKET[iSockCnt] = pClient->Sock;

		// ReadSet 등록
		FD_SET(pClient->Sock, &ReadSet);

		// WriteSet 등록
		if (pClient->SendQ.GetUseSize() > 0)
			FD_SET(pClient->Sock, &WriteSet);

		++iSockCnt;
		//-----------------------------------------------------
		// select 최대치 도달, 만들어진 테이블 정보로 select 호출 후 정리
		//-----------------------------------------------------
		if (FD_SETSIZE <= iSockCnt)
		{
			SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet, iSockCnt);

			FD_ZERO(&ReadSet);
			FD_ZERO(&WriteSet);

			memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
			//---------------------------------------------------------
			//  단일 스레드에 select 방식의 경우 소켓이 천 개 이상으로 많아지고
			// 서버의 로직도 부하가 걸리는 경우 접속자 츠리가 엄청 느려진다.
			//  따라서 64개씩 select 처리를 할 때 매번 accept 처리를 해주도록 한다.
			// 접속자 처리가 조금은 더 원활하게 진행된다.
			//---------------------------------------------------------
			FD_SET(g_ListenSock, &ReadSet);
			UserTable_SOCKET[0] = g_ListenSock;

			iSockCnt = 1;
		}
	}
	//-----------------------------------------------------
	// 전체 클라이언트 for 문 종료 후 iSockCnt 수치가 있다면
	// 추가적으로 마지막 Select 호출을 해준다
	//-----------------------------------------------------
	if (iSockCnt > 0)
	{
		SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet, iSockCnt);
	}
}

void SelectSocket(SOCKET* pTableSocket, FD_SET *pReadSet, FD_SET *pWriteSet, int iSockCnt)
{
	////////////////////////////////////////////////////////////////////
	// select() 함수의 디폴트 한계 값인 FD_SETSIZE(기본 64소켓)을 넘지 않았다면 
	// TimeOut 시간(Select 대기 시간)을 0으로 만들 필요가 없다.
	//
	// 그러나, FD_SETSIZE(기본 64소켓)을 넘었다면 Select를 두 번 이상 해야한다.
	// 이 경우 TimeOut 시간을 0으로 해야하는데 
	// 그 이유는 첫번째 select에서 블럭이 걸려 두번째 select가 반응하지 않기 떄문이다.
	////////////////////////////////////////////////////////////////////
	//-----------------------------------------------------
	// select 함수의 대기시간 설정
	//-----------------------------------------------------
	timeval Time;
	Time.tv_sec = 0;
	Time.tv_usec = 0;

	//-----------------------------------------------------
	// 접속자 요청과 현재 접속중인 클라이언트들의 메시지 송신 검사
	//-----------------------------------------------------
	//  select() 함수에서 block 상태에 있다가 요청이 들어오면,
	// select() 함수는 몇대에서 요청이 들어왔는지 감지를 하고 그 개수를 return합니다.
	// select() 함수가 return 되는 순간 flag를 든 소켓만 WriteSet, ReadSet에 남아있게 됩니다.
	// 그리고 각각 소켓 셋으로 부터 flag 표시를 한 소켓들을 checking하는 것입니다.
	int iResult = select(0, pReadSet, pWriteSet, NULL, &Time);
	if (iResult == SOCKET_ERROR)
	{
		wprintf(L"!! select socket error \n");
	}

	if (iResult > 0)
	{
		for (int iCnt = 0; iCnt < iSockCnt; ++iCnt)
			//< FD_SETSIZE; ++iCnt)
		{
			if (pTableSocket[iCnt] == INVALID_SOCKET)
				continue;

			if (FD_ISSET(pTableSocket[iCnt], pWriteSet))
			{
				ProcSend(pTableSocket[iCnt]);
			}

			if (FD_ISSET(pTableSocket[iCnt], pReadSet))
			{
				//-----------------------------------------------------
				// ListenSocket은 접속자 수락 용도이므로 별도 처리 
				//-----------------------------------------------------
				if (pTableSocket[iCnt] == g_ListenSock)
				{
					ProcAccept();
				}
				else
				{
					ProcRecv(pTableSocket[iCnt]);
				}
			}
		}
	}
}

void ProcAccept()
{
	SOCKADDR_IN clientaddr;
	SOCKET client_sock;
	int addrlen = sizeof(clientaddr);
	client_sock = accept(g_ListenSock, (SOCKADDR *)&clientaddr, &addrlen);
	if (client_sock == INVALID_SOCKET)
	{
		int err = WSAGetLastError();
		return;
	}

	/////////////////////////////////////////////////////////////////////////
	st_CLIENT* stNewClient = new st_CLIENT();
	stNewClient->AccountNo = 0;
	stNewClient->ConnectAddr = clientaddr;
	stNewClient->Sock = client_sock;
	g_ClientMap.insert(pair<SOCKET, st_CLIENT*>(client_sock, stNewClient));
	/////////////////////////////////////////////////////////////////////////

	WCHAR szClientIP[16] = { 0 };
	DWORD dwAddrBufSize = sizeof(szClientIP);
	WSAAddressToString((SOCKADDR*)&clientaddr, sizeof(SOCKADDR), NULL, szClientIP, &dwAddrBufSize);
	wprintf(L"Accept - %s [Socket:%lld]\n", szClientIP, client_sock);

	++g_Connect;
}

void ProcRecv(SOCKET Sock)
{
	st_CLIENT *pClient;
	char RecvBuff[dfBUFFSIZE];
	int iResult, err;

	// Find Current Client
	pClient = Find_Client(Sock);
	if (pClient == NULL)
		return;

	// Recv
	iResult = recv(pClient->Sock, RecvBuff, dfBUFFSIZE, 0);
	// 받다가 에러 발생시 종료
	if (iResult == SOCKET_ERROR || iResult == 0)
	{
		err = WSAGetLastError();
		Disconnect(Sock);
		return;
	}

	// 받은 데이터가 있다면
	if (iResult > 0)
	{
		pClient->RecvQ.Enqueue(RecvBuff, iResult);
		//-----------------------------------------------------
		// 패킷 확인
		// * 패킷이 하나 이상 버퍼에 있을 수 있으므로 반복문으로 한 번에 전부 처리해야함
		//-----------------------------------------------------
		while (1)
		{
			iResult = CompletePacket(pClient);

			// 처리할 패킷이 없음
			if (iResult == 1)
				break;
			// 패킷 처리 오류
			if (iResult == -1)
			{
				wprintf(L"!!!! Packet Error - UserID:%lld \n", Sock);
				return;
			}
		}
	}
	return;
}

bool ProcSend(SOCKET Sock)
{
	st_CLIENT *pClient;
	int iResult, err;
	char SendBuff[dfBUFFSIZE];
	int iSendSize;

	// Find Current Client
	pClient = Find_Client(Sock);
	if (pClient == NULL)
		return FALSE;



	// SendQ에 있는 데이터들을 최대 dfNETWORK_WSABUFF_SIZE 크기로 보낸다
	iSendSize = pClient->SendQ.GetUseSize();
	iSendSize = min(dfBUFFSIZE, iSendSize);

	if (iSendSize <= 0)
		return FALSE;

	// Peek으로 데이터 뽑음. 전송이 제대로 마무리됐을 경우 삭제
	pClient->SendQ.Peek(SendBuff, iSendSize);

	// Send
	iResult = send(pClient->Sock, SendBuff, iSendSize, 0);
	if (iResult == SOCKET_ERROR)
	{
		err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			wprintf(L"////// Socket WOULDBLOCK - Socket:%lld //////\n", Sock);
			return FALSE;
		}
		wprintf(L"!!!!! Socket Error - Socket:%lld ErrorCode:%d\n", Sock, err);
		Disconnect(Sock);
		return FALSE;
	}
	else
	{
		if (iResult > iSendSize)
		{
			//-----------------------------------------------------
			// 보낼 사이즈보다 더 크다면 오류
			// 생기면 안되는 상황이지만 가끔 이런 경우가 생길 수 있다
			//-----------------------------------------------------
			wprintf(L"!!!!!! Send Size Error - Socket:%lld SendSize:%d SendResult:%d \n", Sock, iSendSize, iResult);
			return FALSE;
		}
		else
		{
			//-----------------------------------------------------
			// Send Complate
			// 패킷 전송이 완료됐다는 의미는 아님. 소켓 버퍼에 복사를 완료했다는 의미
			// 송신 큐에서 Peek으로 빼냈던 데이터 제거
			//-----------------------------------------------------
			pClient->SendQ.MoveReadPos(iResult);
		}
	}
	return TRUE;
}

void SendUnicast(st_CLIENT * pClient, st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket)
{
	if (pClient == NULL)
	{
		wprintf(L"SendUnicast Client is NULL \n");
		return;
	}
	pClient->SendQ.Enqueue((char*)pHeader, sizeof(st_PACKET_HEADER));
	pClient->SendQ.Enqueue((char*)pPacket->GetBufferPtr(), pPacket->GetUseSize());
}

st_CLIENT * Find_Client(SOCKET sock)
{
	map<SOCKET, st_CLIENT*>::iterator iter = g_ClientMap.find(sock);
	if (g_ClientMap.end() == iter)
		return NULL;
	return iter->second;
}

st_ACCOUNT * Find_Account(UINT64 accountno)
{
	map<UINT64, st_ACCOUNT*>::iterator iter = g_AccountMap.find(accountno);
	if (g_AccountMap.end() == iter)
		return NULL;
	return iter->second;
}

BYTE Find_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo)
{
	// 이미 친구인 경우
	pair<multimap<UINT64, st_FRIEND*>::iterator, multimap<UINT64, st_FRIEND*>::iterator> range = g_FriendMap.equal_range(FromAccountNo);
	for (auto iter = range.first; iter != range.second; ++iter)
	{
		if ((iter->first == FromAccountNo && iter->second->ToAccountNo == ToAccountNo) ||
			(iter->first == ToAccountNo && iter->second->ToAccountNo == FromAccountNo))
		{
			return df_RESULT_FRIEND_REQUEST_NOTFOUND;
		}
	}

	// 이미 요청했을 경우
	pair<multimap<UINT64, st_FRIEND_REQUEST*>::iterator, multimap<UINT64, st_FRIEND_REQUEST*>::iterator> range_req = g_FriendReqMap.equal_range(FromAccountNo);
	for (auto iter = range_req.first; iter != range_req.second; ++iter)
	{
		if (iter->first == FromAccountNo && iter->second->ToAccountNo == ToAccountNo)
		{
			return df_RESULT_FRIEND_REQUEST_AREADY;
		}
	}

	return df_RESULT_FRIEND_REQUEST_OK;
}

st_ACCOUNT * Add_Account(WCHAR * szName, UINT64 uiAccountNo)
{
	//// 이미 존재하는 닉네임인가
	//for (map<UINT64, st_ACCOUNT*>::iterator iter = g_AccountMap.begin(); iter != g_AccountMap.end(); ++iter)
	//{
	//	if (wcscmp((*iter).second->Nickname, szName) == 0)
	//	{
	//		return NULL;
	//	}
	//}

	// JSON 데이터 때문에 내부에서 증가
	++g_AccountNo;

	st_ACCOUNT* pNewAccount = new st_ACCOUNT();
	pNewAccount->AccountNo = uiAccountNo;
	wcscpy_s(pNewAccount->Nickname, szName);
	g_AccountMap.insert(pair<UINT64, st_ACCOUNT*>(pNewAccount->AccountNo, pNewAccount));
	return pNewAccount;
}

void Add_Friend(UINT64 FromAccountNo, UINT64 ToAccountNo)
{
	st_FRIEND* pNewFriend = new st_FRIEND();
	pNewFriend->FromAccountNo = FromAccountNo;
	pNewFriend->ToAccountNo = ToAccountNo;

	g_FriendMap.emplace(FromAccountNo, pNewFriend);
}

UINT64 Delete_Friend(UINT64 FromAccountNo, UINT64 ToAccountNo)
{
	int deleteCount = 0;
	for (auto iter = g_FriendMap.begin(); iter != g_FriendMap.end();)
	{
		if ((iter->first == FromAccountNo && iter->second->ToAccountNo == ToAccountNo) ||
			(iter->first == ToAccountNo && iter->second->ToAccountNo == FromAccountNo))
		{
			deleteCount++;
			st_FRIEND* pTemp = iter->second;
			iter = g_FriendMap.erase(iter);
			delete pTemp;

			if (deleteCount == 2)
				break;
		}
		iter++;
	}
	return deleteCount;
}

UINT64 Add_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo)
{
	// 본인이 본인에게 요청할 수 없음
	if (FromAccountNo == ToAccountNo || Find_Account(ToAccountNo) == NULL || Find_Account(FromAccountNo) == NULL)
		return df_RESULT_FRIEND_REQUEST_NOTFOUND;

	// g_FriendReqMap, g_RequestFrom, g_RequestTo 추가해야 된다.
	int iResult = Find_FriendReq(FromAccountNo, ToAccountNo);
	if (iResult != df_RESULT_FRIEND_REQUEST_OK)
		return iResult;

	// 추가하기전에 To -> From 방향으로 친구가 들어온건지 확인한다.
	if (Find_FriendReq(ToAccountNo, FromAccountNo) == df_RESULT_FRIEND_REQUEST_AREADY)
	{
		// 이 경우 To, From 방향을 지우고 친구로 추가한다.
		Delete_FriendReq(ToAccountNo, FromAccountNo);
		Add_Friend(FromAccountNo, ToAccountNo);
		Add_Friend(ToAccountNo, FromAccountNo);
		return df_RESULT_FRIEND_REQUEST_OK;
	}

	st_FRIEND_REQUEST *pRequest = new st_FRIEND_REQUEST;
	pRequest->FromAccountNo = FromAccountNo;
	pRequest->ToAccountNo = ToAccountNo;

	g_FriendReqMap.emplace(FromAccountNo, pRequest);
	g_FriendReqIndex_From.emplace(FromAccountNo, ToAccountNo);
	g_FriendReqIndex_To.emplace(ToAccountNo, FromAccountNo);

	return df_RESULT_FRIEND_REQUEST_OK;
}

UINT64 Delete_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo)
{
	// 친구 요청 목록에 해당 유저가 있는가
	auto iter = g_FriendReqMap.begin();

	pair<multimap<UINT64, st_FRIEND_REQUEST *>::iterator,
		multimap<UINT64, st_FRIEND_REQUEST *>::iterator> Allrange;

	Allrange = g_FriendReqMap.equal_range(FromAccountNo);
	for (iter = Allrange.first; iter != Allrange.second;)
	{
		if ((iter->first == FromAccountNo && iter->second->ToAccountNo == ToAccountNo))
		{
			iter = g_FriendReqMap.erase(iter);
			break;
		}
		iter++;
	}

	// 인덱스에서도 삭제
	pair<multimap<UINT64, UINT64>::iterator,
		multimap<UINT64, UINT64>::iterator> range;

	range = g_FriendReqIndex_From.equal_range(FromAccountNo);

	int deleteCount = 0;

	for (auto iterFrom = range.first; iterFrom != range.second;)
	{
		if ((iterFrom->first == FromAccountNo && iterFrom->second == ToAccountNo))
		{
			deleteCount++;
			iterFrom = g_FriendReqIndex_From.erase(iterFrom);
			break;
		}
		iterFrom++;
	}

	pair<multimap<UINT64, UINT64>::iterator,
		multimap<UINT64, UINT64>::iterator> rangeTo;
	rangeTo = g_FriendReqIndex_To.equal_range(ToAccountNo);

	for (auto iterTo = rangeTo.first; iterTo != rangeTo.second;)
	{
		if ((iterTo->first == ToAccountNo && iterTo->second == FromAccountNo))
		{
			deleteCount++;
			iterTo = g_FriendReqIndex_To.erase(iterTo);
			break;
		}
		iterTo++;
	}

	return deleteCount;
}

void Disconnect(SOCKET sock)
{
	st_CLIENT*	pClient = Find_Client(sock);
	if (pClient == NULL)
		return;

	g_ClientMap.erase(sock);

	WCHAR szClientIP[16] = { 0 };
	DWORD dwAddrBufSize = sizeof(szClientIP);
	WSAAddressToString((SOCKADDR*)&pClient->ConnectAddr, sizeof(SOCKADDR), NULL, szClientIP, &dwAddrBufSize);
	wprintf(L"Disconnect - %s [UserNo:%lld][Socket:%lld]\n", szClientIP, pClient->AccountNo, pClient->Sock);

	closesocket(pClient->Sock);
	delete pClient;
	--g_Connect;
}

bool PacketProc(st_CLIENT * client, WORD wType, CSerialBuffer * Packet)
{
	switch (wType)
	{
	case df_REQ_ACCOUNT_ADD:
		return PacketProc_Account_Add(client, Packet);
		break;
	case df_REQ_LOGIN:
		return PacketProc_Login(client, Packet);
		break;
	case df_REQ_ACCOUNT_LIST:
		return PacketProc_Account_List(client, Packet);
		break;
	case df_REQ_FRIEND_LIST:
		return PacketProc_Friend_List(client, Packet);
		break;
	case df_REQ_FRIEND_REQUEST_LIST:
		return PacketProc_Friend_Request_List(client, Packet);
		break;
	case df_REQ_FRIEND_REPLY_LIST:
		return PacketProc_Friend_Reply_List(client, Packet);
		break;
	case df_REQ_FRIEND_REMOVE:
		return PacketProc_Friend_Remove(client, Packet);
		break;
	case df_REQ_FRIEND_REQUEST:
		return PacketProc_Friend_Request(client, Packet);
		break;
	case df_REQ_FRIEND_CANCEL:
		return PacketProc_Friend_Cancel(client, Packet);
		break;
	case df_REQ_FRIEND_DENY:
		return PacketProc_Friend_Deny(client, Packet);
		break;
	case df_REQ_FRIEND_AGREE:
		return PacketProc_Friend_Agree(client, Packet);
		break;
	case df_REQ_STRESS_ECHO:
		return PacketProc_Friend_Echo(client, Packet);
		break;
	}
	return TRUE;
}

bool PacketProc_Account_Add(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	WCHAR	szID[df_NICK_MAX_LEN] = { 0, };
	//------------------------------------------------------------
	// 회원가입 요청
	//
	// {
	//		WCHAR[df_NICK_MAX_LEN]	닉네임
	// }
	//------------------------------------------------------------
	Packet->Dequeue((char*)szID, df_NICK_MAX_LEN);

	st_ACCOUNT* stNewAccount = Add_Account(szID, g_AccountNo);
	//if (stNewAccount == NULL)
	//{
	//	닉네임 중복 처리
	//}

	//------------------------------------------------------------
	// 회원가입 결과
	//
	// {
	//		UINT64		AccountNo
	// }
	//------------------------------------------------------------
	mpResAccountAdd(&stPacketHeader, Packet, stNewAccount->AccountNo);
	SendUnicast(client, &stPacketHeader, Packet);

	return TRUE;
}

bool PacketProc_Login(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 AccountNo;
	WCHAR ID[df_NICK_MAX_LEN] = { 0, };
	//------------------------------------------------------------
	// 회원로그인
	//
	// {
	//		UINT64		AccountNo
	// }
	//------------------------------------------------------------
	// 256 이상의 글자인 경우 예외처리
	*Packet >> AccountNo;
	map<UINT64, st_ACCOUNT*>::iterator iter = g_AccountMap.find(AccountNo);
	if (iter == g_AccountMap.end())
	{
		client->AccountNo = 0;
	}
	else
	{
		client->AccountNo = AccountNo;
		wcscpy_s(ID, (*iter).second->Nickname);
	}

	//------------------------------------------------------------
	// 회원로그인 결과
	//
	// {
	//		UINT64					AccountNo		// 0 이면 실패
	//		WCHAR[df_NICK_MAX_LEN]	ID
	// }
	//------------------------------------------------------------
	mpResLogin(&stPacketHeader, Packet, client->AccountNo, ID);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Account_List(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	//------------------------------------------------------------
	// 회원리스트 요청
	//
	// {
	//		없음.
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// 회원리스트 결과
	//
	// {
	//		UINT	Count		// 회원 수
	//		{
	//			UINT64					AccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}
	// }
	//------------------------------------------------------------
	mpResAccountList(&stPacketHeader, Packet, client->AccountNo);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_List(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	//------------------------------------------------------------
	// 친구목록 요청
	//
	// {
	//		없음
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// 친구목록 결과
	//
	// { 
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	mpResFriendList(&stPacketHeader, Packet, client->AccountNo);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Request_List(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	//------------------------------------------------------------
	// 친구요청 보낸 목록  요청
	//
	// {
	//		없음
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// 친구목록 결과
	//
	// {
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	mpResFriendRequestList(&stPacketHeader, Packet, client->AccountNo);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Reply_List(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	//------------------------------------------------------------
	// 친구요청 받은거 목록  요청
	//
	// {
	//		없음
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// 친구목록 결과
	//
	// {
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	mpResFriendReplyList(&stPacketHeader, Packet, client->AccountNo);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Remove(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 FriendAccountNo;
	//------------------------------------------------------------
	// 친구관계 끊기
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;
	BYTE Result = Delete_Friend(client->AccountNo, FriendAccountNo);
	if (Result == 2)
		Result = df_RESULT_FRIEND_REMOVE_OK;
	else
		Result = df_RESULT_FRIEND_REMOVE_NOTFRIEND;

	//------------------------------------------------------------
	// 친구관계 끊기 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	mpResFriendRemove(&stPacketHeader, Packet, FriendAccountNo, Result);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Request(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 FriendAccountNo = 0;
	BYTE Result = df_RESULT_FRIEND_REQUEST_NOTFOUND;
	//------------------------------------------------------------
	// 친구요청
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;

	Result = Add_FriendReq(client->AccountNo, FriendAccountNo);

	//------------------------------------------------------------
	// 친구요청 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	mpResFriendRequest(&stPacketHeader, Packet, FriendAccountNo, Result);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Cancel(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 FriendAccountNo;
	//------------------------------------------------------------
	// 친구요청 취소
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;

	BYTE Result = Delete_FriendReq(client->AccountNo, FriendAccountNo);
	if (Result == 0)
		Result = df_RESULT_FRIEND_CANCEL_NOTFRIEND;
	else if (Result == 2)
		Result = df_RESULT_FRIEND_CANCEL_OK;
	else
		Result = df_RESULT_FRIEND_CANCEL_FAIL;

	//------------------------------------------------------------
	// 친구요청취소 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	mpResFriendCancel(&stPacketHeader, Packet, FriendAccountNo, Result);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Deny(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 FriendAccountNo;
	//------------------------------------------------------------
	// 친구요청 거부
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;

	BYTE Result = Delete_FriendReq(FriendAccountNo, client->AccountNo);
	if (Result == 0)
		Result = df_RESULT_FRIEND_DENY_NOTFRIEND;
	else if (Result == 2)
		Result = df_RESULT_FRIEND_DENY_OK;
	else
		Result = df_RESULT_FRIEND_DENY_FAIL;

	//------------------------------------------------------------
	// 친구요청 거부 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	mpResFriendDeny(&stPacketHeader, Packet, FriendAccountNo, Result);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Agree(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	UINT64 FriendAccountNo;
	//------------------------------------------------------------
	// 친구요청 수락
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;

	BYTE Result = Delete_FriendReq(FriendAccountNo, client->AccountNo);
	if (Result > 0)
	{
		Result = df_RESULT_FRIEND_AGREE_OK;
		Add_Friend(client->AccountNo, FriendAccountNo);
		Add_Friend(FriendAccountNo, client->AccountNo);
	}
	else
		Result = df_RESULT_FRIEND_AGREE_NOTFRIEND;

	//------------------------------------------------------------
	// 친구요청 수락 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	mpResFriendAgree(&stPacketHeader, Packet, FriendAccountNo, Result);
	SendUnicast(client, &stPacketHeader, Packet);
	return TRUE;
}

bool PacketProc_Friend_Echo(st_CLIENT * client, CSerialBuffer * Packet)
{
	st_PACKET_HEADER stPacketHeader;
	WORD Size;
	WCHAR szMsg[1000];
	//------------------------------------------------------------
	// 스트레스 테스트용 에코
	//
	// {
	//			WORD		Size
	//			Size		문자열 (WCHAR 유니코드)
	// }
	//------------------------------------------------------------
	*Packet >> Size;
	Packet->Dequeue((char*)szMsg, Size);

	//------------------------------------------------------------
	// 스트레스 테스트용 에코응답
	//
	// {
	//			WORD		Size
	//			Size		문자열 (WCHAR 유니코드)
	// }
	//------------------------------------------------------------
	mpResFriendEcho(&stPacketHeader, Packet, Size, szMsg);
	SendUnicast(client, &stPacketHeader, Packet);

	return TRUE;
}

int CompletePacket(st_CLIENT * pClient)
{
	st_PACKET_HEADER stPacketHeader;
	int iRecvQSize = pClient->RecvQ.GetUseSize();
	//-----------------------------------------------------
	// 받은 내용 검사
	// 패킷헤더 크기 이상으로 뭔가 받았다면 스킵
	//-----------------------------------------------------
	if (iRecvQSize < sizeof(st_PACKET_HEADER)) // 처리할 패킷없음
		return 1;
	//-----------------------------------------------------
	// I. PacketCode 검사
	// Peek으로 검사를 하는 이유는 헤더를 얻은 후 사이즈 비교 후 하나의 오나성된 패킷만큼의 데이터가 있는지 확인해
	// 패킷을 마저 얻을지 그냥 중단할지 결정
	// Get으로 얻는 경우는 검사 후 사이즈가 안맞다면 헤더를 다시 큐의 제자리에 넣어야 하는데 FIFO 이므로 어려움
	//-----------------------------------------------------
	pClient->RecvQ.Peek((char*)&stPacketHeader, sizeof(st_PACKET_HEADER));

	if (stPacketHeader.byCode != df_PACKET_CODE)
		return -1;

	//-----------------------------------------------------
	// II. 큐에 저장된 데이터가 얻고자하는 패킷의 크기만큼 있는가
	//-----------------------------------------------------
	if ((WORD)iRecvQSize <  stPacketHeader.wPayloadSize + sizeof(st_PACKET_HEADER))
	{
		// 사이즈가 작다면, 패킷이 아직 완료되지 않았으므로 다음에 다시 처리
		return 1;	// 더 이상 처리할 패킷이 없음
	}
	pClient->RecvQ.MoveReadPos(sizeof(st_PACKET_HEADER));

	CSerialBuffer pPacket;
	// Payload 부분 패킷 버퍼로 복사
	if (stPacketHeader.wPayloadSize != pClient->RecvQ.Dequeue(pPacket.GetBufferPtr(), stPacketHeader.wPayloadSize))
		return -1;
	pPacket.MoveWritePos(stPacketHeader.wPayloadSize);

	// 실질적인 패킷 처리 함수 호출
	if (!PacketProc(pClient, stPacketHeader.wMsgType, &pPacket))
		return -1;
	return 0;	// 패킷 1개 처리 완료, 본 함수 호출부에서 Loop 유도
}

void mpResAccountAdd(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// 회원가입 결과
	//
	// {
	//		UINT64		AccountNo
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << AccountNo;
	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_ACCOUNT_ADD;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResLogin(st_PACKET_HEADER * pHeader, CSerialBuffer *pPacket, UINT64 AccountNo, WCHAR* pID)
{
	//------------------------------------------------------------
	// 회원로그인 결과
	//
	// {
	//		UINT64					AccountNo		// 0 이면 실패
	//		WCHAR[df_NICK_MAX_LEN]	ID
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << AccountNo;
	pPacket->Enqueue((char*)pID, sizeof(WCHAR) * df_NICK_MAX_LEN);
	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_LOGIN;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResAccountList(st_PACKET_HEADER * pHeader, CSerialBuffer *pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// 회원리스트 결과
	//
	// {
	//		UINT	Count		// 회원 수
	//		{
	//			UINT64					AccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}
	// }
	//------------------------------------------------------------;
	pPacket->Clear();
	if (AccountNo != 0)
	{
		*pPacket << (UINT)g_AccountMap.size();
		for (map<UINT64, st_ACCOUNT*>::iterator iter = g_AccountMap.begin(); iter != g_AccountMap.end(); ++iter)
		{
			*pPacket << (*iter).second->AccountNo;
			pPacket->Enqueue((char*)(*iter).second->Nickname, sizeof(WCHAR) * df_NICK_MAX_LEN);
		}
	}
	else
	{
		*pPacket << (UINT)0;
	}

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_ACCOUNT_LIST;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendList(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// 친구목록 결과
	// TODO: 개선필요 (순회 2회)
	// {
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	UINT FriendCount = 0;
	st_ACCOUNT* TempAccount;

	pPacket->Clear();

	if (AccountNo != 0)
	{
		pair<multimap<UINT64, st_FRIEND*>::iterator, multimap<UINT64, st_FRIEND*>::iterator> range = g_FriendMap.equal_range(AccountNo);
		for (auto iter = range.first; iter != range.second; ++iter)
		{
			if (iter->first == AccountNo)
			{
				++FriendCount;
			}
		}
		*pPacket << (UINT)FriendCount;

		for (auto iter = range.first; iter != range.second; ++iter)
		{
			if (iter->first == AccountNo)
			{
				*pPacket << (*iter).second->ToAccountNo;
				TempAccount = Find_Account((*iter).second->ToAccountNo);
				pPacket->Enqueue((char*)TempAccount->Nickname, sizeof(WCHAR) * df_NICK_MAX_LEN);
			}
		}
	}
	else
	{
		*pPacket << (UINT)0;
	}

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_LIST;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendRequestList(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// 내가 요청한 - 친구목록 결과 
	// TODO: 개선필요 (순회 2회)
	// {
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	UINT FriendCount = 0;
	st_ACCOUNT* pTemp;

	pPacket->Clear();
	if (AccountNo != 0)
	{
		for (auto iter = g_FriendReqMap.begin(); iter != g_FriendReqMap.end(); ++iter)
		{
			if ((*iter).second->ToAccountNo == AccountNo)
			{
				++FriendCount;
			}
		}
		*pPacket << (UINT)FriendCount;

		for (auto iter = g_FriendReqMap.begin(); iter != g_FriendReqMap.end(); ++iter)
		{
			if ((*iter).second->ToAccountNo == AccountNo)
			{
				*pPacket << (*iter).second->FromAccountNo;
				pTemp = Find_Account((*iter).second->FromAccountNo);
				pPacket->Enqueue((char*)pTemp->Nickname, sizeof(WCHAR) * df_NICK_MAX_LEN);
			}
		}
	}
	else
	{
		*pPacket << (UINT)0;
	}

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_REQUEST_LIST;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendReplyList(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// 내가 요청받은 - 친구목록 결과 
	// TODO: 개선필요 (순회 2회)
	// {
	//		UINT	FriendCount
	//		{
	//			UINT64					FriendAccountNo
	//			WCHAR[df_NICK_MAX_LEN]	NickName
	//		}	
	// }
	//------------------------------------------------------------
	UINT FriendCount = 0;
	st_ACCOUNT* TempAccount;

	pPacket->Clear();

	if (AccountNo != 0)
	{
		for (map<UINT64, st_FRIEND_REQUEST*>::iterator iter = g_FriendReqMap.begin(); iter != g_FriendReqMap.end(); ++iter)
		{
			if ((*iter).second->FromAccountNo == AccountNo)
			{
				++FriendCount;
			}
		}
		*pPacket << (UINT)FriendCount;

		for (map<UINT64, st_FRIEND_REQUEST*>::iterator iter = g_FriendReqMap.begin(); iter != g_FriendReqMap.end(); ++iter)
		{
			if ((*iter).second->FromAccountNo == AccountNo)
			{
				*pPacket << (*iter).second->ToAccountNo;
				TempAccount = Find_Account((*iter).second->ToAccountNo);
				pPacket->Enqueue((char*)TempAccount->Nickname, sizeof(WCHAR) * df_NICK_MAX_LEN);
			}
		}
	}
	else
	{
		*pPacket << (UINT)0;
	}

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_REPLY_LIST;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendRemove(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 FriendAccountNo, BYTE Result)
{
	//------------------------------------------------------------
	// 친구관계 끊기 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << FriendAccountNo;
	*pPacket << Result;

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_REMOVE;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendRequest(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 FriendAccountNo, BYTE Result)
{
	//------------------------------------------------------------
	// 친구요청 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << FriendAccountNo;
	*pPacket << Result;

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_REQUEST;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendCancel(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 FriendAccountNo, BYTE Result)
{
	//------------------------------------------------------------
	// 친구요청취소 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << FriendAccountNo;
	*pPacket << Result;

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_CANCEL;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendDeny(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 FriendAccountNo, BYTE Result)
{
	//------------------------------------------------------------
	// 친구요청 거부 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << FriendAccountNo;
	*pPacket << Result;

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_DENY;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendAgree(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 FriendAccountNo, BYTE Result)
{
	//------------------------------------------------------------
	// 친구요청 수락 결과
	//
	// {
	//		UINT64	FriendAccountNo
	//
	//		BYTE	Result
	// }
	//------------------------------------------------------------
	pPacket->Clear();
	*pPacket << FriendAccountNo;
	*pPacket << Result;

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_FRIEND_AGREE;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void mpResFriendEcho(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, WORD Size, WCHAR* szString)
{
	//------------------------------------------------------------
	// 스트레스 테스트용 에코응답
	//
	// {
	//			WORD		Size
	//			Size		문자열 (WCHAR 유니코드)
	// }
	//------------------------------------------------------------
	pPacket->Clear();

	*pPacket << Size;
	pPacket->Enqueue((char*)szString, Size);

	pHeader->byCode = (BYTE)df_PACKET_CODE;
	pHeader->wMsgType = (WORD)df_RES_STRESS_ECHO;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

bool ServerControl()
{
	static bool bControlMode = false;

	//------------------------------------------
	// L : Control Lock / U : Unlock / Q : Quit
	//------------------------------------------
	//  _kbhit() 함수 자체가 느리기 때문에 사용자 혹은 더미가 많아지면 느려져서 실제 테스트시 주석처리 권장
	// 그런데도 GetAsyncKeyState를 안 쓴 이유는 창이 활성화되지 않아도 키를 인식함 Windowapi의 경우 
	// 제어가 가능하나 콘솔에선 어려움

	if (_kbhit())
	{
		WCHAR ControlKey = _getwch();

		if (L'u' == ControlKey || L'U' == ControlKey)
		{
			bControlMode = true;

			wprintf(L"[ Control Mode ] \n");
			wprintf(L"Press  L	- Key Lock \n");
			wprintf(L"Press  Q	- Quit \n");
		}

		if (bControlMode == true)
		{
			if (L'l' == ControlKey || L'L' == ControlKey)
			{
				wprintf(L"Controll Lock. Press U - Control Unlock \n");
				bControlMode = false;
			}

			if (L'q' == ControlKey || L'Q' == ControlKey)
			{
				return false;
			}
		}
	}
	return true;
}

bool UTF8toUTF16(const char * szIn, WCHAR * szOut, int iBuffLen)
{
	int iRe = MultiByteToWideChar(CP_UTF8, 0, szIn, strlen(szIn), szOut, iBuffLen);
	if (iRe < iBuffLen)
		szOut[iRe] = L'\0';
	return true;
}

bool UTF16toUTF8(const WCHAR * szIn, char * szOut, int iBuffLen)
{
	int iRe = WideCharToMultiByte(CP_UTF8, 0, szIn, wcslen(szIn), szOut, iBuffLen, NULL, NULL);
	if (iRe < iBuffLen)
		szOut[iRe] = '\0';
	return true;
}

bool LoadData(void)
{
	FILE *fp;
	_wfopen_s(&fp, L"Friend_DB.txt", L"rt");	// read로 열었을때 문제점 - 파일이 없으면 에러가 남

	if (fp == nullptr)
		return false;

	fseek(fp, 0, SEEK_END);
	int iFileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *pJson = new char[iFileSize + 1];
	ZeroMemory(pJson, iFileSize + 1);
	fread(pJson, iFileSize, 1, fp);

	fclose(fp);
	Document Doc;
	Doc.Parse(pJson);

	UINT64 AccountNo;
	WCHAR szNickname[df_NICK_MAX_LEN];

	Value &AccountArray = Doc["Account"];
	for (SizeType i = 0; i < AccountArray.Size(); i++)
	{
		Value &AccountObject = AccountArray[i];
		AccountNo = AccountObject["AccountNo"].GetUint64();
		UTF8toUTF16(AccountObject["Nickname"].GetString(), szNickname, df_NICK_MAX_LEN);
		// 숫자, 영어는 상관없으나 한글 데이터는 UTF16으로 변환 // rapidjson이 UTF-8로 저장해줌

		Add_Account(szNickname, AccountNo);
	}

	Value &FriendArray = Doc["Friend"];
	UINT FriendNo, FromAccountNo, ToAccountNo;
	for (SizeType i = 0; i < FriendArray.Size(); i++)
	{
		Value &FriendObject = FriendArray[i];
		FriendNo = FriendObject["No"].GetUint64();
		FromAccountNo = FriendObject["FromAccountNo"].GetUint64();
		ToAccountNo = FriendObject["ToAccountNo"].GetUint64();

		Add_Friend(ToAccountNo, FromAccountNo);
		Add_Friend(FromAccountNo, ToAccountNo);
	}

	Value &FriendRequestArray = Doc["FriendRequest"];
	for (SizeType i = 0; i < FriendRequestArray.Size(); i++)
	{
		Value &FriendObject = FriendRequestArray[i];
		FriendNo = FriendObject["No"].GetUint64();
		FromAccountNo = FriendObject["FromAccountNo"].GetUint64();
		ToAccountNo = FriendObject["ToAccountNo"].GetUint64();

		Add_FriendReq(ToAccountNo, FromAccountNo);
	}

	return true;
}

bool SaveData(void)
{
	StringBuffer StringJSON;
	Writer<StringBuffer, UTF16<>> writer(StringJSON);

	// Account 정보 저장
	writer.StartObject();
	writer.String(L"Account");
	writer.StartArray();
	for_each(g_AccountMap.begin(), g_AccountMap.end(), [&writer](pair<UINT64, st_ACCOUNT *> pairs)
	{
		writer.StartObject();
		writer.String(L"AccountNo");
		writer.Uint64(pairs.first);
		writer.String(L"Nickname");
		writer.String(pairs.second->Nickname);
		writer.EndObject();
	});
	writer.EndArray();

	// 친구정보 저장
	writer.String(L"Friend");
	writer.StartArray();
	for_each(g_FriendMap.begin(), g_FriendMap.end(), [&writer](pair<UINT64, st_FRIEND *> pairs)
	{
		writer.StartObject();
		writer.String(L"No");
		writer.Uint64(pairs.first);
		writer.String(L"FromAccountNo");
		writer.Uint64(pairs.second->FromAccountNo);
		writer.String(L"ToAccountNo");
		writer.Uint64(pairs.second->ToAccountNo);
		writer.EndObject();
	});
	writer.EndArray();

	// 친구요청 정보 저장
	writer.String(L"FriendRequest");
	writer.StartArray();
	for_each(g_FriendReqMap.begin(), g_FriendReqMap.end(), [&writer](pair<UINT64, st_FRIEND_REQUEST *> pairs)
	{
		writer.StartObject();
		writer.String(L"No");
		writer.Uint64(pairs.first);
		writer.String(L"FromAccountNo");
		writer.Uint64(pairs.second->FromAccountNo);
		writer.String(L"ToAccountNo");
		writer.Uint64(pairs.second->ToAccountNo);
		writer.EndObject();
	});
	writer.EndArray();
	writer.EndObject();

	const char *pJson = StringJSON.GetString();

	FILE *fp;
	// rapidjson이 UTF-8로 저장해줌
	_wfopen_s(&fp, L"Friend_DB.txt", L"wt");	// write로 했을때 문제점 : 기존 내용을 모두 지우고 다시 씀

	if (fp == nullptr)
		return false;

	fwrite(pJson, StringJSON.GetSize(), 1, fp);

	fclose(fp);

	return true;
}
