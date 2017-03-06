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
#define  SHM_SET_LOCK           100000000
#define  SHM_MAX_PIDS           (SHM_MAX_BLOCK_SIZE/(2*SHM_PE))

#define CAS32(ptr, val_old, val_new)({ char ret; __asm__ __volatile__("lock; cmpxchgl %2,%0; setz %1": "+m"(*ptr), "=q"(ret): "r"(val_new),"a"(val_old): "memory"); ret;})

/* lock info
 * 0 can write
 * 1 can read
 */

const char * shm_neuron_error[] = {
    "null",
    "init lock false",
    "max key len",
    "have no buff",
    "memery error",
    "queue not declare",
    "max bind count",
    "key not declare",
    "unknown error",
    "queue not init",
    "empty queue",
    "pid not register",
    "unknown error !"
};

const char * shm_get_error(int error)
{
    int i = -100 - error;
    if (i<0 && i>11){
        return shm_neuron_error[12];
    }
    return shm_neuron_error[i];
}


struct  shm_buff_node
{
    volatile unsigned int   m_index;
    unsigned int            m_msg_len;
    unsigned int            m_next_buff;
    char                    m_buff[SHM_MAX_BUFF];
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
    volatile unsigned int    m_work; //init 0 1 stop 10 start
    unsigned int    m_time;
};

/****************************************二级*****************************************/
struct  shm_cell_64
{
    unsigned int            m_version;
    unsigned int            m_creat_time;
    unsigned int            m_node_num;
    unsigned int            m_pid_num:16;
    unsigned int            m_status:16;
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

struct shm_queue_
{
    char                    m_name[32];
    unsigned int            m_creat_time;
    unsigned int            m_node_num;
    volatile unsigned int   m_set_index;
    volatile unsigned int   m_get_index;
    unsigned int            m_status:16;
    unsigned int            m_pid_num:16;
    shm_pid                 m_pids[16];
    unsigned int            m_bind_key[32];
};

struct shm_key_info
{
    char                    m_name[32];
    unsigned int            m_bind_queue[32];
};

struct shm_cell_head
{
    LOCK_INFOS      m_Lock;
    unsigned int    m_version;
    unsigned int    m_init_time;
    unsigned int    m_buff_cnt;
    unsigned int    m_free_buff_list;
    unsigned int    m_free_buff_count;
    unsigned int    m_node_list;
    shm_node_64     m_queues_list;       //queue name -> key list
    shm_node_64     m_keys_list;         //key name -> queue names
    shm_queue_      m_queues[256];
    shm_key_info    m_keys[256];
};

//index start for 1
char* shm_ptr(char* ptr, unsigned int index)
{
    if (!ptr || index<=0){
        return 0;
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
        LOG(WARNING)<<"head->m_free_buff_count:"<<head->m_free_buff_count;
        LOG(WARNING)<<"SHM_MAX_BLOCK_SIZE:"<<SHM_MAX_BLOCK_SIZE;
        LOG(WARNING)<<"SHM_MAX_NODE:"<<SHM_MAX_NODE;
        
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
    head->m_buff_cnt = head->m_free_buff_count;
    LOG(WARNING)<<"head->m_free_buff_count:"<<head->m_free_buff_count;
    LOG(WARNING)<<"SHM_MAX_BLOCK_SIZE:"<<SHM_MAX_BLOCK_SIZE;
    LOG(WARNING)<<"SHM_MAX_NODE:"<<SHM_MAX_NODE;
    
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
    LOG(WARNING)<<head->m_free_buff_list;
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
            if (0 == pe->m_neuron[pos1]){
                LOG(ERROR)<<"shm_node_declare error:"<<i<<" : "<<len;
                return 0;
            }
        }

        shm_node_64 * op = (shm_node_64*)shm_ptr(ptr, pe->m_neuron[pos1]);
        if (!op){
            LOG(ERROR)<<"shm_node_declare error:"<<pe->m_neuron[pos1]<<" :"<<i<<" : "<<len;
            return 0;
        }

        if (0 == op->m_neuron[pos2]){
            op->m_neuron[pos2] = shm_get_node(ptr);
            if (0 == op->m_neuron[pos2]){
                LOG(ERROR)<<"shm_node_declare error"<<" :"<<i<<" : "<<len;
                return 0;
            }
        }

        ls = (shm_list_64*)shm_ptr(ptr, op->m_neuron[pos2]);
        if (!ls){
            LOG(ERROR)<<"shm_node_declare error:"<<op->m_neuron[pos2]<<" :"<<i<<" : "<<len;
            return 0;
        }
        
        if (!ls->m_self_index){
            ls->m_self_index = op->m_neuron[pos2];
        }

        if (0 == ls->m_next_node && (i+1) < len){
            ls->m_next_node = shm_get_node(ptr);
            if (0 == ls->m_next_node){
                LOG(ERROR)<<"shm_node_declare error"<<" :"<<i<<" : "<<len;
                return 0;
            }
        }
        
        pe = (shm_node_64*)shm_ptr(ptr, ls->m_next_node);
        if (!pe && (i+1) < len){
            LOG(ERROR)<<"shm_node_declare error:"<<ls->m_next_node<<" :"<<i<<" : "<<len;
            return 0;
        }
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
        if (!pe && (i+1) < len ){return 0;}
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
    LOG(ERROR)<<"size shm_cell_64:"<<sizeof(shm_cell_64);
    LOG(ERROR)<<"m_version:"<<so->m_version; 
    LOG(ERROR)<<"m_self_index:"<<ls->m_self_index;
    LOG(ERROR)<<"m_creat_time:"<<so->m_creat_time;
    LOG(ERROR)<<"m_node_num:"<<so->m_node_num;
    LOG(ERROR)<<"m_set_index:"<<so->m_set_index;
    LOG(ERROR)<<"m_get_index:"<<so->m_get_index;
    shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    for (unsigned int i=0; i< so->m_node_num; i++)
    {
        LOG(ERROR)<<"i:"<<i<<" index:"<<node->m_index<<" next:"<<node->m_next_buff;
        node = (shm_buff_node*)shm_ptr(ptr, node->m_next_buff);
    }
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
    
    so->m_get_index = shm_get_buff(ptr);
    LOG(INFO)<<"so->m_get_index:"<<so->m_get_index;
    shm_buff_node * node = (shm_buff_node*)shm_ptr(ptr, so->m_get_index);
    if (!node){
        LOG(ERROR)<<"shm_get_buff false";
        return -104;
    }
    node->m_index = 0;
    node->m_next_buff = shm_get_buff(ptr);
    if (0==node->m_next_buff){
        LOG(ERROR)<<"shm_get_buff false";
        return -104;
    }
    so->m_set_index = node->m_next_buff;
    
    node = (shm_buff_node*)shm_ptr(ptr, so->m_set_index);
    if (!node){
        LOG(ERROR)<<"shm_get_buff false";
        return -104;
    }
    node->m_index = 1;

    so->m_pid_num = 0;
    for (int i=0; i<8; i++){
        so->m_pids[i].m_pid = 0;
        so->m_pids[i].m_work = 0;
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
    size++;
    for (unsigned int ii=2; ii<size; ii++)
    {
        node->m_next_buff = shm_get_buff(ptr);
        if (0==node->m_next_buff){
            LOG(ERROR)<<"no buff to decalre queue";
            return -104;
        }
        node = (shm_buff_node*)shm_ptr(ptr, node->m_next_buff);
        if (!node){
            LOG(ERROR)<<"no buff to decalre queue";
            return -104;
        }
        node->m_index = ii;
    }
    node->m_next_buff = so->m_get_index;
    return 1;
}

int shm_queue_id(char* ptr, const char* queue_name, int len, unsigned int& idx)
{
    if (len<0 ||len>=32){
        return -102; //max len
    }
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }
    shm_node_64 * pe = &(head->m_queues_list);
    shm_list_64 * ls = shm_node_find(ptr, queue_name, len, pe);
    if (!ls){
        return -105; //queue not declare
    }
    idx = ls->m_self_index;
    return 0;
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
        LOG(ERROR)<<"shm_queue_bind.shm_node_declare:"<<key_name<<" :"<<klen;
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

void shm_notify(shm_cell_64* so)
{
    int cnt = 0;
    for (int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid)
        {
            cnt++;
            unsigned int _old = so->m_pids[i].m_work;
            if (_old == 1) //work to stop
            {
                if (!__sync_bool_compare_and_swap(&(so->m_pids[i].m_work), _old, _old))
                {
                    LOG(ERROR)<<"shm_notify.__sync_bool_compare_and_swap false";
                    _old = 2;
                }
            }
            if (_old == 2 && kill(so->m_pids[i].m_pid, SIGUSR1) == -1)
            {
                if (errno == EPERM){
                    LOG(ERROR)<<"pid:"<<so->m_pids[i].m_pid<<" not have power to signal";
                }
                if (errno == ESRCH){
                    so->m_pids[i].m_pid = 0;
                }
            }
        }
        if (cnt >= so->m_pid_num){
            break;
        }
    }
}

int shm_get_cell(char* ptr, const char* queue, int qlen, unsigned int& q_id, shm_cell_64 *&so)
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
    so = (shm_cell_64*) shm_ptr(ptr, ls->m_cell_index);
    if (!so){
        return -104; //unknown error
    }
    if (so->m_version != SHM_VERSION){
        return -109; //queue not init
    }
    return 0;
}

int shm_queue_push_cas(char* ptr, const char* queue, int qlen, unsigned int& q_id, const char* data, int len)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    unsigned int num = len/SHM_MAX_BUFF + 1;
    int trycnt = 0;
    do{
        unsigned int _old = so->m_set_index;
        shm_buff_node * snode = (shm_buff_node*)shm_ptr(ptr, _old);
        unsigned int _iold = snode->m_index;
        if (_iold > SHM_SET_LOCK)
        {
            //may be set ok, so try one
            if (trycnt >= 18){
                LOG(ERROR)<<"can read so no buff";
                return -103; //can read no buff
            }
            trycnt++;
            continue;
        }
        shm_buff_node * cnode = snode;
        bool tty = false;
        for (unsigned int i=0; i< num; i++)
        {
            if ((cnode->m_index > SHM_SET_LOCK && i> 0) || (cnode->m_next_buff == so->m_get_index))
            {
                //not lock so return
                LOG(ERROR)<<"no buff i:"<<i<<" num:"<<num<<" index:"<<cnode->m_index<<" _iold:"<<_iold
                <<" set:"<<so->m_set_index<<" get:"<<so->m_get_index<<" _old:"
                <<_old<<" next:"<<cnode->m_next_buff
                <<" m_node_num:"<<so->m_node_num;
                tty = true;
                break;
            }
            if ((i+1)<num){
                cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
            }
        }
        if (tty || !__sync_bool_compare_and_swap(&(so->m_set_index), _old, cnode->m_next_buff))
        {
            //may be swap false, so try more
            if (trycnt >= 18){
                LOG(ERROR)<<"__sync_bool_compare_and_swap try 28 false";
                return -103;
            }
            trycnt++;
            continue; //lock false
        }
        unsigned int slen  = len;
        cnode = snode;
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
        if (!__sync_bool_compare_and_swap(&(snode->m_index), _iold, _iold + SHM_SET_LOCK))
        {
            LOG(ERROR)<<"__sync_bool_compare_and_swap false :"<<_iold<<" :"<<_iold + SHM_SET_LOCK;
            snode->m_index = _iold + SHM_SET_LOCK;
        }
        shm_notify(so);
        return len;
    }while(true);
    return 0;
}

int shm_queue_pop_cas(char* ptr, const char* queue, int qlen, unsigned int& q_id, char* data, int max)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    do{
        unsigned int _old = so->m_get_index;
        shm_buff_node * rnode = (shm_buff_node*)shm_ptr(ptr, _old);
        unsigned int _new = rnode->m_next_buff;
        rnode =  (shm_buff_node*)shm_ptr(ptr, rnode->m_next_buff); // move 1 indx
        if (rnode->m_msg_len > (unsigned int)max)
        {
            LOG(ERROR)<<"m_get_index:"<<_old<<" len:"<<rnode->m_msg_len<<" max:"<<max;
            return -103; // no buff
        }
        unsigned int _iold = rnode->m_index;
        if (_iold < SHM_SET_LOCK)
        {
            return -110; 
        }
        int num = rnode->m_msg_len/SHM_MAX_BUFF + 1;  // 
        shm_buff_node * cnode = rnode;
        for (int i=0; i<num; i++)
        {
            if ((i+1)<num){
                _new = cnode->m_next_buff;
                cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
            }
        }
        if (!__sync_bool_compare_and_swap(&(so->m_get_index), _old, _new))
        {
            LOG(ERROR)<<"__sync_bool_compare_and_swap try false";
            //continue;
            return -110; //lock false so no message
        }
        cnode = rnode;
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
                if (len < 0){
                    LOG(ERROR)<<"unknown error:"<<"_old:"<<_old<<"  rt:"<<rt<<" len:"<<len<<" num:"<<num;
                    return -104;
                }
                if (len >0){
                    memcpy(cp, cnode->m_buff, len);
                    len = 0;
                    cp += len;
                }
            }
            cnode = (shm_buff_node*)shm_ptr(ptr, cnode->m_next_buff);
        }
        if (!__sync_bool_compare_and_swap(&(rnode->m_index), _iold, _iold%SHM_SET_LOCK))
        {
            LOG(ERROR)<<"__sync_bool_compare_and_swap false :"<<_iold<<" :"<<_iold + SHM_SET_LOCK;
            rnode->m_index = _iold%SHM_SET_LOCK;
        }
        return rt;
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
        if (!ls){
            LOG(ERROR)<<"shm_node_find:"<<key<<" len:"<<klen<<" false";
        }
    }
    if (!ls){
        return -107; //not declare
    }
    int rt = 1;
    unsigned int cc = 0;
    for (unsigned int i=0; i<SHM_MAX_BIND_KEY; i++)
    {
        if (ls->m_node_list[i]){
            unsigned int q_id = ls->m_node_list[i];
            int rs = shm_queue_push_cas(ptr, "", 0, q_id, data, len);
            if (rs<-1){
                LOG(ERROR)<<"publish:"<<key<<":"<<q_id<<" false:"<<shm_get_error(rs);
                rt = 0;
            }
            cc++;
        }
        if (cc>= ls->m_count){
            break;
        }
    }
    return rt;
}

int shm_proce_pid(shm_cell_64* so, unsigned int pid, bool set)
{
    bool has = false;
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid && kill(so->m_pids[i].m_pid,0) != 0)
        {
            so->m_pids[i].m_pid = 0;
            so->m_pid_num--;
        }
        if (so->m_pids[i].m_pid == pid){
            so->m_pids[i].m_work = 0;
            if (!set){
                so->m_pids[i].m_pid = 0;
                so->m_pid_num--;
            }
            has = true;
        }
    }
    if (has || !set){ return 1; }

    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == 0){
            so->m_pids[i].m_pid = pid;
            so->m_pids[i].m_work = 0;
            so->m_pid_num++;
            return 1;
        }
    }
    return 0;
}

int shm_register_pid(char* ptr, unsigned int& qid,  unsigned int pid)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, "1", 1, qid, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_proce_pid(so, pid, true);
}

int shm_register_pid(char* ptr, const char* queue, int qlen,  unsigned int pid)
{
    shm_cell_64* so = 0;
    unsigned int q_id = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_proce_pid(so, pid, true);
}

int shm_unregister_pid(char* ptr, unsigned int& qid, unsigned int pid)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, "1", 1, qid, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_proce_pid(so, pid, false);
}

int shm_unregister_pid(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_64* so = 0;
    unsigned int q_id = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_proce_pid(so, pid, false);
}

int shm_pid_to_work(shm_cell_64* so, unsigned int pid)
{
    for (unsigned int i=0; i<8; i++)
    {
        if (kill(so->m_pids[i].m_pid,0) != 0)
        {
            so->m_pids[i].m_pid = 0;
            so->m_pid_num--;
        }
        if (so->m_pids[i].m_pid == pid)
        {
            so->m_pids[i].m_work = 1;
            so->m_pids[i].m_time=time(0);
            break;
        }
    }
    return 0;
}

int shm_pid_work(char* ptr, unsigned int& qid, unsigned int pid)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, "1", 1, qid, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_pid_to_work(so, pid);
}

int shm_pid_work(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_64* so = 0;
    unsigned int q_id = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_pid_to_work(so, pid);
}

int shm_pid_to_stop(shm_cell_64* so, unsigned int pid)
{
    bool has = false;
    for (unsigned int i=0; i<8; i++)
    {
        if (so->m_pids[i].m_pid == pid){
            has = true;
            unsigned int _old  = so->m_pids[i].m_work;
            if (_old == 1){
                if (!__sync_bool_compare_and_swap(&(so->m_pids[i].m_work), _old, 2))
                {
                    LOG(ERROR)<<"shm_pid_to_stop.__sync_bool_compare_and_swap false";
                    return 0;
                }
            }
        }
    }
    if (!has){
        return -111;
    }
    return 1; //stop ok
}

int shm_pid_stop(char* ptr, unsigned int& qid, unsigned int pid)
{
    shm_cell_64* so = 0;
    int rt = shm_get_cell(ptr, "1",  1, qid, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_pid_to_stop(so, pid);
}

int shm_pid_stop(char* ptr, const char* queue, int qlen, unsigned int pid)
{
    shm_cell_64* so = 0;
    unsigned int q_id = 0;
    int rt = shm_get_cell(ptr, queue, qlen, q_id, so);
    if (rt<0)
    {
        return rt;
    }
    return shm_pid_to_stop(so, pid);
}
/*
struct shm_queue_info
{
    char     m_name[32];
    char     m_bind_key[12][32];
    unsigned int m_node_cnt;
    unsigned int m_use_node;
};

struct shm_info
{
    unsigned int m_node_cnt;
    unsigned int m_use_node;
    shm_queue_info m_queue[256];
};

int shm_get_queue_info(char* ptr, shm_cell_head* h, shm_queue_info * v)
{
    char name[32] = {0};
    for (int i=0; i<16; i++)
    {
        h->m_queues_list.m_neuron
    }
}

int shm_get_info(char* ptr, shm_info& info)
{
    shm_cell_head * head = shm_check_head(ptr);
    if (!head){return -1; }
    FUNCTION_LOCK slock(&(head->m_Lock));
    info.m_node_cnt = head->m_buff_cnt;
    info.m_use_node = head->m_buff_cnt- head->m_queues_list;
    for (int i=0; i<256; i++)
    {
        if (shm_get_queue_info(ptr, head, &(info.m_queue[i])<0)
        {
            break;
        }
    }
    return 0;
}*/
