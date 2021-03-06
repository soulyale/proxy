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


const char*  SERVER_IP = "127.0.0.1";
const UINT   SERVER_PORT = 9971;
const char*  LOCAL_LISTENING_IP = "127.0.0.1";
const UINT   LOCAL_LISTENING_PORT = 8001;

class Tunnel
{
public:
	Tunnel();
	~Tunnel();
	void add();
	void del();
private:
	std::mutex mu;
	char num;
	std::map<SOCKET, char> tube;
};


class Item {
public:
	Item(SOCKET bro_s, SOCKET squ_s, char * msg_adr, size_t msg_size) {
		this->broser_socket = bro_s;
		this->squid_socket = squ_s;
		this->msgaddr = msg_adr;
		this->msgsize = msg_size;
	}
	Item(const Item&aa) :broser_socket(aa.broser_socket), squid_socket(aa.squid_socket), \
		msgaddr(aa.msgaddr),msgsize(aa.msgsize)
	{
		this->msgaddr = new char[aa.msgsize];
		memcpy(this->msgaddr, aa.msgaddr, aa.msgsize);
	}
	Item(Item&& aa) :broser_socket(aa.broser_socket), squid_socket(aa.squid_socket), \
		msgaddr(aa.msgaddr), msgsize(aa.msgsize)
	{
		aa.msgaddr = NULL;
	}
	SOCKET   broser_socket;
	SOCKET   squid_socket;
	char*	 msgaddr;
	size_t	 msgsize;

	~Item() {
		if (this->msgaddr != NULL) {
			free(msgaddr);
		}
	}
};


typedef struct {
#pragma pack(1)
	int  mask;  //校验码 在连接成功后 由服务器返回的随机数
	SOCKET sBrowser;//浏览器连接socket
	SOCKET sQuid;//浏览器连接socket
	UINT  length;//数据报文长度
#pragma pack()
}Header;


template<typename T>
class threadsafe_queue {
private:
	// data_queue访问信号量
	mutable std::mutex mut;
	mutable std::condition_variable data_cond;
	using queue_type = std::queue<T>;
	queue_type data_queue;
public:
	using value_type = typename queue_type::value_type;
	using container_type = typename queue_type::container_type;
	threadsafe_queue() = default;
	threadsafe_queue(const threadsafe_queue&) = delete;
	threadsafe_queue& operator=(const threadsafe_queue&) = delete;
	/*
	* 使用迭代器为参数的构造函数,适用所有容器对象
	* */
	template<typename _InputIterator>
	threadsafe_queue(_InputIterator first, _InputIterator last) {
		for (auto itor = first; itor != last; ++itor) {
			data_queue.push(*itor);
		}
	}
	explicit threadsafe_queue(const container_type &c) :data_queue(c) {}
	/*
	* 使用初始化列表为参数的构造函数
	* */
	threadsafe_queue(std::initializer_list<value_type> list) :threadsafe_queue(list.begin(), list.end()) {
	}
	/*
	* 将元素加入队列
	* */
	void push(const value_type &new_value) {
		std::lock_guard<std::mutex>lk(mut);
		data_queue.push(std::move(new_value));
		data_cond.notify_one();
	}
	/*
	* 从队列中弹出一个元素,如果队列为空就阻塞
	* */
	value_type wait_and_pop() {
		std::unique_lock<std::mutex>lk(mut);
		data_cond.wait(lk, [this] {return !this->data_queue.empty(); });
		auto value = std::move(data_queue.front());
		data_queue.pop();
		return value;
	}
	/*
	* 从队列中弹出一个元素,如果队列为空返回false
	* */
	bool try_pop(value_type& value) {
		std::lock_guard<std::mutex>lk(mut);
		if (data_queue.empty())
			return false;
		value = std::move(data_queue.front());
		data_queue.pop();
		return true;
	}
	/*
	* 返回队列是否为空
	* */
	auto empty() const->decltype(data_queue.empty()) {
		std::lock_guard<std::mutex>lk(mut);
		return data_queue.empty();
	}
	/*
	* 返回队列中元素数个
	* */
	auto size() const->decltype(data_queue.size()) {
		std::lock_guard<std::mutex>lk(mut);
		return data_queue.size();
	}
};

class Client;
class Browser{
private:
	std::shared_ptr<threadsafe_queue<Item>> fromBtoS, fromStoB;
	std::map<SOCKET, SOCKET> sockets;//键是本机socket
	std::set<SOCKET> recycle_sockets;
	std::mutex mu;
	Client *cli;
	bool run;
	bool fromBrowser_exit = false;
	bool toBrowser_exit = false;
public:
	void getSockets(SOCKET socket);
	void fromBrowser();
	void toBrowser();
	void closeSockets(SOCKET socket, bool rw = false);//默认只是添加需要关闭的socket
	Browser(std::shared_ptr<threadsafe_queue<Item>> btos,
		std::shared_ptr<threadsafe_queue<Item>> stob);
	~Browser();
};


class Server {
private:
	SOCKET s_socket;
	int mask;
	bool run = true;
	Browser *browser;
	bool fromServer_exit = false;
	bool toServer_exit = false;
	std::shared_ptr<threadsafe_queue<Item>> fromBtoS, fromStoB;
public:
	Server(SOCKET socket,int msk,std::shared_ptr<threadsafe_queue<Item>> btos, \
		std::shared_ptr<threadsafe_queue<Item>> stob);
	void fromServer();
	void toServer();
	~Server();
};


class Client {
	SOCKET listen_socket;
	UINT  server_port;
	const char*  server_ip;
	Browser *browser=NULL;
public:
	Client(const char* listen_ip, const UINT listen_port,\
		const char* server_ip, const UINT server_port);
	Server*   GetServer(std::shared_ptr<threadsafe_queue<Item>> btos, \
		std::shared_ptr<threadsafe_queue<Item>> stob);
	Browser*  GetBrowser(std::shared_ptr<threadsafe_queue<Item>> btos,\
		std::shared_ptr<threadsafe_queue<Item>> stob);
	void	  SendSocket(Browser *brows);
};

int main() {

	Client cli(LOCAL_LISTENING_IP,LOCAL_LISTENING_PORT, SERVER_IP,SERVER_PORT);
	while (true)
	{
		//对这些变量的清楚 
		auto fromBtoS = std::shared_ptr<threadsafe_queue<Item>>(new threadsafe_queue<Item>());
		auto fromStoB = std::shared_ptr<threadsafe_queue<Item>>(new threadsafe_queue<Item>());

		Browser  *b = cli.GetBrowser(fromBtoS, fromStoB);
		//Client   必须要把收到的连接socket 传递给  Browser
		cli.SendSocket(b);

	    Server *s = cli.GetServer(fromBtoS, fromStoB);//不需要对连接进行清理
			
		std::thread([](Browser *b) {
			b->fromBrowser();
		}, b).detach();

		std::thread([](Server *s) {
			s->fromServer();
		}, s).detach();

		std::thread([](Server *s) {
			s->toServer();
		}, s).detach();

		std::thread([](Browser *b) {
			b->toBrowser();
		}, b).detach();

		std::this_thread::sleep_for(std::chrono::seconds(1000000000));
	}
	return 0;
}


/*

Browser   recv(fromBrowser)       fromBtoS               send(toServer)     Server
      接收浏览器的信息发送给队列   ------->-------->     从队列取出消息发送给服务器
Browser   send(toBrowser)         fromStoB               recv(fromServer)    Server
      取出队列的消息发送给浏览器  <---------<---------    把服务器收到的消息发送给队列

	  Item:bro_s|squ_s|msg_adr|msg_size

	  packet: mask|bro_socket|squ_socket|length|data

*/


//获取浏览器的连接请求
void Browser::getSockets(SOCKET socket)
{
	this->mu.lock();
	this->sockets.insert(std::pair<SOCKET, SOCKET>(socket, 0));
	this->mu.unlock();
}

//接收浏览器的信息发送给队列
//浏览器对客户端有很多连接
void Browser::fromBrowser()
{
	std::map<SOCKET, SOCKET> selfsocks;
	fd_set brosersockets;
	timeval st;
	st.tv_sec = 5;
	st.tv_usec = 0;
	while (this->run)
	{
		this->mu.lock();
		selfsocks = this->sockets;
		this->mu.unlock();

		FD_ZERO(&brosersockets);
		if (selfsocks.empty())continue;
		for (auto sok : selfsocks) {
			FD_SET(sok.first, &brosersockets);
		}
		int rs = select(0, &brosersockets, NULL, NULL, &st);
		if (rs > 0){
			for (int i = 0; i < rs; i++) {
				char *buf=new char[1024];
				int n = recv(brosersockets.fd_array[i], buf, 1024, 0);
				if (n > 0) {   //(SOCKET re_c,SOCKET sq_p,char * adr,size_t s)
					this->fromBtoS->push(Item(brosersockets.fd_array[i],selfsocks[brosersockets.fd_array[i]], buf, n));
				}
				//接收浏览器的数据发生错误 或者连接断开
				else {
					std::cout << __FILE__ << __LINE__ \
						<< "接收浏览器的sock发生错误或者断开" << std::endl;
					this->mu.lock();
					this->recycle_sockets.insert(brosersockets.fd_array[i]);
					this->sockets.erase(brosersockets.fd_array[i]);
					this->mu.unlock();
					this->fromBtoS->push(Item(brosersockets.fd_array[i], selfsocks[brosersockets.fd_array[i]], NULL, 0));
				}

			}
		}
		if (rs == SOCKET_ERROR) {
			std::cout << __FILE__ << __LINE__ << "接收浏览器 select 返回 SOCKET_ERROR" << std::endl;
		}

		this->mu.lock();
		for (auto nsok : this->recycle_sockets) {
			this->closeSockets(nsok);
		}
		this->recycle_sockets.clear();
		this->mu.unlock();
	}
	this->fromBrowser_exit = true;
}

void Browser::toBrowser()
{
	std::map<SOCKET, SOCKET> selfsocks;
	while (this->run)
	{
		const Item& dd = this->fromStoB->wait_and_pop();
		if (dd.msgsize == 0) {//说明代理服务器关闭了连接，那么这边是否需要及时关闭与浏览器的连接呢？因为如果接收的信息来自这些关闭的连接？？？
			this->mu.lock();
			this->sockets.erase(dd.broser_socket);
			this->recycle_sockets.insert(dd.broser_socket);
			this->mu.unlock();
		}
		else {
			//把数据转发给浏览器，发送成功后或失败 再去修改sockets 数组
			if (dd.msgsize == send(dd.broser_socket, dd.msgaddr, dd.msgsize,0)) {
				//查询是否是本地建立的新连接，如果是 则改变对方的通道号码
				std::cout << __FILE__ << __LINE__ << dd.msgaddr << std::endl;
				this->mu.lock();
				if (0 == this->sockets[dd.broser_socket])
					this->sockets[dd.broser_socket] = dd.squid_socket;
				selfsocks = this->sockets;
				this->mu.unlock();
			}
			else {
				std::cout << __FILE__ << __LINE__ << "给浏览器发送数据错误" << std::endl;
				this->mu.lock();
				this->sockets.erase(dd.broser_socket);
				this->recycle_sockets.insert(dd.broser_socket);
				this->mu.unlock();
			}	
		}
	
		this->mu.lock();
		this->closeSockets(0, true);
		this->mu.unlock();
	}
}

void Browser::closeSockets(SOCKET socket, bool rw)
{
	static std::set<SOCKET> ss;
	if (rw) {
		for (auto n : ss) {
			closesocket(n);
		}
		ss.clear();
	}
	else {
		ss.insert(socket);
	}
}

Browser::Browser(std::shared_ptr<threadsafe_queue<Item>> btos, std::shared_ptr<threadsafe_queue<Item>> stob):run(true)
{
	this->fromBtoS = btos;
	this->fromStoB = stob;
}

Browser::~Browser()
{
	this->run = false;
	while (!(this->toBrowser_exit && this->fromBrowser_exit)) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	for (auto socket : sockets) {
		closesocket(socket.first);
	}
	this->closeSockets(0, true);
}


Server::Server(SOCKET socket, int msk, std::shared_ptr<threadsafe_queue<Item>> btos, std::shared_ptr<threadsafe_queue<Item>> stob)
{
	this->s_socket=socket;
	this->mask = msk;
	this->fromBtoS = btos;
	this->fromStoB = stob;
}

//接收从服务器来的消息push到队列
void Server::fromServer()
{
	Header h;
	while (this->run)
	{
		if (sizeof(Header) != recv(this->s_socket, (char*)&h, sizeof(Header), MSG_WAITALL)) {
			std::cout << __FILE__ << __LINE__ << "  与服务器连接出现问题" << std::endl;
			break;
		}
		if (h.mask != this->mask) {
			std::cout << __FILE__ << __LINE__ << "  与服务器连接mask错误" << std::endl;
			continue;
		}
		h.length = ntohl(h.length);
		char*s = new char[h.length];
		if ((h.length) != recv(this->s_socket, s, h.length, MSG_WAITALL)) {
			std::cout << __FILE__ << __LINE__ << "  与服务器连接出现问题" << std::endl;
			break;
		}
		else {
			this->fromStoB->push(Item(h.sBrowser, h.sQuid, s, h.length));
		}
	}
	this->fromServer_exit = true;
	delete(this);
}

//从队列来的消息发送给服务器
void Server::toServer()
{
	while (this->run)
	{
		const Item& dd = this->fromBtoS->wait_and_pop();
		Header h;
		h.length = htonl(dd.msgsize);
		h.mask = this->mask;
		h.sBrowser = dd.broser_socket;
		h.sQuid = dd.squid_socket;
		char* senda = new char[sizeof(Header) + dd.msgsize];
		memcpy(senda, &h, sizeof(Header));
		memcpy((senda + sizeof(Header)), dd.msgaddr, dd.msgsize);
		if (SOCKET_ERROR == send(this->s_socket, senda, (sizeof(Header)+ dd.msgsize),0)) {
			std::cout << __FILE__ << __LINE__ << "客户端发送数据错误" << std::endl;
			break;
		}
		free(senda);
	}
	this->toServer_exit = true;
	this->run = false;
}

Server::~Server()
{
	this->run = false;
	while (!(this->fromServer_exit && this->toServer_exit))
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	closesocket(this->s_socket);
}

Client::Client(const char * listen_ip, const UINT listen_port, const char * server_ip, const UINT server_port)
{
	this->server_ip = server_ip;
	this->server_port = server_port;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSADATA 版本错误" << std::endl;
	}
	this->listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	sockaddr_in Serveraddr;
	memset(&Serveraddr, 0, sizeof(sockaddr_in*));
	Serveraddr.sin_family = AF_INET;
	Serveraddr.sin_addr.s_addr = inet_addr(listen_ip);
	Serveraddr.sin_port = htons(listen_port);
	if (0 != bind(this->listen_socket, (sockaddr *)&Serveraddr, sizeof(Serveraddr))) {
		std::cout << "绑定IP和端口失败" << std::endl;
	}
	if (SOCKET_ERROR == listen(this->listen_socket, 5)) {
		std::cout << "端口监听失败" << std::endl;
	}
	else {
		std::cout << "绑定成功,开始监听  " << listen_ip << ":" << listen_port << std::endl;
	}
	//////这里是需要通知Browser有新的socket到来
}

Browser * Client::GetBrowser(std::shared_ptr<threadsafe_queue<Item>> btos, std::shared_ptr<threadsafe_queue<Item>> stob)
{
	return new Browser(btos, stob);
}

/////接收浏览器的连接请求
void Client::SendSocket(Browser * brows)
{
	std::thread([this](Browser * brows) {
		while (true) {
			SOCKET cnt = accept(this->listen_socket, NULL, 0);
			if (SOCKET_ERROR == cnt) {
				std::cout << "接收浏览器连接失败" << std::endl;
			}
			brows->getSockets(cnt);
		}
	}, brows).detach();
	
}
Server * Client::GetServer(std::shared_ptr<threadsafe_queue<Item>> btos, std::shared_ptr<threadsafe_queue<Item>> stob)
{
	SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in m_nServeraddr;
	memset(&m_nServeraddr, 0, sizeof(m_nServeraddr));
	m_nServeraddr.sin_family = AF_INET;
	m_nServeraddr.sin_port = htons(this->server_port);
	m_nServeraddr.sin_addr.s_addr = inet_addr(this->server_ip);
	if (0 != connect(sk, (sockaddr*)&m_nServeraddr, sizeof(m_nServeraddr))) {
		std::cout << "连接服务器失败" << std::endl;
		return NULL;
	}
	int mask = 0;
	if (SOCKET_ERROR == recv(sk,(char*)&mask,sizeof(int),0)) {
		return NULL;
	}
	std::cout << mask << std::endl;
	return new Server(sk, mask, btos, stob);
}

