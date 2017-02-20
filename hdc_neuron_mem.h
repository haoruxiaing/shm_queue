#ifndef _HDC_NEURON_MEM_H
#define _HDC_NEURON_MEM_H
#include <glog/logging.h>


void shm_test_queue(char* ptr, const char* queue_name, int len);

const char * shm_get_error(int error);
/*
 * result: -1 false -101 init lock false  0 ok
 */
int shm_init(char* ptr, unsigned int size);

/*
 * result: -1 false -102 max key len  -103 no buff  1 ok -104 memery error
 */
int shm_queue_declare(char* ptr, const char* queue_name, int len, unsigned int size, unsigned int& idx);

/*
 * result: -1 false -102 max key len -103 no buff  1 ok -104 memery error -105 queue not declare -106 max bind count
 */
int shm_queue_bind(char* ptr, const char* queue_name, int len, const char* key_name,  int klen);

/*
 * result: -1 false -102 max key len -105 queue not declare -107 key not declare 1 ok
 */
int shm_queue_unbind(char* ptr, const char* queue_name, int len, const char* key_name, int klen);

/*
 * result: -1 false -102 max key len -105 queue not declare -104 memery error -109 queue not init -103 no buff  >0 ok
 */
int shm_queue_push(char* ptr, const char* queue, int qlen, unsigned int& q_id, const char* data, int len);

/*
 * result: -1 false 0 no message >0 message len
 */

int shm_queue_pop_cas(char* ptr, const char* queue, int qlen, unsigned int& q_id, char* data, int max);

/*
 * result: 
 */
int shm_publish_message(char* ptr, const char* key, int klen, unsigned int& key_id, const char* data, int len);


int shm_register_pid(char* ptr, const char* queue, int qlen, unsigned int pid);

int shm_unregister_pid(char* ptr, const char* queue, int qlen, unsigned int pid);

int shm_pid_work(char* ptr, const char* queue, int qlen, unsigned int pid);

int shm_pid_stop(char* ptr, const char* queue, int qlen, unsigned int pid);

/*
int shm_set_k_v(char* ptr, const char* key, int klen, unsigned int& key_id, char* data, int len);

int shm_get_k_v(char* ptr, const char* key, int klen, unsigned int& key_id, char* data, int max);
*/
#endif
