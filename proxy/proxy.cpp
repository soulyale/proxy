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


const char* SQUID_IP = "192.168.3.13";
const UINT  SQUID_PORT = 3128;
const char* SER_LISTENING_IP = "127.0.0.1";
const UINT  SER_LISTENING_PORT = 9971;


class Item {
public:
	Item(SOCKET re_c,SOCKET sq_p,char * adr,size_t s) {
		this->remote_client = re_c;
		this->squid_proxy = sq_p;
		this->addr = adr;
		this->size = s;
	}
	Item(const Item&aa):remote_client(aa.remote_client), squid_proxy(aa.squid_proxy), \
		addr(aa.addr),size(aa.size)
	{
		this->addr = new char[aa.size];
		memcpy(this->addr, aa.addr, aa.size);
	}
	Item(Item&& aa):remote_client(aa.remote_client),squid_proxy(aa.squid_proxy),\
		addr(aa.addr),size(aa.size)
	{
		aa.addr = NULL;
	}
	SOCKET   remote_client;
	SOCKET   squid_proxy;
	char*	 addr;
	size_t	 size;

	~Item() {
		if (this->addr != NULL) {
			free(addr);
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

class Server;

class Squid {
private:
	std::shared_ptr<threadsafe_queue<Item>> fromCtoS, fromStoC;
	std::map<SOCKET,SOCKET> sockets;//键是本机socket
	std::set<SOCKET> recycle_sockets;
	std::mutex mu;
	Server *ser;
	bool run;
	bool fromSquid_exit = false;
	bool toSquid_exit = false;
public:
	void fromSquid();
	void toSquid();
	void closeSockets(SOCKET s, bool rw=false);//默认只是添加需要关闭的socket
	Squid(Server *srv, std::shared_ptr<threadsafe_queue<Item>> ctos, std::shared_ptr<threadsafe_queue<Item>> stoc);
	~Squid();
};


class Client {
private:
	SOCKET s;
	int mask;
	bool run = true;
	Squid *squid;
	bool fromClient_exit = false;
	bool toClient_exit = false;
	std::shared_ptr<threadsafe_queue<Item>> fromCtoS, fromStoC;
public:
	Client(Squid *squid, SOCKET socket, int msk, std::shared_ptr<threadsafe_queue<Item>> ctos,\
		std::shared_ptr<threadsafe_queue<Item>> stoc);
	void fromClient();
	void toClient();
	~Client();
};


class Server {
	SOCKET listening;
	UINT squid_port;
	const char* squid_ip;
public:
	Server(const char* listen_ip, const UINT listen_port, const char* squid_ip, const UINT squid_port);
	Client* GetClient(std::shared_ptr<threadsafe_queue<Item>> a,std::shared_ptr<threadsafe_queue<Item>> b, Squid *d);
	Squid*  GetSquid(std::shared_ptr<threadsafe_queue<Item>> a, std::shared_ptr<threadsafe_queue<Item>> b);
    SOCKET	GetSquidSocket();
};

int main() {

	Server ser(SER_LISTENING_IP, SER_LISTENING_PORT, SQUID_IP, SQUID_PORT);
	while (true)
	{
		//对这些变量的清楚 
		auto fromCtoS = std::shared_ptr<threadsafe_queue<Item>>(new threadsafe_queue<Item>());
		auto fromStoC = std::shared_ptr<threadsafe_queue<Item>>(new threadsafe_queue<Item>());
		
		Squid  *s = ser.GetSquid(fromCtoS, fromStoC);
		Client *c = ser.GetClient(fromCtoS,fromStoC,s);//client 负责对所有资源进行回收
		
		
		///一个client  对应 一个  squid  
		///一个client  只有一个通信socket  而 squid 有很多
		std::thread([&ser](Client *c) {
			c->fromClient();
		}, c).detach();
			
		std::thread([&ser](Client *c) {
			c->toClient();
		}, c).detach();

		std::thread([&ser](Squid *s) {
			s->toSquid();
		}, s).detach();

		std::thread([&ser](Squid *s) {
			s->fromSquid();
		}, s).detach();
	}
	return 0;
}

/*
	Client recv(fromClient)  [fromCtoS]   Squid  send(toSquid)  

	Squid  recv(fromSquid)   [fromStoC]   Client send(toClient)
			add										remove
*/
//循环内部所有socket，向管道发送数据
void Squid::fromSquid()
{
	std::map<SOCKET, SOCKET> selfsocks;
	fd_set squidsockets;
	timeval st;
	st.tv_sec = 5;
	st.tv_usec = 0;
	while (this->run)
	{
		this->mu.lock();
		selfsocks = this->sockets; 
		this->mu.unlock();
		if (selfsocks.empty())continue;
		FD_ZERO(&squidsockets);
		for (auto sok : selfsocks) {
			FD_SET(sok.first, &squidsockets);
		}
		int rs=select(0, &squidsockets, NULL,NULL ,&st);
		if (rs > 0) {
			for (int i = 0; i < rs; i++) {
				char *buf=new char[1024];
				int n=recv(squidsockets.fd_array[i], buf, 1024, 0);
				if (n > 0) {   //(SOCKET re_c,SOCKET sq_p,char * adr,size_t s)
					std::cout << buf << std::endl;
					this->fromStoC->push(Item(selfsocks[squidsockets.fd_array[i]], squidsockets.fd_array[i], buf, n));
				}
				else {
					std::cout << __FILE__ << __LINE__ \
						<< "接收代理服务器的sock发生错误或者断开" << std::endl;
					this->mu.lock();
					this->recycle_sockets.insert(squidsockets.fd_array[i]);
					this->sockets.erase(squidsockets.fd_array[i]);
					this->mu.unlock();
					this->fromStoC->push(Item(selfsocks[squidsockets.fd_array[i]], squidsockets.fd_array[i], NULL, 0));
				}
				
			}
		}
		if (rs == SOCKET_ERROR){
			std::cout << __FILE__ << __LINE__ << "  接收代理服务器select 返回 SOCKET_ERROR" << std::endl;
		}
		
		this->mu.lock();
		for (auto nsok : this->recycle_sockets) {
			this->closeSockets(nsok);
		}
		this->recycle_sockets.clear();
		this->mu.unlock();
	}
	this->fromSquid_exit = true;

}

void Squid::toSquid()
{
	std::map<SOCKET, SOCKET> selfsocks;
	while (this->run)
	{
		//从管道接收数据
		const Item& dd = this->fromCtoS->wait_and_pop();

		//判断数据长度是否为0 0为关闭连接
		if (dd.size == 0) {
			this->mu.lock();
			this->sockets.erase(dd.squid_proxy);
			this->recycle_sockets.insert(dd.squid_proxy);
			this->mu.unlock();
		}
		else {
			//查询是否为新建立的连接
			if (dd.squid_proxy == 0) {
				SOCKET sok = this->ser->GetSquidSocket();
				if (dd.size == send(sok, dd.addr, dd.size, 0)){
					this->mu.lock();
					this->sockets.insert(std::pair<SOCKET, SOCKET>(sok, dd.remote_client));
					this->mu.unlock();
				}
				else {
					std::cout << __FILE__ << __LINE__ << "代理送数据错误1" << std::endl;
				}
			}
			else {
				this->mu.lock();
				selfsocks = this->sockets;
				this->mu.unlock();
				if (selfsocks.find(dd.squid_proxy) == selfsocks.end()) {
					this->fromStoC->push(Item(dd.remote_client,dd.squid_proxy,0,0));
					std::cout << __FILE__ << __LINE__ << "代理接收到sock不存在的数据" << std::endl;
				}
				else {
					if (dd.size != send(dd.squid_proxy, dd.addr, dd.size, 0)) {
						this->fromStoC->push(Item(dd.remote_client, dd.squid_proxy, 0, 0));
						std::cout << __FILE__ << __LINE__ << "代理送数据错误2" << std::endl;
						this->mu.lock();
						this->sockets.erase(dd.squid_proxy);
						this->recycle_sockets.insert(dd.squid_proxy);
						this->mu.unlock();
					}
				}
			}
		}
		this->mu.lock();
		this->closeSockets(0, true);
		this->mu.unlock();
	}
	this->toSquid_exit = true;
}

void Squid::closeSockets(SOCKET s, bool rw)
{
	static std::set<SOCKET> ss;
	if (rw) {
		for (auto n : ss) {
			closesocket(n);
		}
		ss.clear();
	}
	else {
		ss.insert(s);
	}
}

Squid::Squid(Server *srv, std::shared_ptr<threadsafe_queue<Item>> ctos,\
	std::shared_ptr<threadsafe_queue<Item>> stoc):run(true)
{
	this->fromCtoS = ctos;
	this->fromStoC = stoc;
	this->ser = srv;
}



Squid::~Squid()
{
	this->run = false;
	while (!(this->toSquid_exit && this->fromSquid_exit)){
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	for (auto socket : sockets) {
		closesocket(socket.first);
	}
	this->closeSockets(0, true);
}


Client::Client(Squid * squid, SOCKET socket, int msk, std::shared_ptr<threadsafe_queue<Item>> ctos, std::shared_ptr<threadsafe_queue<Item>> stoc)
{
	this->squid = squid;
	this->s = socket;
	this->mask = msk;
	this->fromCtoS = ctos;
	this->fromStoC = stoc;
}

void Client::fromClient()
{
	Header h;
	while (this->run)
	{
		if (sizeof(Header) !=recv(this->s, (char*)&h, sizeof(Header), MSG_WAITALL)) {
			std::cout << __FILE__ << __LINE__ << "与客户端连接出现问题" << std::endl;
			break;
		}
		if (h.mask != this->mask) {
			std::cout << __FILE__ << __LINE__ << "与客户端连接mask错误" << std::endl;
			continue;
		}
		h.length = ntohl(h.length);
		char*s = new char[h.length];
		if ((h.length) != recv(this->s,s,h.length, 0)) {
			std::cout << __FILE__ << __LINE__ << "与客户端连接出现问题" << std::endl;
			break;
		}
		else {
			this->fromCtoS->push(Item(h.sBrowser, h.sQuid, s, h.length));
		}
	}
	this->fromClient_exit = true;
	delete(this);
}

void Client::toClient()
{
	while (this->run)
	{
		const Item& dd=this->fromStoC->wait_and_pop();
		Header h;
		h.length =htonl(dd.size);
		h.mask = this->mask;
		h.sBrowser = dd.remote_client;
		h.sQuid = dd.squid_proxy;
		char* senda = new char[sizeof(Header) + dd.size];
		memcpy(senda, &h, sizeof(Header));
		memcpy(senda + sizeof(Header), dd.addr, dd.size);
		std::cout <<senda<< std::endl;
		if (SOCKET_ERROR == send(this->s, senda, sizeof(Header) + dd.size, 0)) {
			std::cout << __FILE__ << __LINE__ << "客户端发送数据错误" << std::endl;
			break;
		}

	}
	this->toClient_exit = true;
	this->run = false;
}

Client::~Client()
{
	this->run = false;
	while (!(this->fromClient_exit && this->toClient_exit))
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	delete(this->squid);
	closesocket(this->s);
}

Server::Server(const char * listen_ip, const UINT listen_port, \
	const char * squid_ip, const UINT squid_port)
{
	this->squid_ip = squid_ip;
	this->squid_port = squid_port;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		std::cout << "WSADATA 版本错误" << std::endl;
	}
	this->listening = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in Serveraddr;
	memset(&Serveraddr, 0, sizeof(sockaddr_in*));
	Serveraddr.sin_family = AF_INET;
	Serveraddr.sin_addr.s_addr = inet_addr(listen_ip);
	Serveraddr.sin_port = htons(listen_port);
	if (0 != bind(this->listening, (sockaddr *)&Serveraddr, sizeof(Serveraddr))) {
		std::cout << "绑定IP和端口失败" << std::endl;
	}
	if (SOCKET_ERROR == listen(this->listening, 5)) {
		std::cout << "端口监听失败" << std::endl;
	}
	else {
		std::cout << "绑定成功,开始监听  "<<listen_ip<<":"<<listen_port << std::endl;
	}

}

Client * Server::GetClient(std::shared_ptr<threadsafe_queue<Item>> ctos, \
	std::shared_ptr<threadsafe_queue<Item>> stoc, Squid * d)
{
	SOCKET cnt = accept(this->listening, NULL, 0);
	if (SOCKET_ERROR == cnt) {
		std::cout << "接收客户端连接失败" << std::endl;
		return NULL;
	}
	int ran = rand();
	if (SOCKET_ERROR == send(cnt, (char*)&ran, sizeof(int), 0)) {
		std::cout << "发送给客户端mask失败" << std::endl;
		return NULL;
	 }
	return new Client(d,cnt,ran,ctos,stoc);
}

Squid * Server::GetSquid(std::shared_ptr<threadsafe_queue<Item>> ctos,\
	std::shared_ptr<threadsafe_queue<Item>> stoc)
{
	return new Squid(this,ctos,stoc);
}

SOCKET Server::GetSquidSocket()
{
	SOCKET sk= socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	sockaddr_in m_nServeraddr;
	memset(&m_nServeraddr, 0, sizeof(m_nServeraddr));
	m_nServeraddr.sin_family = AF_INET;
	m_nServeraddr.sin_port = htons(this->squid_port);
	m_nServeraddr.sin_addr.s_addr = inet_addr(this->squid_ip);
	if(0 != connect(sk, (sockaddr*)&m_nServeraddr, sizeof(m_nServeraddr))) {
		std::cout << "连接代理服务器失败" << std::endl;
		return 0;
	}
	return sk;
}
