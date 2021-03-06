#include "stdafx.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS 
#define FD_SETSIZE      128
#include<winsock2.h>
#include <thread>
#include <queue>
#include <mutex>
#include <map>
#include <set>
#include <memory>
#include <iostream>
#pragma comment(lib,"ws2_32.lib")

/*
browser   ---->----->tunnel_client>|  网  |<tunnel_server<-------<------squid

browser   <-----<----tunnel_client>|  络   |<tunnel_server ------->------>squid

*/

//实时报告连接数目
const char*  SERVER_IP = "127.0.0.1";
const UINT   SERVER_PORT = 9971;
const char*  LOCAL_LISTENING_IP = "127.0.0.1";
const UINT   LOCAL_LISTENING_PORT = 8001;
const char MASK = 11;




class Tunnel_client;
class Browser{
private:
	
public:
	Tunnel_client * tunnel_client;
	std::mutex mu;
	std::map<SOCKET, char> m_port;
	Browser();
	char getFreePort();
	void getData(char port, short length, const char* data);
	void sendData(Tunnel_client *t);

};

//只建立连接，发送数据  接收数据

typedef struct {
#pragma pack(1)
	char mask;
	char port;
	short length;
#pragma pack()
}header;


class Tunnel_client {
private:
	const char* remote_ip;
	UINT  remote_port;
public:
	Browser * brw;
	SOCKET getConn() {
		SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in m_nServeraddr;
		memset(&m_nServeraddr, 0, sizeof(m_nServeraddr));
		m_nServeraddr.sin_family = AF_INET;
		m_nServeraddr.sin_port = htons(this->remote_port);
		m_nServeraddr.sin_addr.s_addr = inet_addr(this->remote_ip);
		if (0 != connect(sk, (sockaddr*)&m_nServeraddr, sizeof(m_nServeraddr))) {
			std::cout << "连接服务器失败" << std::endl;
			return 0;
		}
		return sk;
	}
	SOCKET sock;
	Tunnel_client(Browser* brw,const char* rmt_ip,const UINT rmt_port) {
		this->brw = brw;
		this->remote_ip = rmt_ip;
		this->remote_port = rmt_port;
		this->sock = this->getConn();
		if (this->sock == 0) {
			return;
		}
		std::thread([this]() {
			header h;
			while (true)///// mask 
			{
				if (SOCKET_ERROR == recv(this->sock,(char*)&h,sizeof(header),MSG_WAITALL)) {
					std::cout << __FILE__ << __LINE__ << "  从远程接收数据头发生错误" << std::endl;
					closesocket(this->sock);
					this->sock = getConn();
					continue;
				}
				if (h.mask != MASK) {
					std::cout << __FILE__ << __LINE__ << "  从远程接收MASK错误" << std::endl;
					continue;
				}
				h.length = ntohs(h.length);
				if (h.length == 0) {
					this->brw->getData(h.port, 0, 0);
					continue;
				}
				char *da = new char[h.length];
				if (SOCKET_ERROR == recv(this->sock, da, h.length, MSG_WAITALL)) {
					std::cout << __FILE__ << __LINE__ << "  从远程接收数据发生错误" << std::endl;
					closesocket(this->sock);
					this->sock = getConn();
					continue;
				}
				this->brw->getData(h.port, h.length, da);
				free(da);
			}
		}).detach();
	}
	//加上头哦
	void sendTo(char port,char * data,short size) {
		header h; h.length = htons(size); h.mask = MASK; h.port = port;
		char* dd = new char[size + sizeof(header)];
		memcpy(dd, &h, sizeof(header));
		if(size>0)
			memcpy((dd + sizeof(header)), data, size);
		if (SOCKET_ERROR == send(this->sock, dd, size + sizeof(header), 0)) {
			std::cout << __FILE__ << __LINE__ <<"    给远程服务器发送发生错误" << std::endl;
		}
		free(dd);
	}
};


int main()
{
	Browser brows;
	Tunnel_client tc(&brows, SERVER_IP, SERVER_PORT);
	while (true)
	{
		brows.sendData(&tc);
	}
    return 0;
}
Browser::Browser()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSADATA 版本错误" << std::endl;
	}
	static SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	sockaddr_in Serveraddr;
	memset(&Serveraddr, 0, sizeof(sockaddr_in*));
	Serveraddr.sin_family = AF_INET;
	Serveraddr.sin_addr.s_addr = inet_addr(LOCAL_LISTENING_IP);
	Serveraddr.sin_port = htons(LOCAL_LISTENING_PORT);
	if (0 != bind(listen_socket, (sockaddr *)&Serveraddr, sizeof(Serveraddr))) {
		std::cout << "绑定IP和端口失败" << std::endl;
	}
	if (SOCKET_ERROR == listen(listen_socket, 5)) {
		std::cout << "端口监听失败" << std::endl;
	}
	else {
		std::cout << "绑定成功,开始监听  " << LOCAL_LISTENING_IP << ":" << LOCAL_LISTENING_PORT << std::endl;
	}
	std::thread([this](SOCKET s) {
		while (true)
		{
			SOCKET cnt = accept(s, NULL, 0);
			if (SOCKET_ERROR == cnt) {
				std::cout << "接收浏览器连接失败" << std::endl;
			}
			else {
				this->mu.lock();
				this->m_port.insert(std::pair<SOCKET, char>(cnt, this->getFreePort()));
				this->mu.unlock();
				std::cout << "当前浏览器连接数为："<<this->m_port.size() << std::endl;
			}
		}

	}, listen_socket).detach();




}
//最多只能有256个端口
char Browser::getFreePort()
{
	static char i = 0;
	int x = 0;
	while (true)
	{
		i++;
		if ([this](char i)->bool{
			for (auto nn : this->m_port) {
				if (nn.second == i) return false;
			}
			return true;

		},i) {
			return i;
		}
		x++;
		if (x > 256)
			return i;
	}
}

//是Tunnel_client发送给浏览器的数据 
void Browser::getData(char port, short length, const char * data)
{
	this->mu.lock();
	std::map<SOCKET, char> self_port = this->m_port;
	this->mu.unlock();
	for (auto nn : self_port) {
		if (nn.second == port) {
			if (0==length || SOCKET_ERROR==send(nn.first, data, length, 0)) {
				this->mu.lock();
				this->m_port.erase(nn.first);
				closesocket(nn.first);
				this->mu.unlock();
			}
			return;
		}
		
	}
	//需要返回去 告诉需要关闭端口
	this->tunnel_client->sendTo(port, 0, 0);
	std::cout << __FILE__ << __LINE__ << "    端口不存在" << std::endl;
}

//接收浏览器的数据 通过Tunnel_client发送出去
void Browser::sendData(Tunnel_client * t)
{
	this->tunnel_client = t;
	this->mu.lock();
	std::map<SOCKET, char> self_ports = this->m_port;
	this->mu.unlock();

	if (self_ports.empty()) return;
	timeval st;
	st.tv_sec = 5;
	st.tv_usec = 0;

	fd_set socks;
	FD_ZERO(&socks);
	
	for (auto sok : self_ports) {
		FD_SET(sok.first, &socks);
	}
	int rs = select(0, &socks, NULL, NULL, &st);
	if (rs > 0) {
		for (int i = 0; i < rs; i++) {
			SOCKET s = socks.fd_array[i];
			char buf[1024];
			short n = recv(s, buf, 1024, 0);
			if (SOCKET_ERROR == n ||0 == n) {
				t->sendTo(self_ports[s], 0, 0);
				this->mu.lock();
				this->m_port.erase(s);
				closesocket(s);
				this->mu.unlock();
				continue;
			}
			t->sendTo(self_ports[s], buf, n);
		}

	}
	if (rs == SOCKET_ERROR) {
		std::cout << __FILE__ << __LINE__ << "接收浏览器 select 返回 SOCKET_ERROR" << std::endl;
	}


}
