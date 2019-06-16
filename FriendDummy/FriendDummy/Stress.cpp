#include "Stress.h"
#include <map>
#include <algorithm>
using namespace std;

map<SOCKET, st_CLIENT*>		g_ClientMap;

UINT64 g_ConnectMax = 1500;

UINT64 g_ConnectTry;
UINT64 g_ConnectSuccess;
UINT64 g_ConnectFail;
UINT64 g_ConnectTotal;

UINT64 g_Error;

void Monitoring(DWORD nowTick)
{
	UINT RecvCnt = 0;
	UINT SendCnt = 0;

	DWORD Tick;
	DWORD minTick = 9999;
	DWORD maxTick = 0;
	int Cnt = 0;

	/*
	template<class InputIterator, class Function> inline
	Function for_each(
	InputIterator First,
	InputIterator Last,
	Function F
	)
	The for_each algorithm calls Function F for each element in the range [First, Last) and returns the input parameter F. This function does not modify any elements in the sequence.
	for_each 알고리즘은 First, Last범위안에 있는 각 요소를 함수 F의 인자로 호출한다. 이 함수는 일련의 요소에 대해서 수정을 하지 않는다.
	*/
	for_each(g_ClientMap.begin(), g_ClientMap.end(), // iterator
		[&RecvCnt, &SendCnt, &Tick, &nowTick, &minTick, &maxTick, &Cnt](pair<SOCKET, st_CLIENT *> pair) // Parameter
	{ // Func
		st_CLIENT *pClient = pair.second;

		if (pClient->Sock != INVALID_SOCKET)
		{
			// Latency 계산, Recv 계산
			DWORD tTick = max(nowTick - pClient->dwRequestTick, 0);

			RecvCnt += pClient->iResponsePacket;
			SendCnt += pClient->iRequestPacket;

			if (tTick != 0)
			{

				if (tTick > maxTick)
					maxTick = tTick;

				if (tTick < minTick)
					minTick = tTick;

				Tick += tTick;

				Cnt++;
			}
			pClient->iResponsePacket = 0;
			pClient->iRequestPacket = 0;
		}
	}
	);

	Tick = Tick / Cnt;

	wprintf(L"===================================================\n");
	wprintf(L" Client:%lld \n", g_ConnectMax);
	wprintf(L"===================================================\n");
	wprintf(L" Connect Try	 : %lld \n", g_ConnectTry);
	wprintf(L" Connect Success : %lld \n", g_ConnectSuccess);
	wprintf(L" Connect Total	 : %lld \n", g_ConnectTotal);
	wprintf(L" ConnectFail	 : %lld \n", g_ConnectFail);
	wprintf(L"\n");
	wprintf(L" Error - Connect Fail : %lld\n", g_ConnectFail);
	wprintf(L" Error - Packet Error : %lld\n", g_Error);
	wprintf(L"\n");
	wprintf(L" RTT  avg : %ld ms, max : %ld ms, min : %ld ms\n", Tick, maxTick, minTick);
	wprintf(L"\n");
	wprintf(L" SendPacket TPS : %lld\n", SendCnt);
	wprintf(L" RecvPacket TPS : %lld\n", RecvCnt);
	wprintf(L"\n\n");
}

bool NetworkInit()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;
	return true;
}

void NetworkClean()
{
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end(); ++iter)
	{
		SOCKET Socket = (*iter).second->Sock;

		closesocket(Socket);
	}

	WSACleanup();
}

st_CLIENT * Find_Client(SOCKET sock)
{
	map<SOCKET, st_CLIENT*>::iterator iter = g_ClientMap.find(sock);
	if (g_ClientMap.end() == iter)
	{
		printf("!! not exist client\n");
		return NULL;
	}
	return iter->second;
}

void Disconnect(SOCKET sock)
{
	st_CLIENT*	pClient = Find_Client(sock);
	if (pClient == NULL)
		return;

	g_ClientMap.erase(sock);
	closesocket(pClient->Sock);
	delete pClient;
	g_ConnectTotal--;
}

void NetworkProcess()
{
	SOCKET UserTable_SOCKET[FD_SETSIZE];
	int iSockCnt = 0;
	//-----------------------------------------------------
	// FD_SET은 FD_SETSIZE만큼씩만 소켓 검사 가능
	//-----------------------------------------------------
	FD_SET ReadSet;
	FD_SET WriteSet;
	FD_ZERO(&ReadSet);
	FD_ZERO(&WriteSet);
	memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) *FD_SETSIZE);

	//-----------------------------------------------------
	// 모든 클라이언트에 대해 Socket 검사
	//-----------------------------------------------------
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end();)
	{
		st_CLIENT *pClient = iter->second;
		++iter; 	// SelectSocket 함수 내부에서 ClientMap을 삭제하는 경우가 있어서 미리 증가
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
			SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet);

			FD_ZERO(&ReadSet);
			FD_ZERO(&WriteSet);

			memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);

			iSockCnt = 0;
		}
	}
	//-----------------------------------------------------
	// 전체 클라이언트 for 문 종료 후 iSockCnt 수치가 있다면
	// 추가적으로 마지막 Select 호출을 해준다
	//-----------------------------------------------------
	if (iSockCnt > 0)
	{
		SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet);
	}
}

void SelectSocket(SOCKET* pTableSocket, FD_SET *pReadSet, FD_SET *pWriteSet)
{
	timeval Time;
	int iResult;

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
	Time.tv_sec = 0;
	Time.tv_usec = 0;

	//-----------------------------------------------------
	// 접속자 요청과 현재 접속중인 클라이언트들의 메시지 송신 검사
	//-----------------------------------------------------
	//  select() 함수에서 block 상태에 있다가 요청이 들어오면,
	// select() 함수는 몇대에서 요청이 들어왔는지 감지를 하고 그 개수를 return합니다.
	// select() 함수가 return 되는 순간 flag를 든 소켓만 WriteSet, ReadSet에 남아있게 됩니다.
	// 그리고 각각 소켓 셋으로 부터 flag 표시를 한 소켓들을 checking하는 것입니다.
	iResult = select(0, pReadSet, pWriteSet, NULL, &Time);
	if (iResult > 0)
	{
		for (int iCnt = 0; iCnt < FD_SETSIZE; ++iCnt)
		{
			if (pTableSocket[iCnt] == INVALID_SOCKET)
				continue;

			if (FD_ISSET(pTableSocket[iCnt], pWriteSet))
			{
				ProcSend(pTableSocket[iCnt]);
			}

			if (FD_ISSET(pTableSocket[iCnt], pReadSet))
			{
				ProcRecv(pTableSocket[iCnt]);
			}
		}
	}
	else if (iResult == SOCKET_ERROR)
	{
		wprintf(L"!! select socket error \n");
	}
}

bool ProcSend(SOCKET Sock)
{
	char SendBuff[dfBUFFSIZE];

	st_CLIENT *pClient = Find_Client(Sock);
	if (pClient == NULL)
		return FALSE;

	// SendQ에 있는 데이터들을 최대 dfNETWORK_WSABUFF_SIZE 크기로 보낸다
	int iSendSize = pClient->SendQ.GetUseSize();
	iSendSize = min(dfBUFFSIZE, iSendSize);

	if (iSendSize <= 0)
		return FALSE;

	// Peek으로 데이터 뽑음. 전송이 제대로 마무리됐을 경우 삭제
	pClient->SendQ.Peek(SendBuff, iSendSize);

	// Send
	int iResult = send(pClient->Sock, SendBuff, iSendSize, 0);
	if (iResult == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
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
			iResult = CompleteRecvPacket(pClient);

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

void SendUnicast(st_CLIENT * pClient, st_PACKET_HEADER * pHeader, mylib::CSerialBuffer * pPacket)
{
	if (pClient == NULL)
	{
		wprintf(L"SendUnicast Client is NULL \n");
		return;
	}
	pClient->SendQ.Enqueue((char*)pHeader, sizeof(st_PACKET_HEADER));
	pClient->SendQ.Enqueue((char*)pPacket->GetBufferPtr(), pPacket->GetUseSize());
}

int CompleteRecvPacket(st_CLIENT *pClient)
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
	// 사이즈가 작다면, 패킷이 아직 완료되지 않았으므로 다음에 다시 처리
	if ((WORD)iRecvQSize <  stPacketHeader.wPayloadSize + sizeof(st_PACKET_HEADER))
		return 1;	// 더 이상 처리할 패킷이 없음
	pClient->RecvQ.MoveReadPos(sizeof(st_PACKET_HEADER));

	mylib::CSerialBuffer pPacket;
	// Payload 부분 패킷 버퍼로 복사
	if (stPacketHeader.wPayloadSize != pClient->RecvQ.Dequeue(pPacket.GetBufferPtr(), stPacketHeader.wPayloadSize))
		return -1;
	pPacket.MoveWritePos(stPacketHeader.wPayloadSize);

	// 실질적인 패킷 처리 함수 호출
	if (!PacketProc(pClient, stPacketHeader.wMsgType, &pPacket))
		return -1;
	return 0;	// 패킷 1개 처리 완료, 본 함수 호출부에서 Loop 유도
}

bool PacketProc(st_CLIENT * client, WORD wType, mylib::CSerialBuffer * Packet)
{
	switch (wType)
	{
	case df_RES_STRESS_ECHO:
		return ResStressEcho(client, Packet);
		break;
	default:
		wprintf(L"Packet Error !!!!!!!!!!!!!!!!!!!!\n");
		wprintf(L"Packet Error !!!!!!!!!!!!!!!!!!!!\n");
		return false;
	}
	return true;
}

bool ResStressEcho(st_CLIENT *pClient, mylib::CSerialBuffer *Buffer)
{
	if (pClient->pStressEcho == nullptr)
		return true;

	WCHAR *pData = pClient->pStressEcho;
	WCHAR *pTempData = new WCHAR[1000];
	WORD iSize;
	//------------------------------------------------------------
	// 스트레스 테스트용 에코응답
	//
	// {
	//			WORD		Size
	//			Size		문자열 (WCHAR 유니코드)
	// }
	//------------------------------------------------------------
	*Buffer >> iSize;

	Buffer->Dequeue((char*)pTempData, iSize);

	if (memcmp(pTempData, pData, iSize) != 0)
	{
		wprintf(L"Echo Error\n");
		g_Error++;
	}
	delete[] pClient->pStressEcho;
	delete[] pTempData;

	pClient->pStressEcho = NULL;
	pClient->iResponsePacket++;
	pClient->iPacketCnt++;

	ReqStressEcho(pClient);
	return true;
}

bool ReqStressEcho(st_CLIENT *pClient)
{
	if (pClient->pStressEcho != NULL)
		return true;

	st_PACKET_HEADER stpPacketHeader;
	WORD wSize = rand() % 500 + 300;
	WCHAR *pData = new WCHAR[wSize];
	mylib::CSerialBuffer Packet;
	//------------------------------------------------------------
	// 스트레스 테스트용 에코
	//
	// {
	//			WORD		Size
	//			Size		문자열 (WCHAR 유니코드)
	// }
	//------------------------------------------------------------
	pClient->pStressEcho = pData;

	mpReqFriendEcho(&stpPacketHeader, &Packet, wSize, pClient->pStressEcho);
	SendUnicast(pClient, &stpPacketHeader, &Packet);

	pClient->iRequestPacket++;
	pClient->dwRequestTick = GetTickCount64();
	return true;
}

void mpReqFriendEcho(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer * pPacket, WORD Size, WCHAR * szString)
{
	//------------------------------------------------------------
	// 스트레스 테스트용 에코
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
	pHeader->wMsgType = (WORD)df_REQ_STRESS_ECHO;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

void DummyConnect()
{
	SOCKADDR_IN serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(df_NETWORK_PORT);
	InetPton(AF_INET, L"127.0.0.1", &serveraddr.sin_addr);

	for (; g_ConnectTotal < g_ConnectMax; g_ConnectTotal++)
	{
		SOCKET ClientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (connect(ClientSock, (SOCKADDR *)& serveraddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
		{
			wprintf(L"Connect Fail\n");
			g_ConnectFail++;
			continue;
		}

		st_CLIENT *pClient = new st_CLIENT;
		pClient->dwRequestTick = 0;
		pClient->iRequestPacket = 0;
		pClient->iResponsePacket = 0;
		pClient->iPacketCnt = 0;
		pClient->pStressEcho = NULL;
		pClient->Sock = ClientSock;

		g_ClientMap.emplace(pClient->Sock, pClient);

		g_ConnectTry++;
		g_ConnectSuccess++;
	}
}

void DummyProcess()
{
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end(); ++iter)
	{
		st_CLIENT *pClient = iter->second;
		if (pClient->Sock != INVALID_SOCKET)
			ReqStressEcho(pClient);
	}
}