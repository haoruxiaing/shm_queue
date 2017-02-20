#ifndef __HDC_THREAD_H
#define __HDC_THREAD_H

#define _S_LINUX__

#ifdef  _S_WINDOWS__
#include <windows.h>
#define  _THREAD_INIT(x)  InitializeCriticalSection( (LPCRITICAL_SECTION)x )
#define  _THREAD_LEAVE(x)  LeaveCriticalSection( (LPCRITICAL_SECTION)x )
#define  _THREAD_ACTION  CRITICAL_SECTION
static int _threadlock( LPCRITICAL_SECTION x)
{
    EnterCriticalSection(x);
    return 0;
};
static int _threadunlock( LPCRITICAL_SECTION x)
{
    LeaveCriticalSection(x);
    return 0;
};
#define  _THREAD_LOCK(x)  _threadlock((LPCRITICAL_SECTION)x) 
#define  _THREAD_UNLOCK(x) _threadunlock( (LPCRITICAL_SECTION)x )
#define  _THREAD_RET DWORD WINAPI
#endif

#ifdef   _S_LINUX__
#define  _THREAD_INIT(x)  pthread_mutex_init( (pthread_mutex_t*)x,NULL )
#define  _THREAD_LEAVE(x) 
#define  _THREAD_ACTION  pthread_mutex_t    
#define  _THREAD_LOCK(x)  pthread_mutex_lock( (pthread_mutex_t*)x )
#define  _THREAD_UNLOCK(x) pthread_mutex_unlock( (pthread_mutex_t*)x )
#define  _THREAD_RET void*
#define  LPVOID void*
#endif

class LOCK_INFOS
{
public:
    LOCK_INFOS()
    {
        _THREAD_INIT(&mutex);
    };
    ~LOCK_INFOS()
    {
        _THREAD_LEAVE(&mutex);
    };
    _THREAD_ACTION mutex;
    unsigned int  LockPID;  
    time_t  LockTime;
};  //锁

//接口锁
class FUNCTION_LOCK                
{
public:
    FUNCTION_LOCK( _THREAD_ACTION* sk, unsigned int pid = 0 )
    {
        mlock = sk;
         _THREAD_LOCK( mlock);
    }
    FUNCTION_LOCK( LOCK_INFOS* sk, unsigned int pid = 0 )
    {
        if (sk != NULL)
        {
            sk->LockPID = pid;
            sk->LockTime = time(0);
            mlock = &(sk->mutex) ;
             _THREAD_LOCK( mlock);
        }
    };
    ~FUNCTION_LOCK()
    {
        if (mlock != NULL)
        {
            _THREAD_UNLOCK( mlock );
        }
    };
private:
    _THREAD_ACTION * mlock;
};

#ifdef  _S_WINDOWS__
class HrxTLockx_Event
{
public:
    HrxTLockx_Event( HANDLE sk,int ms )
    {
        mlock = sk;
        m_ms = ms;
        m_islock = false;
    };
    int Lock()
    {
        if (mlock == NULL) return -1;
        DWORD kk = WaitForSingleObject( mlock, m_ms );
        if (kk == WAIT_OBJECT_0)
        {
            m_islock = true;
            return 0;              //
        }
        if ( kk == WAIT_ABANDONED )
        {
            return -78;            //错
        }
        if ( kk == WAIT_TIMEOUT )
        {
            return -68;            //超时
        }
        return -1;
    };
    ~HrxTLockx_Event()
    {
        if( m_islock == true && mlock != NULL ) SetEvent( mlock );
    };
private:
    HANDLE mlock;
    int    m_ms;
    bool   m_islock;
};
#endif
#define  H_THREAD_STACKSIZE    1024*1024*16  //50M
//线程函数： 线程属性（是否绑定、是否是分离线程、堆栈地址、堆栈大小、优先级）
//默认属性： 非绑定、非分离线程、缺省1M的堆栈、与父进程一样优先级

template<class T>
class hdc_thread
{
public:
    hdc_thread()
    {
        m_is_exit = 0;
        Execution = 0;
        m_parent = NULL;
    #ifdef  _S_WINDOWS__
        m_Handle = 0;
    #endif
    };
    ~hdc_thread()
    {
    #ifdef  _S_WINDOWS__
        CloseHandle(m_Handle);
    #endif
    };
    int Creat(int size = 0)
    {
#ifdef  _S_WINDOWS__
        if( m_Handle == NULL )
        {
            if( size != 0 )
            {
                int ns = 1024*1024*size;
                m_Handle = ::CreateThread(NULL,ns,&ThreadProc,this,0,NULL);
            }
            else
            {
                m_Handle = ::CreateThread(NULL,H_THREAD_STACKSIZE,&ThreadProc,this,0,NULL);
            }
        }
        if( m_Handle != NULL ) return 0;
#endif
#ifdef _S_LINUX__
        pthread_t tid = 0;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr,PTHREAD_SCOPE_PROCESS);
        if( size != 0 )
        {
            pthread_attr_setstacksize(&attr, 1024*1024*size);
        }
        else
        {
            pthread_attr_setstacksize(&attr, H_THREAD_STACKSIZE);
        }
        int ret = pthread_create(&tid, &attr, ThreadProc, this);
        if(ret != 0) return -1;
        ret = pthread_detach(tid);  //设置分离线程
        return 0;
#endif
        return -1;
    }
    void  procfun()
    {
        if (Execution != 0 && m_parent != NULL )
        {
            ((T*)m_parent->*Execution)(m_index); //执行回调函数
        }
        m_is_exit = 1;
    };
    bool exit(){ return m_is_exit; };
    int  (T::*Execution)(int);
    void*  m_parent;
    int    m_index;
    void set_exit(bool exit){m_is_exit = exit;}
private:
#ifdef  _S_WINDOWS__
    HANDLE    m_Handle;
#endif
    bool   m_is_exit;
protected:
    static _THREAD_RET ThreadProc(LPVOID lpParam)
    {
        hdc_thread<T> * pp = (hdc_thread<T> *)lpParam;
        pp->procfun();
        return 0;
    };
};

#endif

