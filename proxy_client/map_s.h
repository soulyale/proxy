#pragma once
#include <map>

#if defined  _WIN32                                                         //Windows
#include <Windows.h>
#define MyMap_CLock_Mutex_t                 HANDLE
#define MyMap_CLock_Mutex_Init(_mutex)      (_mutex = CreateSemaphore(NULL,1,1,NULL))
#define MyMap_CLock_Mutex_Lock(_mutex)      (WaitForSingleObject(_mutex, INFINITE))
#define MyMap_CLock_Mutex_UnLock(_mutex)    (ReleaseSemaphore(_mutex,1,NULL))
#define MyMap_CLock_Mutex_Destroy(_mutex)   (CloseHandle(_mutex))
#define MyMap_Declar_Typename               typename
#define MyMap_Type_Typename

#elif defined __linux                                                       //Linux
#include <pthread.h>
#define MyMap_CLock_Mutex_t                 pthread_mutex_t
#define MyMap_CLock_Mutex_Init(_mutex)      (pthread_mutex_init(&_mutex, NULL))
#define MyMap_CLock_Mutex_Lock(_mutex)      (pthread_mutex_lock(&_mutex))
#define MyMap_CLock_Mutex_UnLock(_mutex)    (pthread_mutex_unlock(&_mutex))
#define MyMap_CLock_Mutex_Destroy(_mutex)   (pthread_mutex_destroy(&_mutex))
#define MyMap_Declar_Typename 
#define MyMap_Type_Typename                 typename
#endif

//************************************  
// 函数名称: PthreadSelf
// 函数说明： 获取线程ID内联函数
// 作 成 者：smallErLang  
// 作成日期：2016/04/22
// 返 回 值: unsigned int ：返回当前线程的ID
//************************************
inline unsigned int PthreadSelf()
{
#ifdef _WIN32
	return GetCurrentThreadId();
#else
	return thread_self();
#endif
}

namespace mymap
{
	template<class K, class V>
	class map;

	template<class K, class V>
	class myiterator;

	//lock
	class CLock
	{
	public:
		CLock() { MyMap_CLock_Mutex_Init(_mutex); }
		~CLock() { MyMap_CLock_Mutex_Destroy(_mutex); }

		void Lock() { MyMap_CLock_Mutex_Lock(_mutex); }
		void UnLock() { MyMap_CLock_Mutex_UnLock(_mutex); }


	private:
		MyMap_CLock_Mutex_t _mutex;
	};

	//threadlockstats
	/************************************************************************/
	/*
	1.CThreadLockStats是保存了当前所有线程调用_mutex_usr.Lock()和_mutex_usr.UnLock()
	的差值,记录同一线程中_mutex_usr的使用情况。
	2.类中可用函数为TryLock(),TryUnLock()，如果同一线程中_mutex_usr已经Lock，则TryLock()
	不再锁，只是将标识自增；如果同一线程中_mutex_usr的Lock次数为1同时需要释放锁的时候，
	调用TryUnLock会释放，不为1则将标识自减。
	*/
	/************************************************************************/
	template<class K, class V>
	class CThreadLockStats
	{
	private:
		CThreadLockStats();
		~CThreadLockStats();

		//************************************  
		// 函数名称: TryLock
		// 函数说明： 防止重锁
		// 作 成 者：smallErLang  
		// 作成日期：2016/04/22
		// 返 回 值: void  
		//************************************
		void TryLock();

		//************************************  
		// 函数名称: TryUnLock
		// 函数说明： 与TryLock对应的释放锁
		// 作 成 者：smallErLang  
		// 作成日期：2016/04/22
		// 返 回 值: void  
		//************************************
		void TryUnLock();

	private:
		CLock _mutex_usr;

		CLock _mutex_stats;

		std::map<unsigned int, int>* _thread_lock_stats;

		friend mymap::map<K, V>;

		friend mymap::myiterator<K, V>;
	};

	template<class K, class V>
	void mymap::CThreadLockStats<K, V>::TryUnLock()
	{
		bool _isneedusrunlock = false;
		unsigned int _thread_id = PthreadSelf();

		_mutex_stats.Lock();

		if (_thread_lock_stats) {
			MyMap_Type_Typename std::map<unsigned int, int>::iterator _finditr = _thread_lock_stats->find(_thread_id);

			if (_finditr != _thread_lock_stats->end()) {

				if (_finditr->second == 1) {

					_isneedusrunlock = true;
				}

				_finditr->second -= 1;

			}
			else {

				int _new_lock_counts = 0;

				_thread_lock_stats->insert(MyMap_Type_Typename std::map<unsigned int, int>::value_type(_thread_id, _new_lock_counts));
			}
		}

		_mutex_stats.UnLock();

		if (_isneedusrunlock) {

			_mutex_usr.UnLock();
		}
	}

	template<class K, class V>
	void mymap::CThreadLockStats<K, V>::TryLock()
	{
		bool _isneedusrlock = false;
		unsigned int _thread_id = PthreadSelf();

		_mutex_stats.Lock();

		if (_thread_lock_stats) {

			MyMap_Type_Typename std::map<unsigned int, int>::iterator _finditr = _thread_lock_stats->find(_thread_id);

			if (_finditr != _thread_lock_stats->end()) {

				if (_finditr->second <= 0) {

					_finditr->second = 0;

					_isneedusrlock = true;
				}

				_finditr->second += 1;
			}
			else {

				int _new_lock_counts = 1;

				_thread_lock_stats->insert(MyMap_Type_Typename std::map<unsigned int, int>::value_type(_thread_id, _new_lock_counts));

				_isneedusrlock = true;
			}
		}

		_mutex_stats.UnLock();

		//将_mutex_usr.Lock放置于_mutex_stats释放后，防止死锁
		//函数中非成对出现的锁加上其他锁在多线程调用时一定会出现死锁
		if (_isneedusrlock) {

			_mutex_usr.Lock();
		}
	}

	template<class K, class V>
	mymap::CThreadLockStats<K, V>::~CThreadLockStats()
	{
		_mutex_stats.Lock();

		delete _thread_lock_stats;

		_thread_lock_stats = NULL;

		_mutex_stats.UnLock();
	}

	template<class K, class V>
	mymap::CThreadLockStats<K, V>::CThreadLockStats()
	{
		_thread_lock_stats = new std::map<unsigned int, int>;
	}

	//myiterator:迭代器继承标准迭代器,减少运算符和一些函数的重载
	template<class K, class V>
	class myiterator : public MyMap_Type_Typename std::map<K, V>::iterator
	{
	public:
		myiterator();
		//************************************  
		// 函数名称: myiterator
		// 函数说明： 重载拷贝构造，方便拷贝时就Lock,释放时就UnLock；
		// 作 成 者：smallErLang  
		// 作成日期：2016/04/22
		// 返 回 值:   
		// 参    数: const myiterator & val_
		//************************************
		myiterator(const myiterator& val_);
		~myiterator();

		//************************************  
		// 函数名称: operator=
		// 函数说明： 赋值运算会隐藏父类赋值函数，不会继承，需要重载
		// 作 成 者：smallErLang  
		// 作成日期：2016/04/22
		// 返 回 值: myiterator&  
		// 参    数: const myiterator & val_
		//************************************
		myiterator& operator=(const myiterator& val_);

	private:
		myiterator & operator=(const MyMap_Declar_Typename std::map<K, V>::iterator& val_);

	private:
		CThreadLockStats<K, V>* _mutex_stats;

		friend mymap::map<K, V>;
	};

	template<class K, class V>
	mymap::myiterator<K, V>& mymap::myiterator<K, V>::operator=(const mymap::myiterator<K, V>& val_)
	{
		_mutex_stats = val_._mutex_stats;

		if (_mutex_stats) {

			_mutex_stats->TryLock();
		}

		this->std::map<K, V>::iterator::operator=(val_);

		return *this;
	}

	template<class K, class V>
	mymap::myiterator<K, V>::myiterator(const myiterator& val_)
	{
		_mutex_stats = val_._mutex_stats;

		if (_mutex_stats) {

			_mutex_stats->TryLock();
		}

		this->std::map<K, V>::iterator::operator=(val_);
	}

	template<class K, class V>
	mymap::myiterator<K, V>& mymap::myiterator<K, V>::operator=(const MyMap_Declar_Typename std::map<K, V>::iterator& val_)
	{
		this->std::map<K, V>::iterator::operator=(val_);

		return *this;
	}

	//************************************  
	// 函数名称: ~myiterator
	// 函数说明： 迭代器赋值和拷贝构造时Lock，析构时需要释放锁
	// 作 成 者：smallErLang  
	// 作成日期：2016/04/22
	// 返 回 值:   
	//************************************
	template<class K, class V>
	mymap::myiterator<K, V>::~myiterator()
	{
		if (_mutex_stats) {

			_mutex_stats->TryUnLock();
		}

		_mutex_stats = NULL;
	}

	template<class K, class V>
	mymap::myiterator<K, V>::myiterator()
	{
		_mutex_stats = NULL;
	}

	//mymap
	/************************************************************************/
	/*
	由于iterator默认构造函数没有TryLock，所以在返回该类型临时变量时需要TryLock一次，
	临时变量析构时TryUnLock一次；将返回的值进行拷贝构造或者拷贝赋值会TryLock一次，使用
	完后析构TryUnLock一次。达到成对锁和释放锁的目的。
	*/
	/************************************************************************/
	template<class K, class V>
	class map : private std::map<K, V>
	{
	public:
		map();
		map(const map& val_);
		~map();

		map& operator=(const map& val_);

		typedef MyMap_Declar_Typename myiterator<K, V> iterator;

		void insert_s(const K& key_, const V& val_);
		void erase_s(const K& key_);
		void erase_s(iterator& itr_);
		iterator find_s(const K& key_);

		iterator begin_s();
		iterator end_s();

		unsigned int size_s();

	private:
		CThreadLockStats<K, V> _mutex_stats;
	};

	template<class K, class V>
	unsigned int mymap::map<K, V>::size_s()
	{
		unsigned int _size = 0;

		_mutex_stats.TryLock();

		_size = this->size();

		_mutex_stats.TryUnLock();

		return _size;
	}

	template<class K, class V>
	typename mymap::map<K, V>::iterator mymap::map<K, V>::end_s()
	{
		mymap::map<K, V>::iterator _ret;

		_ret._mutex_stats = &_mutex_stats;

		_mutex_stats.TryLock();

		_ret = this->end();

		return _ret;
	}

	template<class K, class V>
	typename mymap::map<K, V>::iterator mymap::map<K, V>::begin_s()
	{
		mymap::map<K, V>::iterator _ret;

		_ret._mutex_stats = &_mutex_stats;

		_mutex_stats.TryLock();

		_ret = this->begin();

		return _ret;
	}

	template<class K, class V>
	typename mymap::map<K, V>::iterator mymap::map<K, V>::find_s(const K& key_)
	{
		mymap::map<K, V>::iterator _ret;

		_ret._mutex_stats = &_mutex_stats;

		_mutex_stats.TryLock();

		_ret = this->find(key_);

		return _ret;
	}

	template<class K, class V>
	void mymap::map<K, V>::erase_s(typename mymap::map<K, V>::iterator& itr_)
	{
		_mutex_stats.TryLock();

		this->erase(itr_);

		_mutex_stats.TryUnLock();
	}

	template<class K, class V>
	void mymap::map<K, V>::erase_s(const K& key_)
	{
		_mutex_stats.TryLock();

		this->erase(key_);

		_mutex_stats.TryUnLock();

		return;
	}

	template<class K, class V>
	void mymap::map<K, V>::insert_s(const K& key_, const V& val_)
	{
		_mutex_stats.TryLock();

		this->insert(std::map<K, V>::value_type(key_, val_));

		_mutex_stats.TryUnLock();

		return;
	}

	template<class K, class V>
	mymap::map<K, V>& mymap::map<K, V>::operator=(const map& val_)
	{
		val_._mutex_stats.TryLock();

		this->std::map<K, V>::operator =(val_);

		val_._mutex_stats.TryUnLock();

		return *this;
	}

	template<class K, class V>
	mymap::map<K, V>::~map()
	{

	}

	template<class K, class V>
	mymap::map<K, V>::map(const mymap::map<K, V>& val_)
	{
		val_._mutex_stats.TryLock();

		this->std::map<K, V>::operator =(val_);

		val_._mutex_stats.TryUnLock();
	}

	template<class K, class V>
	mymap::map<K, V>::map()
	{

	}
}