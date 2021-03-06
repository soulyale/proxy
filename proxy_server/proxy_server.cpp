#include "stdafx.h"
#if defined  _WIN32                                                         //Windows
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define FD_SETSIZE      128
#include<winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#elif defined __linux                                                       //Linux
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <thread>
#include <queue>
#include <mutex>
#include <map>
#include <set>
#include <memory>
#include <iostream>


////////////////数据长度为0 没有考虑到
/*
browser   ---->----->tunnel_client>|  网  |<tunnel_server<-------<------squid

browser   <-----<----tunnel_client>|  络   |<tunnel_server ------->------>squid

*/

const char* SQUID_IP = "192.168.3.15";
const UINT  SQUID_PORT = 3128;
const char* SER_LISTENING_IP = "127.0.0.1";
const UINT  SER_LISTENING_PORT = 9971;


const char MASK = 11;

typedef struct {
#pragma pack(1)
	char mask=MASK;
	char port;
	short length;
#pragma pack()
}header;

class Tunnel_server {
private:
	SOCKET listening;
public:

	//监听客户端的连接
	Tunnel_server();
	void accepts();

	//向客户端发送数据
	static void sendTo(SOCKET s, char port, char* data, short length);

};
class Squid {
public:
	std::map<SOCKET, char> tube;
	std::mutex mu;
	SOCKET tunnel_listen;
public:
	//socket 为需要发送数据给通道的socket
	Squid(SOCKET  listening,bool* run);
	//接收通道的数据  发送给代理服务器
	void  reci(char port, char* data, short length);
};

int main()
{
	Tunnel_server ser ;
	while (true)
	{
		ser.accepts();
	}
	
    return 0;
}

Tunnel_server::Tunnel_server()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSADATA 版本错误" << std::endl;
	}
	this->listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in Serveraddr;
	memset(&Serveraddr, 0, sizeof(sockaddr_in*));
	Serveraddr.sin_family = AF_INET;
	Serveraddr.sin_addr.s_addr = inet_addr(SER_LISTENING_IP);
	Serveraddr.sin_port = htons(SER_LISTENING_PORT);
	if (0 != bind(this->listening, (sockaddr *)&Serveraddr, sizeof(Serveraddr))) {
		std::cout << "绑定IP和端口失败" << std::endl;
	}
	if (SOCKET_ERROR == listen(this->listening, 5)) {
		std::cout << "端口监听失败" << std::endl;
	}
	else {
		std::cout << "绑定成功,开始监听  " << SER_LISTENING_IP << ":" << SER_LISTENING_PORT << std::endl;
	}
}


void Tunnel_server::accepts()
{
	SOCKET cnt = accept(this->listening, NULL, 0);
	if (SOCKET_ERROR == cnt) {
		std::cout << "接收客户端连接失败" << std::endl;
		return;
	}
	//从客户端的tunnel 接收数据  发送给squid
	std::thread([this](SOCKET s) {
		std::cout << __FILE__ << __LINE__ << "Tunnel_server 一个客户端新连接" << std::endl;
		header h;
		bool run = true;
		Squid  squid(s,&run);
		while (true)
		{
			if (SOCKET_ERROR == recv(s, (char*)&h, sizeof(header), MSG_WAITALL)) {
				std::cout << __FILE__ << __LINE__ << "接收 客户端数据头 隧道 SOCKET_ERROR" << std::endl;
				break;
			}
			if (h.mask != MASK) {
				std::cout << __FILE__ << __LINE__ << "接收 客户端数据 MASK 错误" << std::endl;
				continue;
			}
			h.length = ntohs(h.length);
			if (h.length == 0){
				squid.reci(h.port, 0, 0);
				continue;
			}
			char*buf = new char[h.length];
			if (SOCKET_ERROR == recv(s, buf, h.length, MSG_WAITALL)) {
				std::cout << __FILE__ << __LINE__ << "接收 客户端数据 隧道 SOCKET_ERROR" << std::endl;
				free(buf);
				break;
			}
			squid.reci(h.port, buf, h.length);
			free(buf);
		}
		run = false;
		std::cout << __FILE__ << __LINE__ << "Tunnel_server 一个客户端断开连接" << std::endl;
	}, cnt).detach();
}

void Tunnel_server::sendTo(SOCKET s,char port,char* data,short length)
{
	header h; h.length = htons(length); h.port = port; h.mask = MASK;
	char *dd = new char[sizeof(header)+length];
	memcpy(dd, &h, sizeof(header));
	if(length>0)
		memcpy((dd + sizeof(header)), data, length);

	if (SOCKET_ERROR == send(s,dd, sizeof(header) + length,0)) {
		std::cout << __FILE__ << __LINE__ << "发送 数据 给 客户端 隧道 SOCKET_ERROR" << std::endl;
	}
	free(dd);
}

//接收从代理的数据  发送给隧道
Squid::Squid(SOCKET listening,bool *run)
{
	//如果这个socket 不能用  这个线程是要退出的
	this->tunnel_listen = listening;
	std::thread([this](SOCKET listening,bool *run) {
		fd_set squidsockets;
		timeval st;
		st.tv_sec = 5;
		st.tv_usec = 0;
		while (*run)
		{
			this->mu.lock();
			std::map<SOCKET, char>  selfsocks = this->tube;
			this->mu.unlock();

			if (selfsocks.empty()) continue;

			FD_ZERO(&squidsockets);
			for (auto sok : selfsocks) {
				FD_SET(sok.first, &squidsockets);
			}
			char buf[1024];
			int rs = select(0, &squidsockets, NULL, NULL, &st);
			if (rs > 0) {
				for (int i = 0; i < rs; i++) {
					int n = recv(squidsockets.fd_array[i], buf, 1024, 0);
					if (n > 0) {

						Tunnel_server::sendTo(listening, selfsocks[squidsockets.fd_array[i]], buf, n);
					}
					else
					{
						std::cout << __FILE__ << __LINE__ << "  从代理接收数据失败，关闭一个连接" << std::endl;
						this->mu.lock();
						this->tube.erase(squidsockets.fd_array[i]);
						closesocket(squidsockets.fd_array[i]);
						this->mu.unlock();
						Tunnel_server::sendTo(listening, selfsocks[squidsockets.fd_array[i]], 0, 0);
					}
				}
			}
		}
	}, listening,run).detach();
}

/////从隧道接收的数据 发送给代理
void Squid::reci(char port, char * data, short length)
{
	this->mu.lock();
	std::map<SOCKET, char> self_tube = this->tube;
	this->mu.unlock();
	SOCKET  m_socket = 0;
	for (auto nn : self_tube) {
		if (nn.second == port) {
			m_socket = nn.first;
		}
	}
	if (length == 0) {
		if (m_socket != 0) {
			this->mu.lock();
			this->tube.erase(m_socket);
			closesocket(m_socket);
			this->mu.unlock();
		}
		return;
	}
	//创建连接到代理
	if (0 == m_socket) {
		m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in m_nServeraddr;
		memset(&m_nServeraddr, 0, sizeof(m_nServeraddr));
		m_nServeraddr.sin_family = AF_INET;
		m_nServeraddr.sin_port = htons(SQUID_PORT);
		m_nServeraddr.sin_addr.s_addr = inet_addr(SQUID_IP);
		if (0 != connect(m_socket, (sockaddr*)&m_nServeraddr, sizeof(m_nServeraddr))) {
			std::cout << __FILE__ << __LINE__ << "连接代理服务失败" << std::endl;
			return;
		}
		this->mu.lock();
		this->tube.insert(std::pair<SOCKET, char>(m_socket,port));
		this->mu.unlock();
	}
	if (SOCKET_ERROR == send(m_socket, data, length, 0)) {
		std::cout << __FILE__ << __LINE__ << "发送数据到代理服务失败" << std::endl;
		this->mu.lock();
		this->tube.erase(m_socket);
		closesocket(m_socket);
		this->mu.unlock();
		//this->
		///这里应该逆向关闭连接 以后处理
		Tunnel_server::sendTo(this->tunnel_listen, port, 0, 0);
		return;
	}
	



}
