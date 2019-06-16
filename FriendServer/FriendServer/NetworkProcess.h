#ifndef __NETWORK_H__
#define __NETWORK_H__

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "Winmm.lib")
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <conio.h>
#include <list>
#include <map>
#include "Protocol.h"

#include <algorithm>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "CRingBuffer.h"
#include "CSerialBuffer.h"
#include "CSystemLog.h"

using namespace rapidjson;

///////////////////////////////////////////////////////
// Network Process
///////////////////////////////////////////////////////
bool NetworkInit();
void NetworkClean();
void NetworkProcess();

void SelectSocket(SOCKET* pTableSocket, FD_SET *pReadSet, FD_SET *pWriteSet, int iSockCnt);

///////////////////////////////////////////////////////
// Structure
///////////////////////////////////////////////////////
struct st_CLIENT
{
	SOCKET Sock;
	SOCKADDR_IN ConnectAddr;
	mylib::CRingBuffer SendQ;
	mylib::CRingBuffer RecvQ;
	UINT64 AccountNo;
};

struct st_ACCOUNT
{
	UINT64 AccountNo;
	WCHAR Nickname[df_NICK_MAX_LEN];
};

struct st_FRIEND
{
	UINT64 FromAccountNo;
	UINT64 ToAccountNo;
};

struct st_FRIEND_REQUEST
{
	UINT64 FromAccountNo;
	UINT64 ToAccountNo;
};


///////////////////////////////////////////////////////
// Recv & Send
///////////////////////////////////////////////////////
void ProcAccept();
void ProcRecv(SOCKET Sock);
bool ProcSend(SOCKET Sock);
void SendUnicast(st_CLIENT * pClient, st_PACKET_HEADER * pHeader, mylib::CSerialBuffer * pPacket);

///////////////////////////////////////////////////////
// Contents
///////////////////////////////////////////////////////
st_CLIENT*		Find_Client(SOCKET sock);
st_ACCOUNT*		Find_Account(UINT64 accountno);
BYTE			Find_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo);

st_ACCOUNT*	Add_Account(WCHAR *szName, UINT64 uiAccountNo);
void		Add_Friend(UINT64 FromAccountNo, UINT64 ToAccountNo);
UINT64		Delete_Friend(UINT64 FromAccountNo, UINT64 ToAccountNo);
UINT64		Add_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo);
UINT64		Delete_FriendReq(UINT64 FromAccountNo, UINT64 ToAccountNo);
void		Disconnect(SOCKET sock);

bool PacketProc(st_CLIENT * client, WORD wType, mylib::CSerialBuffer* Packet);
bool PacketProc_Account_Add(st_CLIENT *client, mylib::CSerialBuffer* Packet);
bool PacketProc_Login(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Account_List(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_List(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Request_List(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Reply_List(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Remove(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Request(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Cancel(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Deny(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Agree(st_CLIENT * client, mylib::CSerialBuffer* Packet);
bool PacketProc_Friend_Echo(st_CLIENT * client, mylib::CSerialBuffer* Packet);

int CompletePacket(st_CLIENT * pClient);

void mpResAccountAdd(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo);
void mpResLogin(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo, WCHAR* pID);
void mpResAccountList(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo);
void mpResFriendList(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo);
void mpResFriendRequestList(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo);
void mpResFriendReplyList(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 AccountNo);
void mpResFriendRemove(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 FriendAccountNo, BYTE Result);
void mpResFriendRequest(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 FriendAccountNo, BYTE Result);
void mpResFriendCancel(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 FriendAccountNo, BYTE Result);
void mpResFriendDeny(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 FriendAccountNo, BYTE Result);
void mpResFriendAgree(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, UINT64 FriendAccountNo, BYTE Result);
void mpResFriendEcho(st_PACKET_HEADER * pHeader, mylib::CSerialBuffer *pPacket, WORD Size, WCHAR* szString);


///////////////////////////////////////////////////////
// Server Control
///////////////////////////////////////////////////////
bool ServerControl();

///////////////////////////////////////////////////////
// rapidjson
///////////////////////////////////////////////////////
// 실제로는 DB에 저장. json 실습삼아 써보는 것 -> 대게는 프로토콜로써 사용
bool UTF8toUTF16(const char *szText, WCHAR *szBuff, int iBuffLen);
bool UTF16toUTF8(const WCHAR * szIn, char * szOut, int iBuffLen);
bool LoadData(void);
bool SaveData(void);

extern UINT64 g_Connect;
#endif