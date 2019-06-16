#pragma once
#pragma comment(lib, "ws2_32")
#pragma comment(lib, "Winmm.lib")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include "Protocol.h"

#include "CSystemLog.h"
#include "CRingBuffer.h"
#include "CSerialBuffer.h"

#define dfBUFFSIZE 1024

///////////////////////////////////////////////////////
// Structure
///////////////////////////////////////////////////////
struct st_CLIENT  // ∞Ë¡§
{
	DWORD UserNo;

	SOCKET Sock;

	mylib::CRingBuffer SendQ;
	mylib::CRingBuffer RecvQ;

	UINT64 iRequestPacket;
	UINT64 iResponsePacket;

	DWORD dwRequestTick;
	UINT iPacketCnt;

	WCHAR *pStressEcho;
};

void Monitoring(DWORD nowTick);

///////////////////////////////////////////////////////
// Network Process
///////////////////////////////////////////////////////
bool NetworkInit();
void NetworkClean();
void NetworkProcess();

void SelectSocket(SOCKET* pTableSocket, FD_SET *pReadSet, FD_SET *pWriteSet);

///////////////////////////////////////////////////////
// Recv & Send
///////////////////////////////////////////////////////
bool ProcSend(SOCKET Sock);
void ProcRecv(SOCKET Sock);
void SendUnicast(st_CLIENT * pClient, st_PACKET_HEADER * pHeader, mylib::CSerialBuffer * pPacket);

///////////////////////////////////////////////////////
// Contents
///////////////////////////////////////////////////////
st_CLIENT*	Find_Client(SOCKET sock);
void		Disconnect(SOCKET sock);

bool PacketProc(st_CLIENT * client, WORD wType, mylib::CSerialBuffer * Packet);

int CompleteRecvPacket(st_CLIENT *pClient);

bool ResStressEcho(st_CLIENT *pClient, mylib::CSerialBuffer *Buffer);
bool ReqStressEcho(st_CLIENT *pClient);

void mpReqFriendEcho(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, WORD Size, WCHAR * szString);

void DummyConnect();
void DummyProcess();