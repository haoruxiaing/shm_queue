#include "hdc_neuron_mem.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "hdc_thread.h"
#include <signal.h>

#define  SHM_PE                 sizeof(unsigned int)
#define  SHM_MAX_BLOCK_SIZE     512
#define  SHM_MAX_NODE           (SHM_PE*16)
#define  SHM_MAX_PE             (SHM_MAX_BLOCK_SIZE/SHM_MAX_NODE)
#define  SHM_MAX_BUFF           (SHM_MAX_BLOCK_SIZE - (3*SHM_PE))
#define  SHM_MAX_MEMERY         (512*1024*1024)
#define  SHM_MAX_BIND_KEY       (SHM_MAX_NODE/SHM_PE - 4)
#define  SHM_SET_LOCK  100000000
#define  SHM_MAX_PIDS           (SHM_MAX_BLOCK_SIZE/(2*SHM_PE))

#define CAS32(ptr, val_old, val_new)({ char ret; __asm__ __volatile__("lock; cmpxchgl %2,%0; setz %1": "+m"(*ptr), "=q"(ret): "r"(val_new),"a"(val_old): "memory"); ret;})


const char * shm_neuron_error[] = {
    "null",
    "init lock false",
    "max key len",
    "have no buff",
    "memery error",
    "queue not declare",
    "max bind count",
    "key not declare",
    "",
    "queue not init",
    "empty queue",
    "pid not register"
};

const char * shm_get_error(int error)
{
    int i = -100 - error;
    if (i<0 && i>9){
        return shm_neuron_error[10];
    }
    return shm_neuron_error[i];
}


struct  shm_buff_node
{
    unsigned int    m_index;
    unsigned int    m_msg_len;
    unsigned int    m_next_buff;
    char            m_buff[SHM_MAX_BUFF];
};

struct  shm_list_64
{
    unsigned int    m_count;
    unsigned int    m_next_node;
    unsigned int    m_self_index;
    unsigned int    m_cell_index;
    unsigned int    m_node_list[SHM_MAX_BIND_KEY]; //queue list or key list
};

struct  shm_pid
{
    unsigned int    m_pid;
    unsigned int    m_work;
    unsigned int    m_time;
};

/****************************************二级*****************************************/
struct  shm_cell_64
{
    unsigned int            m_version;
    unsigned int            m_creat_time;
    unsigned int            m_node_num;
    unsigned int            m_status;
    volatile unsigned int   m_set_index;
    volatile unsigned int   m_get_index;
    LOCK_INFOS              m_Lock;
    shm_pid                 m_pids[8];
};

/***************************************一级******************************************/
struct  shm_node_64
{
    unsigned int    m_neuron[16];
};

#define  SHM_VERSION   1151986

struct shm_cell_head
{
    LOCK_INFOS      m_Lock;
    unsigned int    m_version;
    unsigned int    m_init_time;
    unsigned int    m_free_buff_list;
    unsigned int    m_free_buff_count;
    unsigned int    m_node_list;
    shm_node_64     m_queues_list;       //queue name -> key list
    shm_node_64     m_keys_list;         //key name -> queue names
};

//index start for 1
char* shm_ptr(char* ptr, unsigned int index)
{
    if (!ptr || index<=0){
        return 0;
    }
    if (index >=SHM_SET_LOCK){
        index -= SHM_SET_LOCK;
    }
    index--;
    unsigned int mv = sizeof(shm_cell_head) + SHM_MAX_NODE*index;
    ptr+=mv;
    return ptr;
}

unsigned int shm_get_node(char* ptr)
{
    if (!ptr) {return 0;}
    shm_cell_head * head = (shm_cell_head*)ptr;
    if (0==head->m_node_list && head->m_free_buff_count)
    {
        unsigned int old = head->m_free_buff_list;
        shm_buff_node* pe = (shm_buff_node*)shm_ptr(ptr, head->m_free_buff_list);
        head->m_free_buff_count--;
        head->m_node_list = old;
        head->m_free_buff_list = pe->m_next_buff;
        char *p =(char*)pe;
        for (unsigned int i=0; i<SHM_MAX_PE; i++)
        {
            shm_list_64 * n = (shm_list_64*)p;
            n->m_count = 0;
            n->m_next_node = 0;
            n->m_self_index = old+i;
            n->m_cell_index = 0;
            if (i<(SHM_MAX_PE-1)){
                n->m_next_node = old+i+1;
            }
            p+=SHM_MAX_NODE;
        }
    }
    if (0==head->m_node_list){return 0;}
    unsigned int rt = head->m_node_list;
    shm_list_64 * n = (shm_list_64*)shm_ptr(ptr, head->m_node_list);
    head->m_node_list = n->m_next_node;
    return rt;
}

unsigned int shm_get_buff(char* ptr)
{
    if (!ptr) {return 0;}
    shm_cell_head * head = (shm_cell_head*)ptr;
    if (head->m_free_buff_count){
        shm_buff_node* pe = (shm_buff_node*)shm_ptr(ptr, head->m_free_buff_list);
        unsigned int index = head->m_free_buff_list;
        head->m_free_buff_list = pe->m_next_buff;
        head->m_free_buff_count--;
        return index;
    }
    return 0;
}

int shm_init(char* ptr, unsigned int size)
{
    if (!ptr || size <= sizeof(shm_cell_head)){
        return -1;
    }
    shm_cell_head * head = (shm_cell_head*)ptr;
    if (head->m_version == SHM_VERSION){
        return 0;
    }
    
#ifdef _S_LINUX__
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr,PTHREAD_PROCESS_SHARED);
#endif
#ifdef  _S_WINDOWS__
    InitializeCriticalSection(&(head->m_Lock.mutex));
#endif
#ifdef  _S_LINUX__
    if (pthread_mutex_init( &(head->m_Lock.mutex), &mutexattr ) < 0){
        LOG(ERROR)<<"pthread_mutex_init false";
        return -101;  //初始化锁失败
    }
#endif
    
    head->m_version = SHM_VERSION;
    head->m_init_time = (unsigned int)time(0);
    head->m_free_buff_list = 0;
    head->m_node_list = 0;

    size -= sizeof(shm_cell_head);
    head->m_free_buff_count = size/SHM_MAX_BLOCK_SIZE;
    LOG(INFO)<<"head->m_free_buff_count:"<<head->m_free_buff_count;
    LOG(INFO)<<"SHM_MAX_BLOCK_SIZE:"<<SHM_MAX_BLOCK_SIZE;
    LOG(INFO)<<"SHM_MAX_NODE:"<<SHM_MAX_NODE;
    
    for (unsigned int i=0; i<64; i++)
    {
        head->m_queues_list.m_neuron[i] = 0;
        head->m_keys_list.m_neuron[i] = 0;
    }
    
    shm_buff_node * prev = 0;
    for (unsigned int i=0; i<head->m_free_buff_count; i++)
    {
        if (!head->m_free_buff_list){
            head->m_free_buff_list = i*SHM_MAX_PE+1;
        }
        shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, i*SHM_MAX_PE+1);
        node->m_index = i*SHM_MAX_PE+1;
        node->m_msg_len = 0;
        node->m_next_buff = 0;
        if (prev){
            prev->m_next_buff = node->m_index;
        }
        prev = node;
    }
    LOG(INFO)<<head->m_free_buff_list;
    return 0;
}

shm_list_64* shm_node_declare(char* ptr, const char* name, int len, shm_node_64* pe)
{
    shm_list_64 * ls = 0;
    for (int i = 0; i < len; i++)
    {
        unsigned char uc = name[i];
        unsigned int  pos1 = uc/16;
        unsigned int  pos2 = uc%16;

        if (0 == pe->m_neuron[pos1]){
            pe->m_neuron[pos1] = shm_get_node(ptr);
            if (0 == pe->m_neuron[pos1]){return 0;}
        }

        shm_node_64 * op = (shm_node_64*)shm_ptr(ptr, pe->m_neuron[pos1]);
        if (!op){return 0;}

        if (0 == op->m_neuron[pos2]){
            op->m_neuron[pos2] = shm_get_node(ptr);
            if (0 == op->m_neuron[pos2]){return 0;}
        }

        ls = (shm_list_64*)shm_ptr(ptr, op->m_neuron[pos2]);
        if (!ls){return 0;}
        
        if (!ls->m_self_index){
            ls->m_self_index = op->m_neuron[pos2];
        }

        if (0 == ls->m_next_node && (i+1) < len){
            ls->m_next_node = shm_get_node(ptr);
            if (0 == ls->m_next_node){return 0;}
        }
        
        pe = (shm_node_64*)shm_ptr(ptr, ls->m_next_node);
        if (!pe){return 0;}
    }
    return ls;
}

shm_list_64* shm_node_find(char* ptr, const char* name, int len, shm_node_64* pe)
{
    shm_list_64 * ls = 0;
    for (int i = 0; i < len; i++)
    {
        unsigned char uc = name[i];
        unsigned int  pos1 = uc/16;
        unsigned int  pos2 = uc%16;

        if (0 == pe->m_neuron[pos1]){
            return 0;
        }

        shm_node_64 * op = (shm_node_64*)shm_ptr(ptr, pe->m_neuron[pos1]);
        if (!op){return 0;}

        if (0 == op->m_neuron[pos2]){
            return 0;
        }

        ls = (shm_list_64*)shm_ptr(ptr, op->m_neuron[pos2]);
        if (!ls){return 0;}
        if (0 == ls->m_next_node && (i+1) < len){
            return 0;
        }
        pe = (shm_node_64*)shm_ptr(ptr, ls->m_next_node);
        if (!pe){return 0;}
    }
    return ls;
}

shm_cell_head* shm_check_head(char* ptr)
{
    if (!ptr){
        return 0;
    }
    shm_cell_head * head = (shm_cell_head*)ptr;
    if (head->m_version != SHM_VERSION){
        return 0;
    }
    return head;
}

void shm_test_queue(char* ptr, const char* queue_name, int len)
{
    if (len<0 ||len>=32){
        LOG(ERROR)<<"queue_name len:"<<len<<" error";
        return ;
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){
        LOG(ERROR)<<"ptr null";
        return ;
    }
    shm_node_64 * pe = &(head->m_queues_list);
    shm_list_64 * ls = shm_node_find(ptr, queue_name, len, pe);
    if (!ls){
        LOG(ERROR)<<"not find queue:"<<queue_name;
        return ;
    }
    LOG(INFO)<<"m_cell_index:"<<ls->m_cell_index;
    if (0==ls->m_cell_index){
        LOG(ERROR)<<"not have cell:"<<queue_name;
        return ;
    }
    
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    LOG(INFO)<<"size shm_cell_64:"<<sizeof(shm_cell_64);
    LOG(INFO)<<"m_version:"<<so->m_version; 
    LOG(INFO)<<"m_self_index:"<<ls->m_self_index;
    LOG(INFO)<<"m_creat_time:"<<so->m_creat_time;
    LOG(INFO)<<"m_node_num:"<<so->m_node_num;
    LOG(INFO)<<"m_set_index:"<<so->m_set_index;
    LOG(INFO)<<"m_get_index:"<<so->m_get_index;
    shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    unsigned int index = node->m_index;
    int count = 0;
    for(;;)
    {
        if (node->m_msg_len)
        {
            count++;
        }
        node = (shm_buff_node*)shm_ptr(ptr, node->m_next_buff);
        if (node->m_index == index){
            break;
        }
    }
    node = (shm_buff_node*)shm_ptr(ptr, so->m_get_index);
    LOG(INFO)<<"msg_len:"<< node->m_msg_len<<" "<<count;
}

int shm_queue_declare(char* ptr, const char* queue_name, int len, unsigned int size, unsigned int& idx)
{
    if (len<0 ||len>=32){
        return -102; //max len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }
    
    FUNCTION_LOCK slock(&(head->m_Lock));

    shm_node_64 * pe = &(head->m_queues_list);
    shm_list_64 * ls = shm_node_declare(ptr, queue_name, len, pe);
    if (!ls){
        LOG(ERROR)<<"shm_node_declare false";
        return -103;
    }

    if (0==ls->m_cell_index){
        ls->m_cell_index = shm_get_buff(ptr);
        if (0==ls->m_cell_index){
            LOG(ERROR)<<"shm_get_node false";
            return -103;
        }
    }
    
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -103;
    }

    idx = ls->m_self_index;
    if (so->m_version == SHM_VERSION){
        return 1;
    }

    if (size > head->m_free_buff_count){
        LOG(ERROR)<<"shm_get_node false";
        return -103; //no buff
    }

    so->m_version = SHM_VERSION;
    so->m_creat_time = time(0);
    so->m_node_num = size;
    so->m_set_index = shm_get_buff(ptr);
    so->m_get_index = so->m_set_index;

    for (int i=0; i<8; i++){
        so->m_pids[i].m_pid=0;
        so->m_pids[i].m_work=9999;
        so->m_pids[i].m_time = 0;
    }

#ifdef _S_LINUX__
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr,PTHREAD_PROCESS_SHARED);
#endif
#ifdef  _S_WINDOWS__
    InitializeCriticalSection(&(so->m_Lock.mutex));
#endif
#ifdef  _S_LINUX__
    if (pthread_mutex_init( &(so->m_Lock.mutex), &mutexattr ) < 0){
        LOG(ERROR)<<"pthread_mutex_init false";
        return -101;  //初始化锁失败
    }
#endif
    
    shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    if (!node){return -104;}
    node->m_index = 0;
    for (unsigned int ii=1; ii<size; ii++)
    {
        node->m_next_buff = shm_get_buff(ptr);
        if (0==node->m_next_buff){return -104;}
        node = (shm_buff_node*)shm_ptr(ptr, node->m_next_buff);
        if (!node){return -104;}
        node->m_index = ii;
    }
    node->m_next_buff = so->m_set_index;
    return 1;
}

int shm_queue_bind(char* ptr, const char* queue_name, int len, const char* key_name,  int klen)
{
    if (len<0 ||len>=32 || klen < 0 || klen >= 32){
        return -102; //max len
    }

    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }
    FUNCTION_LOCK slock(&(head->m_Lock));

    shm_node_64 * pe = &(head->m_queues_list);
    shm_list_64 * ls = shm_node_find(ptr, queue_name, len, pe);
    if (!ls){
        return -105; //queue not declare
    }
    shm_node_64 * ke = &(head->m_keys_list);
    shm_list_64 * ks = shm_node_declare(ptr, key_name, klen, ke);
    if (!ks){
        return -103; //no buff
    }
    
    if (ks->m_count >= SHM_MAX_BIND_KEY || ls->m_count >= SHM_MAX_BIND_KEY){
        return -106; //max SHM_MAX_BIND_CNT
    }
    
    bool has = false;
    bool bhas =  false;
    for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
    {
        if (ls->m_node_list[i]==ks->m_self_index){
            has = true;
        }
        if (ks->m_node_list[i]==ls->m_self_index){
            bhas = true;
        }
    }
    
    //bind key
    if (!has)
    {
        for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
        {
            if (ls->m_node_list[i]==0){
                ls->m_node_list[i]=ks->m_self_index;
                ls->m_count++;
                break;
            }
        }
    }
    
    if (!bhas)
    {
        for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
        {
            if (ks->m_node_list[i]==0){
                ks->m_node_list[i]=ls->m_self_index;
                ks->m_count++;
                break;
            }
        }
    }
    return 1;
}

int shm_queue_unbind(char* ptr, const char* queue_name, int len, const char* key_name, int klen)
{
    if (len<0 ||len>=32 || klen < 0 || klen >= 32){
        return -102; //max len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }
    FUNCTION_LOCK slock(&(head->m_Lock));
    
    shm_node_64 * pe = &(head->m_queues_list);
    shm_list_64 * ls = shm_node_find(ptr, queue_name, len, pe);
    if (!ls){
        return -105; //queue not declare
    }
    
    shm_node_64 * ke = &(head->m_keys_list);
    shm_list_64 * ks = shm_node_find(ptr, key_name, klen, ke);
    if (!ks){
        return -107; //key not declare
    }
    
    //unbind key
    for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
    {
        if (ls->m_node_list[i]==ks->m_self_index){
            ls->m_node_list[i]=0;
            ls->m_count--;
            break;
        }
    }

    for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
    {
        if (ks->m_node_list[i]==ls->m_self_index){
            ks->m_node_list[i]=0;
            ks->m_count--;
            break;
        }
    }
    return 1;
}

int shm_queue_push(char* ptr, const char* queue, int qlen, unsigned int& q_id, const char* data, int len)
{
    if (qlen<0 ||qlen>=32 ||len <=0){
        return -102; //max queue len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }

    shm_list_64 * ls = 0;
    if (q_id){
        ls = (shm_list_64*)shm_ptr(ptr, q_id);
    }else{
        shm_node_64 * pe = &(head->m_queues_list);
        ls = shm_node_find(ptr, queue, qlen, pe);
    }
    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    
    unsigned int num = len/SHM_MAX_BUFF + 1;

    FUNCTION_LOCK slock(&(so->m_Lock));
    
    for (int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid && !so->m_pids[i].m_work)
        {
            if (kill(so->m_pids[i].m_pid, SIGUSR1) == -1)
            {
                if (errno == EPERM){
                    LOG(ERROR)<<"pid:"<<so->m_pids[i].m_pid<<" not have power to signal";
                }
                if (errno == ESRCH){
                    so->m_pids[i].m_pid = 0;
                }
            }
            else{
                LOG(INFO)<<so->m_pids[i].m_pid<<": "<<so->m_pids[i].m_work<<" ok";
            }
        }
    }
    
    shm_buff_node * rnode = (shm_buff_node*)shm_ptr(ptr, so->m_get_index);
    shm_buff_node * snode = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    unsigned int nnum = 0;
    
    if (snode->m_index == rnode->m_index){
        nnum = so->m_node_num -1;
    }else{
        nnum = (rnode->m_index + so->m_node_num - snode->m_index)%so->m_node_num - 1;
    }
    
    if (nnum < num){
        return -103;
    }

    unsigned int slen  = len;
    
    shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    shm_buff_node * cnode = node;
    for (unsigned int i=0; i<num; i++)
    {
        cnode->m_msg_len = len;
        if (slen > SHM_MAX_BUFF)
        {
            memcpy(cnode->m_buff, data, SHM_MAX_BUFF);
            data += SHM_MAX_BUFF;
            slen -= SHM_MAX_BUFF;
        }
        else
        {
            memcpy(cnode->m_buff, data, slen);
            break;
        }
        cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
    }
    so->m_set_index = cnode->m_next_buff;
    return len;
}

int shm_queue_pop(char* ptr, const char* queue, int qlen, unsigned int& q_id, char* data, int max)
{
    if (qlen<0 ||qlen>=32){
        return -102; //max queue len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }

    shm_list_64 * ls = 0;
    if (q_id){
        ls = (shm_list_64*)shm_ptr(ptr, q_id);
    }else{
        shm_node_64 * pe = &(head->m_queues_list);
        ls = shm_node_find(ptr, queue, qlen, pe);
    }
    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    FUNCTION_LOCK slock(&(so->m_Lock));
    
    shm_buff_node * rnode = (shm_buff_node*)shm_ptr(ptr, so->m_get_index);
    shm_buff_node * snode = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    int nnum = (snode->m_index + so->m_node_num - rnode->m_index)%so->m_node_num;
    
    if (nnum<=0){
        return 0;
    }
    shm_buff_node * cnode = rnode;
    int num = rnode->m_msg_len/SHM_MAX_BUFF + 1;
    unsigned int len = rnode->m_msg_len;
    int rt = rnode->m_msg_len;
    for (int i=0; i<num; i++)
    {
        cnode->m_msg_len = 0;
        if (len >SHM_MAX_BUFF)
        {
            memcpy(data, cnode->m_buff, SHM_MAX_BUFF);
            data+= SHM_MAX_BUFF;
            len -= SHM_MAX_BUFF;
        }
        else
        {
            memcpy(data, cnode->m_buff, len);
            break;
        }
        cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
    }
    so->m_get_index = cnode->m_next_buff;
    return rt;
}

int shm_queue_pop_cas(char* ptr, const char* queue, int qlen, unsigned int& q_id, char* data, int max)
{
    if (qlen<0 ||qlen>=32){
        return -102; //max queue len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }

    shm_list_64 * ls = 0;
    if (q_id){
        ls = (shm_list_64*)shm_ptr(ptr, q_id);
    }else{
        shm_node_64 * pe = &(head->m_queues_list);
        ls = shm_node_find(ptr, queue, qlen, pe);
    }
    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }

    unsigned int read_old = so->m_get_index;

    int rt = 0;
    do{
        if (so->m_get_index >= SHM_SET_LOCK || so->m_set_index >= SHM_SET_LOCK){
            return 0;
        }

        shm_buff_node * rnode = (shm_buff_node*)shm_ptr(ptr, so->m_get_index);
        shm_buff_node * snode = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
        int nnum = (snode->m_index + so->m_node_num - rnode->m_index)%so->m_node_num;

        if (nnum<=0){
            return -110; //no message
        }

        shm_buff_node * cnode = rnode;
        int num = rnode->m_msg_len/SHM_MAX_BUFF + 1;
        unsigned int len = rnode->m_msg_len;
        rt = len;
        char * cp = data;
        for (int i=0; i<num; i++)
        {
            if (len >SHM_MAX_BUFF)
            {
                memcpy(cp, cnode->m_buff, SHM_MAX_BUFF);
                cp += SHM_MAX_BUFF;
                len -= SHM_MAX_BUFF;
            }
            else
            {
                memcpy(cp, cnode->m_buff, len);
                break;
            }
            cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
        }
        if (!__sync_bool_compare_and_swap(&(so->m_get_index), read_old, cnode->m_next_buff)){
            return 0; //lock false
        }
        break;
    }while(true);
    return rt;
}

int shm_publish_message(char* ptr, const char* key, int klen, unsigned int& key_id, const char* data, int len)
{
    if (klen<0 ||klen>=32){
        return -102; //max queue len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }

    shm_list_64 * ls = 0;
    if (key_id){
        ls = (shm_list_64*)shm_ptr(ptr, key_id);
    }else{
        shm_node_64 * pe = &(head->m_keys_list);
        ls = shm_node_find(ptr, key, klen, pe);
    }
    if (!ls){
        return -107; //not declare
    }
    int rt = 1;
    for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
    {
        if (ls->m_node_list[i]){
            unsigned int q_id = ls->m_node_list[i];
            int rs = shm_queue_push(ptr, "", 0, q_id, data, len);
            if (rs<-1){
                //LOG(ERROR)<<"publish:"<<key<<":"<<q_id<<" false:"<<shm_get_error(rs);
                rt = 0;
            }
        }
    }
    return rt;
}

int shm_register_pid(char* ptr, const char* queue, int qlen,  unsigned int pid)
{
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1;}
    
    shm_list_64 * ls = 0;
    shm_node_64 * pe = &(head->m_queues_list);
    ls = shm_node_find(ptr, queue, qlen, pe);
    
    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    FUNCTION_LOCK slock(&(so->m_Lock));
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == pid){
            so->m_pids[i].m_work = 999;
            return 1;
        }
    }

    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == 0){
            so->m_pids[i].m_pid = pid;
            so->m_pids[i].m_work = 999;
            return 1;
        }
    }
    return 0;
}

int shm_unregister_pid(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1;}

    shm_list_64 * ls = 0;
    shm_node_64 * pe = &(head->m_queues_list);
    ls = shm_node_find(ptr, queue, qlen, pe);
    
    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    FUNCTION_LOCK slock(&(so->m_Lock));
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == pid){
            so->m_pids[i].m_pid = 0;
        }
    }
    return 0;
}

int shm_pid_work(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1;}

    shm_list_64 * ls = 0;
    shm_node_64 * pe = &(head->m_queues_list);
    ls = shm_node_find(ptr, queue, qlen, pe);

    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    FUNCTION_LOCK slock(&(so->m_Lock));
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == pid){
            if (so->m_pids[i].m_work>=999){
                so->m_pids[i].m_work = 1;
            }else
            {
                so->m_pids[i].m_work++;
            }
            so->m_pids[i].m_time=time(0);
        }
    }
    return 0;
}

int shm_pid_stop(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1;}

    shm_list_64 * ls = 0;
    shm_node_64 * pe = &(head->m_queues_list);
    ls = shm_node_find(ptr, queue, qlen, pe);

    if (!ls){
        return -105; //queue not declare
    }
    shm_cell_64 * so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    FUNCTION_LOCK slock(&(so->m_Lock));
    if (so->m_get_index != so->m_set_index){
        return 1;
    }
    bool has = false;
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == pid && time(0) - so->m_pids[i].m_time <= 1)
        {
            return -112; //stop 失败
        }
        if (so->m_pids[i].m_pid == pid){ has = true;}
        if (so->m_pids[i].m_pid == pid && so->m_pids[i].m_work>=999){
            so->m_pids[i].m_work = 0;
        }
        if (so->m_pids[i].m_pid == pid && so->m_pids[i].m_work){
            so->m_pids[i].m_work--;
        }
    }
    if (!has){
        return -111;
    }
    return 0;
}
