#include "shm_queue.h"
#include "shm_memery.h"
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/signalfd.h>

void shm_sleep(unsigned int secs)
{
    struct timeval tval;
    tval.tv_sec=secs/1000;
    tval.tv_usec=(secs*1000)%1000000;
    select(0,NULL,NULL,NULL,&tval);
}

shm_queue::shm_queue(int num)
{
    shm_bool_exit = false;
    m_data.clear();
    m_thread_num = num;
    m_thread_list = 0;
    m_free_thread = 0;
    std::vector<int*>* sy = new std::vector<int*>;
    m_data.push_back(sy);
    if (num>0)
    {
        m_thread_list = new hdc_thread<shm_queue>[num];
        for (int i=0; i<num; i++)
        {
            m_thread_list[i].m_parent = this;
            m_thread_list[i].m_index = i+1;
            m_thread_list[i].Execution = &shm_queue::Execution;
            m_thread_list[i].set_exit(true);
            std::vector<int*>* sy = new std::vector<int*>;
            m_data.push_back(sy);
        }
    }
    m_ptr = 0;
    m_id = 0;
    m_queue.clear();
    m_exit = 99999;
}

shm_queue::~shm_queue()
{
    shm_bool_exit = true;
    for (int i=0; i<m_thread_num; i++)
    {
        do{
            if (!m_thread_list[i].exit()){
                usleep(100000);
                continue;
            }else{
                break;
            }
        }while(true);
    }

    std::map<int, int> map;
    for (unsigned int i=0; i<m_data.size(); i++)
    {
        std::vector<int*>* sy = m_data[i];
        for (unsigned int ii = 0; ii <sy->size(); ii++)
        {
            std::map<int, int>::iterator it  = map.find(*((*sy)[ii]));
            if (it == map.end()){
                map[*((*sy)[ii])] = 1; 
            }else{
                int nn = map[*((*sy)[ii])];
                map[*((*sy)[ii])] = nn+1;
                if (nn>1){
                    LOG(INFO)<<*((*sy)[ii])<<":"<<nn+1;
                }
            }
        }
    }
}

void shm_queue::test_queue()
{
    shm_test_queue((char*)m_ptr, m_queue.c_str(), m_queue.length());   
}

void shm_queue::handlermsg(const std::string & message)
{
    const char *p = message.c_str();
    p++;
    if (atol(p)%10000 == 0){
        LOG(INFO)<<"handlermsg:"<<message<<" "<<message.size();
    }
}

bool shm_queue::init(const std::string& key, unsigned int msize, const std::string &queue, unsigned int size)
{
    ShareMemery sh;
    if ( sh.Creat(key.c_str(), (size_t)msize) < 0 )
    {
        LOG(ERROR)<<"ShareMemery creat false";
        return false;
    }
    
    if (sh.GetMemPtr(key.c_str(), m_ptr)<0){
        LOG(ERROR)<<"ShareMemery get ptr false";
        return false;
    }
    
    if (!m_ptr){
        return false;
    }
    
    if (shm_init((char*)m_ptr, msize)<0){
        LOG(ERROR)<<"shm_init false";
        return false;
    }
    
    int rt = shm_queue_declare((char*)m_ptr, queue.c_str(), queue.length(), size, m_id);
    if (rt <=0 )
    {
        LOG(ERROR)<<"shm_queue_declare "<<queue<<" false:"<<shm_get_error(rt);
        return false;
    }
    
    m_queue = queue;
    
    return true;
}

int  shm_queue::push(const std::string & msg)
{
    if (!m_id || !m_ptr){
        return -1;
    }
    
    int rt = shm_queue_push((char*)m_ptr, m_queue.c_str(), m_queue.length(), m_id, msg.c_str(), msg.length());
    return rt;
}

int  shm_queue::bind_key(const std::string &key)
{
    int rt = shm_queue_bind((char*)m_ptr, m_queue.c_str(), m_queue.length(), key.c_str(), key.length());
    if (rt<0)
    {
        LOG(ERROR)<<"shm_queue_bind "<<m_queue<<" false:"<<shm_get_error(rt);
    }
    return rt;
}

int  shm_queue::publish_message(const std::string &key, std::string & msg)
{
    std::map<std::string, unsigned int>::iterator it = m_key_ids.find(key);
    if (it != m_key_ids.end())
    {
        return shm_publish_message((char*)m_ptr, key.c_str(), key.length(), m_key_ids[key], msg.c_str(), msg.length());
    }
    unsigned int id = 0;
    int rt = shm_publish_message((char*)m_ptr, key.c_str(), key.length(), id, msg.c_str(), msg.length());
    if (id>0){
        m_key_ids[key] = id;
    }
    return rt;
}

int  shm_queue::run()
{
    for (int i=0; i<m_thread_num; i++)
    {
        m_thread_list[i].Creat();
    }
    return 0;
}
void shm_queue::thread_dormant(int sfd)
{
    struct signalfd_siginfo fdsi;
    ssize_t s;
    for (;;)
    {
        s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));  
        if (s != sizeof(struct signalfd_siginfo)){
            LOG(ERROR)<<"read error";
            return;
        }
        if (fdsi.ssi_signo == SIGUSR1){
            LOG(INFO)<<"thread_dormant SIGUSR1 ok";
            return ;
        }
        if (fdsi.ssi_signo == SIGUSR2){
            LOG(INFO)<<"thread_dormant SIGUSR2 ok";
            return ;
        }
        if (fdsi.ssi_signo == SIGINT || SIGSTOP == fdsi.ssi_signo || SIGKILL == fdsi.ssi_signo)
        {
            kill(getpid(), SIGINT);
            shm_bool_exit = true;
            LOG(INFO)<<"exit ok";
            return;
        }
    }
    return ;
}

int  shm_queue::do_work(char* buff, int num, int ind)
{
    std::vector<int*>* sy = m_data[ind];
    int rt = 0;
    for (int i = 0; i<num; i++)
    {
        int  len = shm_queue_pop_cas((char*)m_ptr, m_queue.c_str(), m_queue.length(), m_id, buff, 1024*1024*4);
        if (len >0)
        {
            std::string msg(buff,len);
            LOG(INFO)<<"pop:"<<msg;
            const char *p = msg.c_str();
            p++;
            int * cnum = new int;
            *cnum = atol(p);
            sy->push_back(cnum);
            rt++;
        }
        if (len <0 && len != -110){
            rt = -1;
            break;
        }
    }
    return rt;
}

int  shm_queue::Execution(int ind)
{
    int rt = shm_register_pid((char*)m_ptr, m_queue.c_str(), m_queue.length(), getpid());
    if (rt<0){
        LOG(ERROR)<<"shm_rigister_pid "<<m_queue<<" false:"<<shm_get_error(rt);
        return false;
    }
    sigset_t mask;
    int sfd;

    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGSTOP);
    sigaddset(&mask, SIGKILL);

    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    {
        LOG(ERROR)<<"sigprocmask error";
        return -1;
    }
    sfd = signalfd(-1, &mask, 0);
    if (sfd == -1)  {
        LOG(ERROR)<<"signalfd error";
        return -1;
    }
    char * m_buff = new char[1024*1024*4];

    LOG(INFO)<<"Execution:"<<ind;
    
    bool dormant = true;
    while(!shm_bool_exit)
    {
        if (dormant){
            shm_pid_work((char*)m_ptr, m_queue.c_str(), m_queue.length(), getpid());
            del_free();
            dormant = false;
        }
        int rt = do_work(m_buff, 100, ind);
        if (rt<0){
            LOG(ERROR)<<"do_work false";
            break;
        }
        if (!rt){
            int c = shm_pid_stop((char*)m_ptr, m_queue.c_str(), m_queue.length(), getpid());
            if (c<0 && c!= -112){
                LOG(ERROR)<<"shm_pid_stop false:"<<c<<" "<<shm_get_error(c);
                break;
            }
            if (c==0){
                add_free();
                thread_dormant(sfd); //休眠
            }else{
                continue;
            }
            dormant = true;
        }
        if (50==rt && m_free_thread)
        {
            for (int i=0; i<m_free_thread; i++)
            {
                kill(getpid(), SIGUSR2);
            }
        }
    }
    delete m_buff;
    LOG(INFO)<<m_data[ind]->size();
    if (ind>0){
        m_thread_list[ind-1].set_exit(true);
    }
    return 0;
}

