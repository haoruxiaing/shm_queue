#include "shm_queue.h"

int main(int argc ,char *argv[])
{   
    shm_queue queue(4);   
    queue.init("0x1150001", 1024*1024, "queue.test", 1000);
    queue.bind_key("key.test");
    queue.test_queue();
    
    if (argc == 2 && strcmp(argv[1], "-r") == 0)
    {
        queue.run();
        queue.Execution();
    }
    if (argc == 2 && strcmp(argv[1], "-s") == 0)
    {
        int count = 0;
        char buff[2048] = {0};
        while(!queue.signal_exit())
        {
            std::string qmsg = "Q" + std::to_string(count);
            std::string kmsg = "K"+ std::to_string(count);
            memcpy(buff, qmsg.c_str(), qmsg.size());
            std::string tqmsg(buff,24+rand()%2000);
            memcpy(buff, kmsg.c_str(), qmsg.size());
            std::string tkmsg(buff,24+rand()%2000);
            if (queue.push(tqmsg)<=0)
            {
                LOG(INFO)<<"push false";
                usleep(1000);
            }
            LOG(INFO)<<"push:"<<tqmsg;
            if (queue.publish_message("key.test",tkmsg)<=0)
            {
                LOG(INFO)<<"publish_message false";
                usleep(1000);
            }
            LOG(INFO)<<"push:"<<tkmsg;
            count++;
            if (count>=10){
                break;
            }
        }
    }
    LOG(INFO)<<"main exit...";
    return 0;
}
