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
//  multimap�� ����ؾ� �ϴ� ���
// - ����� �����͵��� �����ؾ� �� ���
// - ���� �ڷ���� �����ϰ�, Ư�� �ڷῡ ���� �˻��� ������ �ؾ� �� ���
// - map�� �����ϳ�, key ���� �ߺ��� ���Ҹ� ���� �� �ִ� ���
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

	// Map ����
	SaveData();
}

void NetworkProcess()
{
	SOCKET	UserTable_SOCKET[FD_SETSIZE];	// FD_SET�� ��ϵ� ���� ����
	int		iSockCnt = 0;
	//-----------------------------------------------------
	// FD_SET�� FD_SETSIZE��ŭ���� ���� �˻� ����
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
	// ListenSocket �� ��� Ŭ���̾�Ʈ�� ���� Socket �˻�
	//-----------------------------------------------------
	for (auto iter = g_ClientMap.begin(); iter != g_ClientMap.end();)
	{
		st_CLIENT * pClient = iter->second;
		++iter;	// SelectSocket �Լ� ���ο��� ClientMap�� �����ϴ� ��찡 �־ �̸� ����
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
			SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet, iSockCnt);

			FD_ZERO(&ReadSet);
			FD_ZERO(&WriteSet);

			memset(UserTable_SOCKET, INVALID_SOCKET, sizeof(SOCKET) * FD_SETSIZE);
			//---------------------------------------------------------
			//  ���� �����忡 select ����� ��� ������ õ �� �̻����� ��������
			// ������ ������ ���ϰ� �ɸ��� ��� ������ ������ ��û ��������.
			//  ���� 64���� select ó���� �� �� �Ź� accept ó���� ���ֵ��� �Ѵ�.
			// ������ ó���� ������ �� ��Ȱ�ϰ� ����ȴ�.
			//---------------------------------------------------------
			FD_SET(g_ListenSock, &ReadSet);
			UserTable_SOCKET[0] = g_ListenSock;

			iSockCnt = 1;
		}
	}
	//-----------------------------------------------------
	// ��ü Ŭ���̾�Ʈ for �� ���� �� iSockCnt ��ġ�� �ִٸ�
	// �߰������� ������ Select ȣ���� ���ش�
	//-----------------------------------------------------
	if (iSockCnt > 0)
	{
		SelectSocket(UserTable_SOCKET, &ReadSet, &WriteSet, iSockCnt);
	}
}

void SelectSocket(SOCKET* pTableSocket, FD_SET *pReadSet, FD_SET *pWriteSet, int iSockCnt)
{
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
	timeval Time;
	Time.tv_sec = 0;
	Time.tv_usec = 0;

	//-----------------------------------------------------
	// ������ ��û�� ���� �������� Ŭ���̾�Ʈ���� �޽��� �۽� �˻�
	//-----------------------------------------------------
	//  select() �Լ����� block ���¿� �ִٰ� ��û�� ������,
	// select() �Լ��� ��뿡�� ��û�� ���Դ��� ������ �ϰ� �� ������ return�մϴ�.
	// select() �Լ��� return �Ǵ� ���� flag�� �� ���ϸ� WriteSet, ReadSet�� �����ְ� �˴ϴ�.
	// �׸��� ���� ���� ������ ���� flag ǥ�ø� �� ���ϵ��� checking�ϴ� ���Դϴ�.
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
				// ListenSocket�� ������ ���� �뵵�̹Ƿ� ���� ó�� 
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
			iResult = CompletePacket(pClient);

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



	// SendQ�� �ִ� �����͵��� �ִ� dfNETWORK_WSABUFF_SIZE ũ��� ������
	iSendSize = pClient->SendQ.GetUseSize();
	iSendSize = min(dfBUFFSIZE, iSendSize);

	if (iSendSize <= 0)
		return FALSE;

	// Peek���� ������ ����. ������ ����� ���������� ��� ����
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
	// �̹� ģ���� ���
	pair<multimap<UINT64, st_FRIEND*>::iterator, multimap<UINT64, st_FRIEND*>::iterator> range = g_FriendMap.equal_range(FromAccountNo);
	for (auto iter = range.first; iter != range.second; ++iter)
	{
		if ((iter->first == FromAccountNo && iter->second->ToAccountNo == ToAccountNo) ||
			(iter->first == ToAccountNo && iter->second->ToAccountNo == FromAccountNo))
		{
			return df_RESULT_FRIEND_REQUEST_NOTFOUND;
		}
	}

	// �̹� ��û���� ���
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
	//// �̹� �����ϴ� �г����ΰ�
	//for (map<UINT64, st_ACCOUNT*>::iterator iter = g_AccountMap.begin(); iter != g_AccountMap.end(); ++iter)
	//{
	//	if (wcscmp((*iter).second->Nickname, szName) == 0)
	//	{
	//		return NULL;
	//	}
	//}

	// JSON ������ ������ ���ο��� ����
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
	// ������ ���ο��� ��û�� �� ����
	if (FromAccountNo == ToAccountNo || Find_Account(ToAccountNo) == NULL || Find_Account(FromAccountNo) == NULL)
		return df_RESULT_FRIEND_REQUEST_NOTFOUND;

	// g_FriendReqMap, g_RequestFrom, g_RequestTo �߰��ؾ� �ȴ�.
	int iResult = Find_FriendReq(FromAccountNo, ToAccountNo);
	if (iResult != df_RESULT_FRIEND_REQUEST_OK)
		return iResult;

	// �߰��ϱ����� To -> From �������� ģ���� ���°��� Ȯ���Ѵ�.
	if (Find_FriendReq(ToAccountNo, FromAccountNo) == df_RESULT_FRIEND_REQUEST_AREADY)
	{
		// �� ��� To, From ������ ����� ģ���� �߰��Ѵ�.
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
	// ģ�� ��û ��Ͽ� �ش� ������ �ִ°�
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

	// �ε��������� ����
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
	// ȸ������ ��û
	//
	// {
	//		WCHAR[df_NICK_MAX_LEN]	�г���
	// }
	//------------------------------------------------------------
	Packet->Dequeue((char*)szID, df_NICK_MAX_LEN);

	st_ACCOUNT* stNewAccount = Add_Account(szID, g_AccountNo);
	//if (stNewAccount == NULL)
	//{
	//	�г��� �ߺ� ó��
	//}

	//------------------------------------------------------------
	// ȸ������ ���
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
	// ȸ���α���
	//
	// {
	//		UINT64		AccountNo
	// }
	//------------------------------------------------------------
	// 256 �̻��� ������ ��� ����ó��
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
	// ȸ���α��� ���
	//
	// {
	//		UINT64					AccountNo		// 0 �̸� ����
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
	// ȸ������Ʈ ��û
	//
	// {
	//		����.
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// ȸ������Ʈ ���
	//
	// {
	//		UINT	Count		// ȸ�� ��
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
	// ģ����� ��û
	//
	// {
	//		����
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// ģ����� ���
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
	// ģ����û ���� ���  ��û
	//
	// {
	//		����
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// ģ����� ���
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
	// ģ����û ������ ���  ��û
	//
	// {
	//		����
	// }
	//------------------------------------------------------------
	//------------------------------------------------------------
	// ģ����� ���
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
	// ģ������ ����
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
	// ģ������ ���� ���
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
	// ģ����û
	//
	// {
	//		UINT64	FriendAccountNo
	// }
	//------------------------------------------------------------
	*Packet >> FriendAccountNo;

	Result = Add_FriendReq(client->AccountNo, FriendAccountNo);

	//------------------------------------------------------------
	// ģ����û ���
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
	// ģ����û ���
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
	// ģ����û��� ���
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
	// ģ����û �ź�
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
	// ģ����û �ź� ���
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
	// ģ����û ����
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
	// ģ����û ���� ���
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
	// ��Ʈ���� �׽�Ʈ�� ����
	//
	// {
	//			WORD		Size
	//			Size		���ڿ� (WCHAR �����ڵ�)
	// }
	//------------------------------------------------------------
	*Packet >> Size;
	Packet->Dequeue((char*)szMsg, Size);

	//------------------------------------------------------------
	// ��Ʈ���� �׽�Ʈ�� ��������
	//
	// {
	//			WORD		Size
	//			Size		���ڿ� (WCHAR �����ڵ�)
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
	if ((WORD)iRecvQSize <  stPacketHeader.wPayloadSize + sizeof(st_PACKET_HEADER))
	{
		// ����� �۴ٸ�, ��Ŷ�� ���� �Ϸ���� �ʾ����Ƿ� ������ �ٽ� ó��
		return 1;	// �� �̻� ó���� ��Ŷ�� ����
	}
	pClient->RecvQ.MoveReadPos(sizeof(st_PACKET_HEADER));

	CSerialBuffer pPacket;
	// Payload �κ� ��Ŷ ���۷� ����
	if (stPacketHeader.wPayloadSize != pClient->RecvQ.Dequeue(pPacket.GetBufferPtr(), stPacketHeader.wPayloadSize))
		return -1;
	pPacket.MoveWritePos(stPacketHeader.wPayloadSize);

	// �������� ��Ŷ ó�� �Լ� ȣ��
	if (!PacketProc(pClient, stPacketHeader.wMsgType, &pPacket))
		return -1;
	return 0;	// ��Ŷ 1�� ó�� �Ϸ�, �� �Լ� ȣ��ο��� Loop ����
}

void mpResAccountAdd(st_PACKET_HEADER * pHeader, CSerialBuffer * pPacket, UINT64 AccountNo)
{
	//------------------------------------------------------------
	// ȸ������ ���
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
	// ȸ���α��� ���
	//
	// {
	//		UINT64					AccountNo		// 0 �̸� ����
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
	// ȸ������Ʈ ���
	//
	// {
	//		UINT	Count		// ȸ�� ��
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
	// ģ����� ���
	// TODO: �����ʿ� (��ȸ 2ȸ)
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
	// ���� ��û�� - ģ����� ��� 
	// TODO: �����ʿ� (��ȸ 2ȸ)
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
	// ���� ��û���� - ģ����� ��� 
	// TODO: �����ʿ� (��ȸ 2ȸ)
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
	// ģ������ ���� ���
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
	// ģ����û ���
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
	// ģ����û��� ���
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
	// ģ����û �ź� ���
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
	// ģ����û ���� ���
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
	// ��Ʈ���� �׽�Ʈ�� ��������
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
	pHeader->wMsgType = (WORD)df_RES_STRESS_ECHO;
	pHeader->wPayloadSize = (WORD)pPacket->GetUseSize();
}

bool ServerControl()
{
	static bool bControlMode = false;

	//------------------------------------------
	// L : Control Lock / U : Unlock / Q : Quit
	//------------------------------------------
	//  _kbhit() �Լ� ��ü�� ������ ������ ����� Ȥ�� ���̰� �������� �������� ���� �׽�Ʈ�� �ּ�ó�� ����
	// �׷����� GetAsyncKeyState�� �� �� ������ â�� Ȱ��ȭ���� �ʾƵ� Ű�� �ν��� Windowapi�� ��� 
	// ��� �����ϳ� �ֿܼ��� �����

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
	_wfopen_s(&fp, L"Friend_DB.txt", L"rt");	// read�� �������� ������ - ������ ������ ������ ��

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
		// ����, ����� ��������� �ѱ� �����ʹ� UTF16���� ��ȯ // rapidjson�� UTF-8�� ��������

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

	// Account ���� ����
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

	// ģ������ ����
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

	// ģ����û ���� ����
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
	// rapidjson�� UTF-8�� ��������
	_wfopen_s(&fp, L"Friend_DB.txt", L"wt");	// write�� ������ ������ : ���� ������ ��� ����� �ٽ� ��

	if (fp == nullptr)
		return false;

	fwrite(pJson, StringJSON.GetSize(), 1, fp);

	fclose(fp);

	return true;
}
