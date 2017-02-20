#ifndef _SHM_MEMERY_H
#define _SHM_MEMERY_H

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#define H_IPC_IRWUG  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP

#include <string>

class ShareMemery
{
public:
    ShareMemery()
    {
        m_ptr = NULL;
    };

    ~ShareMemery()
    {
    };

    unsigned int StrToInt32( const char* str )
    {
        if (strlen(str) >= 3 )
        {
            if( str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ) )
            {
                const char * p = str;
                p += 2;
                unsigned int  iRet = 0;
                unsigned char iByte;
                for( ;p != 0; p ++)
                {
                    if( *p >= '0' && *p <= '9')
                    {
                        iByte = (unsigned char)*p - '0';
                    }
                    else if(*p >= 'a' && *p <= 'f')
                    {
                        iByte = (unsigned char)*p - 'a' + 10;
                    }
                    else if(*p >= 'A' && *p <= 'F')
                    {
                        iByte = (unsigned char)*p - 'A' + 10;
                    }
                    else
                    {
                        break;
                    }

                    iRet = (iRet << 4) | iByte;
                }
                return iRet;
            }
        }
        return (unsigned int)atoi(str);
    };

    int  Creat(const char* key_id, size_t size)
    {
        unsigned int id = StrToInt32(key_id);
        return Creat(id, size);
    };

    int  Creat(unsigned int id, size_t size)
    {
        int Memory = shmget ( id, 0, 0 );
        if (Memory < 0)
        {
            if (errno == ENOENT)
            {
                Memory = shmget (id, size, IPC_CREAT|0666);
                //Memory = shmget (id, size, IPC_CREAT|IPC_EXCL|H_IPC_IRWUG);
            }
            if (Memory < 0)
            {
                return -1;
            }
            return 0;
        }
        return 1;
    };
    
    int  GetMemPtr(const char* key_id, void* &ptr)
    {
        unsigned int id = StrToInt32(key_id);
        return GetMemPtr(id, ptr);
    };

    int  GetMemPtr(unsigned int id, void* &ptr)
    {
        int Memory = shmget ( id, 0, 0 );
        if (Memory < 0)
        {
            return -1;
        }
        ptr = shmat(Memory,0,0);
        if (ptr == (void*)-1)
        {
            return -1;
        }
        m_ptr = ptr;
        return 0;
    };

    int  Destroy(const char* key_id)
    {
        unsigned int id = StrToInt32(key_id);
        return Destroy(id);
    };

    int  Destroy(unsigned int id)
    {
        int Memory = shmget (id, 0, 0 );
        if (Memory < 0)
        {
            return -1;
        }
        void* MemPtr = shmat(Memory,0,0);
        if (MemPtr == (void*)-1)
        {
            return -1;
        }
        shmdt( (char*)MemPtr );
        shmctl( Memory, IPC_RMID, 0 );
        return 0;
    };

    int  GetMemSize(const char* key_id, size_t &size)
    {
        unsigned int id = StrToInt32(key_id);
        return GetMemSize(id, size);
    };
    
    int  GetMemSize(unsigned id, size_t &size)
    {
        int Memory = shmget ( id, 0, 0 );
        if (Memory < 0)
        {
            return -1;
        }
        struct shmid_ds buf;
        if (shmctl( Memory, IPC_STAT, &buf) == 0)
        {
            size = buf.shm_segsz;
            return 0;
        }
        return -1;
    };
    
    int Init(const char* key_id)
    {
        if (key_id != NULL)
        {
            if (GetMemPtr(key_id,m_ptr) < 0) return -1;
            if (GetMemSize(key_id,m_size) < 0) return -1;
        }
        return 0;
    };

    void*   GetPtr(){
        return m_ptr;
    };

    int     GetSize(){
        return m_size;
    };

    void    SetPtr(void* value){
        m_ptr = value;
    };

    void    SetSize(int size){
        m_size = size;
    };

private:
    void * m_ptr;
    size_t m_size;
};

#endif

