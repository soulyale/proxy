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
// ��������: PthreadSelf
// ����˵���� ��ȡ�߳�ID��������
// �� �� �ߣ�smallErLang  
// �������ڣ�2016/04/22
// �� �� ֵ: unsigned int �����ص�ǰ�̵߳�ID
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
	1.CThreadLockStats�Ǳ����˵�ǰ�����̵߳���_mutex_usr.Lock()��_mutex_usr.UnLock()
	�Ĳ�ֵ,��¼ͬһ�߳���_mutex_usr��ʹ�������
	2.���п��ú���ΪTryLock(),TryUnLock()�����ͬһ�߳���_mutex_usr�Ѿ�Lock����TryLock()
	��������ֻ�ǽ���ʶ���������ͬһ�߳���_mutex_usr��Lock����Ϊ1ͬʱ��Ҫ�ͷ�����ʱ��
	����TryUnLock���ͷţ���Ϊ1�򽫱�ʶ�Լ���
	*/
	/************************************************************************/
	template<class K, class V>
	class CThreadLockStats
	{
	private:
		CThreadLockStats();
		~CThreadLockStats();

		//************************************  
		// ��������: TryLock
		// ����˵���� ��ֹ����
		// �� �� �ߣ�smallErLang  
		// �������ڣ�2016/04/22
		// �� �� ֵ: void  
		//************************************
		void TryLock();

		//************************************  
		// ��������: TryUnLock
		// ����˵���� ��TryLock��Ӧ���ͷ���
		// �� �� �ߣ�smallErLang  
		// �������ڣ�2016/04/22
		// �� �� ֵ: void  
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

		//��_mutex_usr.Lock������_mutex_stats�ͷź󣬷�ֹ����
		//�����зǳɶԳ��ֵ��������������ڶ��̵߳���ʱһ�����������
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

	//myiterator:�������̳б�׼������,�����������һЩ����������
	template<class K, class V>
	class myiterator : public MyMap_Type_Typename std::map<K, V>::iterator
	{
	public:
		myiterator();
		//************************************  
		// ��������: myiterator
		// ����˵���� ���ؿ������죬���㿽��ʱ��Lock,�ͷ�ʱ��UnLock��
		// �� �� �ߣ�smallErLang  
		// �������ڣ�2016/04/22
		// �� �� ֵ:   
		// ��    ��: const myiterator & val_
		//************************************
		myiterator(const myiterator& val_);
		~myiterator();

		//************************************  
		// ��������: operator=
		// ����˵���� ��ֵ��������ظ��ำֵ����������̳У���Ҫ����
		// �� �� �ߣ�smallErLang  
		// �������ڣ�2016/04/22
		// �� �� ֵ: myiterator&  
		// ��    ��: const myiterator & val_
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
	// ��������: ~myiterator
	// ����˵���� ��������ֵ�Ϳ�������ʱLock������ʱ��Ҫ�ͷ���
	// �� �� �ߣ�smallErLang  
	// �������ڣ�2016/04/22
	// �� �� ֵ:   
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
	����iteratorĬ�Ϲ��캯��û��TryLock�������ڷ��ظ�������ʱ����ʱ��ҪTryLockһ�Σ�
	��ʱ��������ʱTryUnLockһ�Σ������ص�ֵ���п���������߿�����ֵ��TryLockһ�Σ�ʹ��
	�������TryUnLockһ�Ρ��ﵽ�ɶ������ͷ�����Ŀ�ġ�
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