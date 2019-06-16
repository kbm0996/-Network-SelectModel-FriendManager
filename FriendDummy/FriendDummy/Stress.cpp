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
	for_each �˰����� First, Last�����ȿ� �ִ� �� ��Ҹ� �Լ� F�� ���ڷ� ȣ���Ѵ�. �� �Լ��� �Ϸ��� ��ҿ� ���ؼ� ������ ���� �ʴ´�.
	*/
	for_each(g_ClientMap.begin(), g_ClientMap.end(), // iterator
		[&RecvCnt, &SendCnt, &Tick, &nowTick, &minTick, &maxTick, &Cnt](pair<SOCKET, st_CLIENT *> pair) // Parameter
	{ // Func
		st_CLIENT *pClient = pair.second;

		if (pClient->Sock != INVALID_SOCKET)
		{
			// Latency ���, Recv ���
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
	// FD_SET�� FD_SETSIZE��ŭ���� ���� �˻� ����
	//-----------------------------------------------------
	FD_SET ReadSet;
	FD_SET WriteSet;
	FD_ZERO(&ReadSet);
	FD_ZERO(&WriteSet);
	memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) *FD_SETSIZE);

	//-----------------------------------------------------
	// ��� Ŭ���̾�Ʈ�� ���� Socket �˻�
	//-----------------------------------------------------
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end();)
	{
		st_CLIENT *pClient = iter->second;
		++iter; 	// SelectSocket �Լ� ���ο��� ClientMap�� �����ϴ� ��찡 �־ �̸� ����
					//-----------------------------------------------------
					// �ش� Ŭ���̾�Ʈ ReadSet ���
					// SendQ�� �����Ͱ� �ִٸ� WriteSet ���
					//-----------------------------------------------------
		UserTable_SOCKET[iSockCnt] = pClient->Sock;

		// ReadSet ���
		FD_SET(pClient->Sock, &ReadSet);

		// WriteSet ���
		if (pClient->SendQ.GetUseSize() > 0)
			FD_SET(pClient->Sock, &WriteSet);

		++iSockCnt;
		//-----------------------------------------------------
		// select �ִ�ġ ����, ������� ���̺� ������ select ȣ�� �� ����
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
	// ��ü Ŭ���̾�Ʈ for �� ���� �� iSockCnt ��ġ�� �ִٸ�
	// �߰������� ������ Select ȣ���� ���ش�
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
	// select() �Լ��� ����Ʈ �Ѱ� ���� FD_SETSIZE(�⺻ 64����)�� ���� �ʾҴٸ� 
	// TimeOut �ð�(Select ��� �ð�)�� 0���� ���� �ʿ䰡 ����.
	//
	// �׷���, FD_SETSIZE(�⺻ 64����)�� �Ѿ��ٸ� Select�� �� �� �̻� �ؾ��Ѵ�.
	// �� ��� TimeOut �ð��� 0���� �ؾ��ϴµ� 
	// �� ������ ù��° select���� ���� �ɷ� �ι�° select�� �������� �ʱ� �����̴�.
	////////////////////////////////////////////////////////////////////
	//-----------------------------------------------------
	// select �Լ��� ���ð� ����
	//-----------------------------------------------------
	Time.tv_sec = 0;
	Time.tv_usec = 0;

	//-----------------------------------------------------
	// ������ ��û�� ���� �������� Ŭ���̾�Ʈ���� �޽��� �۽� �˻�
	//-----------------------------------------------------
	//  select() �Լ����� block ���¿� �ִٰ� ��û�� ������,
	// select() �Լ��� ��뿡�� ��û�� ���Դ��� ������ �ϰ� �� ������ return�մϴ�.
	// select() �Լ��� return �Ǵ� ���� flag�� �� ���ϸ� WriteSet, ReadSet�� �����ְ� �˴ϴ�.
	// �׸��� ���� ���� ������ ���� flag ǥ�ø� �� ���ϵ��� checking�ϴ� ���Դϴ�.
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

	// SendQ�� �ִ� �����͵��� �ִ� dfNETWORK_WSABUFF_SIZE ũ��� ������
	int iSendSize = pClient->SendQ.GetUseSize();
	iSendSize = min(dfBUFFSIZE, iSendSize);

	if (iSendSize <= 0)
		return FALSE;

	// Peek���� ������ ����. ������ ����� ���������� ��� ����
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
			// ���� ������� �� ũ�ٸ� ����
			// ����� �ȵǴ� ��Ȳ������ ���� �̷� ��찡 ���� �� �ִ�
			//-----------------------------------------------------
			wprintf(L"!!!!!! Send Size Error - Socket:%lld SendSize:%d SendResult:%d \n", Sock, iSendSize, iResult);
			return FALSE;
		}
		else
		{
			//-----------------------------------------------------
			// Send Complate
			// ��Ŷ ������ �Ϸ�ƴٴ� �ǹ̴� �ƴ�. ���� ���ۿ� ���縦 �Ϸ��ߴٴ� �ǹ�
			// �۽� ť���� Peek���� ���´� ������ ����
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
	// �޴ٰ� ���� �߻��� ����
	if (iResult == SOCKET_ERROR || iResult == 0)
	{
		err = WSAGetLastError();
		Disconnect(Sock);
		return;
	}

	// ���� �����Ͱ� �ִٸ�
	if (iResult > 0)
	{
		pClient->RecvQ.Enqueue(RecvBuff, iResult);
		//-----------------------------------------------------
		// ��Ŷ Ȯ��
		// * ��Ŷ�� �ϳ� �̻� ���ۿ� ���� �� �����Ƿ� �ݺ������� �� ���� ���� ó���ؾ���
		//-----------------------------------------------------
		while (1)
		{
			iResult = CompleteRecvPacket(pClient);

			// ó���� ��Ŷ�� ����
			if (iResult == 1)
				break;
			// ��Ŷ ó�� ����
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
	// ���� ���� �˻�
	// ��Ŷ��� ũ�� �̻����� ���� �޾Ҵٸ� ��ŵ
	//-----------------------------------------------------
	if (iRecvQSize < sizeof(st_PACKET_HEADER)) // ó���� ��Ŷ����
		return 1;
	//-----------------------------------------------------
	// I. PacketCode �˻�
	// Peek���� �˻縦 �ϴ� ������ ����� ���� �� ������ �� �� �ϳ��� �������� ��Ŷ��ŭ�� �����Ͱ� �ִ��� Ȯ����
	// ��Ŷ�� ���� ������ �׳� �ߴ����� ����
	// Get���� ��� ���� �˻� �� ����� �ȸ´ٸ� ����� �ٽ� ť�� ���ڸ��� �־�� �ϴµ� FIFO �̹Ƿ� �����
	//-----------------------------------------------------
	pClient->RecvQ.Peek((char*)&stPacketHeader, sizeof(st_PACKET_HEADER));

	if (stPacketHeader.byCode != df_PACKET_CODE)
		return -1;

	//-----------------------------------------------------
	// II. ť�� ����� �����Ͱ� ������ϴ� ��Ŷ�� ũ�⸸ŭ �ִ°�
	//-----------------------------------------------------
	// ����� �۴ٸ�, ��Ŷ�� ���� �Ϸ���� �ʾ����Ƿ� ������ �ٽ� ó��
	if ((WORD)iRecvQSize <  stPacketHeader.wPayloadSize + sizeof(st_PACKET_HEADER))
		return 1;	// �� �̻� ó���� ��Ŷ�� ����
	pClient->RecvQ.MoveReadPos(sizeof(st_PACKET_HEADER));

	mylib::CSerialBuffer pPacket;
	// Payload �κ� ��Ŷ ���۷� ����
	if (stPacketHeader.wPayloadSize != pClient->RecvQ.Dequeue(pPacket.GetBufferPtr(), stPacketHeader.wPayloadSize))
		return -1;
	pPacket.MoveWritePos(stPacketHeader.wPayloadSize);

	// �������� ��Ŷ ó�� �Լ� ȣ��
	if (!PacketProc(pClient, stPacketHeader.wMsgType, &pPacket))
		return -1;
	return 0;	// ��Ŷ 1�� ó�� �Ϸ�, �� �Լ� ȣ��ο��� Loop ����
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
	// ��Ʈ���� �׽�Ʈ�� ��������
	//
	// {
	//			WORD		Size
	//			Size		���ڿ� (WCHAR �����ڵ�)
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
	// ��Ʈ���� �׽�Ʈ�� ����
	//
	// {
	//			WORD		Size
	//			Size		���ڿ� (WCHAR �����ڵ�)
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
	// ��Ʈ���� �׽�Ʈ�� ����
	//
	// {
	//			WORD		Size
	//			Size		���ڿ� (WCHAR �����ڵ�)
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