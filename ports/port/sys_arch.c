#ifndef SYS_ARCH_TIMEOUT
#define SYS_ARCH_TIMEOUT 0xffffffffUL
#endif

#ifndef SYS_MBOX_EMPTY
#define SYS_MBOX_EMPTY SYS_ARCH_TIMEOUT
#endif

#ifndef ERR_OK
#define ERR_OK 0
#endif

#ifndef ERR_MEM
#define ERR_MEM -1
#endif

#ifndef LWIP_UNUSED_ARG
#define LWIP_UNUSED_ARG(x) (void)x
#endif 

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(x) do {printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     x, __LINE__, __FILE__); fflush(NULL); abort();} while(0)
#include <stdio.h>
#include <stdlib.h>
#endif

#ifndef LWIP_ASSERT
#define LWIP_ASSERT(message, assertion) do { if (!(assertion)) { \
  LWIP_PLATFORM_ASSERT(message); }} while(0)
#endif

#include "os_ctxt.h"

simulation_context sim_ctxt;
static sc_dt::uint64 starttime;
static struct sys_thread *threads = NULL;
static sc_core::sc_mutex threads_mutex;


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thread_fn)(void *arg);
struct sys_thread;
typedef struct sys_thread* sys_thread_t;

/*multithreading APIs*/
sys_thread_t sys_thread_new(const char *name, thread_fn function, void *arg, int stacksize, int prio);
void sys_thread_join(sys_thread_t thread);

/*Semaphore APIs*/
struct sys_sem;
typedef struct sys_sem* sys_sem_t;
int8_t sys_sem_new(sys_sem_t *sem, uint8_t count);
void sys_sem_signal(sys_sem_t *s);
uint32_t sys_arch_sem_wait(sys_sem_t *s, uint32_t timeout);
void sys_sem_free(sys_sem_t *sem);
uint32_t sys_now(void);
double sys_now_in_sec(void);

void sys_init(void);

void sys_sleep(uint32_t ms); /* only has a (close to) 1 ms resolution. */

struct sys_thread {
  struct sys_thread *next;
  sc_core::sc_process_handle sc_thread;
};

/*Definition for thread-safe mailboxes*/
#define SYS_MBOX_SIZE 1280
struct sys_mbox {
  int id;
  int first, last;
  void *msgs[SYS_MBOX_SIZE];
  struct sys_sem *not_empty;
  struct sys_sem *not_full;
  struct sys_sem *mutex;
  int wait_send;
  void * ctxt;
};

#define GLOBAL_SEMS 2000
struct sys_sem {
   bool free;
   bool wait_flag;
   int id;
   unsigned int c;
   sc_core::sc_event cond;
   sc_core::sc_mutex mutex;
   void* ctxt;
   int blocking_task_id;
   int blocked_task_id;
} sems[GLOBAL_SEMS];

/*Function wrapper for OS task model*/
typedef void (*os_wrapper_fn)(const char *name, os_model_context* os_model, app_context* app_ctxt, thread_fn function, void* arg, int task_id);
void wrapper(const char *name, os_model_context* os_model, app_context* app_ctxt, thread_fn function, void *arg, int task_id){
   sim_ctxt.register_task(os_model, app_ctxt, task_id, sc_core::sc_get_current_process_handle());
   os_model->os_port->taskActivate(task_id);
   std::cout << name << " taskActivate!" << std::endl;
   function(arg);
   os_model->os_port->taskTerminate(task_id);
   std::cout << name << " taskTerminate!" << std::endl;
}

sys_thread_t sys_thread_new(const char *name, thread_fn function, void *arg, int priority, int core){
   os_model_context* os_model = sim_ctxt.get_os_ctxt(sc_core::sc_get_current_process_handle());
   app_context* app_ctxt = sim_ctxt.get_app_ctxt(sc_core::sc_get_current_process_handle());
   int child_id = os_model->os_port->taskCreate(
				sc_core::sc_gen_unique_name("child_task"), 
				HCSim::OS_RT_APERIODIC, priority, 0, 0, 
				HCSim::DEFAULT_TS, core, core);//HCSim::ALL_CORES
   std::cout << "taskCreate!" << std::endl;
   os_model->os_port->dynamicStart(core);
   std::cout << "Name is: " << name << std::endl; 
   std::cout << "ID is: " << child_id << std::endl; 
   std::cout << "dynamicStart!" << std::endl;
   os_wrapper_fn os_fn = NULL;
   os_fn = &wrapper;
   sc_core::sc_process_handle th_handle = sc_core::sc_spawn(     
                                         sc_bind(  
                                         os_fn,
                                         name, os_model, app_ctxt, function, arg, child_id 
                                         )         
                                ); 
   struct sys_thread *thread = new sys_thread;
   if (thread != NULL) {
      threads_mutex.lock();
      thread->next = threads;
      thread->sc_thread = th_handle;
      threads = thread;
      threads_mutex.unlock();
   }
   return thread;
}

void sys_thread_join(sys_thread_t thread){

}

static struct sys_sem * sys_sem_new_internal(uint8_t count)
{
   struct sys_sem *sem=NULL;
   for(int i = 0; i<GLOBAL_SEMS; i++){
      if(sems[i].free == 1){
         sem = sems + i;
         sem->id = i;
         sem->blocking_task_id = -1;
         sem->blocked_task_id = -1; 
         sem->free=0;
         sem->c = count;
	 break;
      }
   }
   return sem;
}

int8_t sys_sem_new(sys_sem_t *sem, uint8_t count)
{
   *sem = sys_sem_new_internal(count);
   if (*sem == NULL) {
      return ERR_MEM;
   }
   return ERR_OK;
}


void
sys_sem_signal(sys_sem_t *s)
{
   struct sys_sem *sem;
   sem = *s;
   int task_id = sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle());
   os_model_context* os_model = sim_ctxt.get_os_ctxt( sc_core::sc_get_current_process_handle() );
   sem->blocking_task_id = task_id;
   sem->c++;
   if (sem->c > 1) {
      sem->c = 1;
   }
   os_model->os_port->syncGlobalTime(task_id);
   if(sem->wait_flag){
      os_model->os_port->preNotify(task_id, sem->blocked_task_id);	
      sem->cond.notify();
      os_model->os_port->postNotify(task_id, sem->blocked_task_id);	
   }
}

uint32_t
sys_arch_sem_wait(sys_sem_t *s, uint32_t timeout){
   sc_dt::uint64 start_time;
   uint32_t time_needed = 0;
   struct sys_sem *sem;
   sem = *s;
   int task_id = sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle());
   os_model_context* os_model = sim_ctxt.get_os_ctxt( sc_core::sc_get_current_process_handle() );
   sem->blocked_task_id = task_id;

   while(sem->c <= 0){
      os_model->os_port->syncGlobalTime(task_id);
      os_model->os_port->preWait(task_id, sem->blocking_task_id);	
      if(sem->c > 0){
         os_model->os_port->postWait(task_id);
         break;
      }
      sem->wait_flag = true; 
      if (timeout > 0) {
         start_time = sc_core::sc_time_stamp().value();
         sc_core::wait(timeout, sc_core::SC_MS, sem->cond);
         start_time = (sc_core::sc_time_stamp().value() - start_time);
         time_needed = (uint32_t)(start_time/1000000000);
         if((time_needed == timeout) && (sem->c <= 0)){
            sem->wait_flag = false; 
            //if(os_model->node_id==0) std::cout << "Time out is reached at " << sc_core::sc_time_stamp().to_seconds() << " waiting period is: " << time_needed << "\n";
            os_model->os_port->postWaitWithSWIntr(task_id);
	    return SYS_ARCH_TIMEOUT;
         }
      }else{
         sc_core::wait(sem->cond);	
      }
      sem->wait_flag = false; 
      os_model->os_port->postWait(task_id);
   }
   sem->c--;
   return 0;
}

static void sys_sem_free_internal(struct sys_sem *sem){
   sem->free=1;
   sem->wait_flag = false; 
}

void sys_sem_free(sys_sem_t *sem){
   if (sem != NULL) {
      sys_sem_free_internal(*sem);
   }
}

void sys_sleep(uint32_t milliseconds){
   os_model_context* os_model = sim_ctxt.get_os_ctxt( sc_core::sc_get_current_process_handle() );
   int task_id = sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle());
   os_model->os_port->preWait(task_id, task_id);	
   sc_core::wait(milliseconds, sc_core::SC_MS);
   os_model->os_port->postWait(task_id);	   
}

uint32_t sys_now(void){
   /*Million seconds*/
   sc_dt::uint64 msec;
   msec = (sc_core::sc_time_stamp().value()/1000000000) - starttime;
   return (uint32_t)msec;
}

double sys_now_in_sec(void){
   /*Seconds*/
   return ((double)sys_now())/1000.0;
}

uint32_t sys_jiffies(void){
  sc_dt::uint64 msec;
  msec = (sc_core::sc_time_stamp().value()/1000000000) - starttime;
  return (uint32_t)(msec*1000000);
}

/*Intialization function used*/
void sys_init(void){
   static int init = 0;
   /*Avoid the system being initialized several times*/
   starttime = (sc_dt::uint64) (sc_core::sc_time_stamp().value()/1000000000);
   int i = 0;
   if(init == 0){
      for(i = 0; i<GLOBAL_SEMS; i++){
         sems[i].free = 1;
         sems[i].wait_flag = false;
      }
      init = 1;
   }
}

int8_t sys_mbox_new(struct sys_mbox **mb, int size){
  int task_id = sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle());
  
  struct sys_mbox *mbox;
  LWIP_UNUSED_ARG(size);
  mbox = new sys_mbox;

  if (mbox == NULL) {
    return ERR_MEM;
  }
  mbox->id = task_id;
  //mbox->ctxt = ctxt;
  mbox->first = mbox->last = 0;
  mbox->not_empty = sys_sem_new_internal(0);
  mbox->not_full = sys_sem_new_internal(0);
  mbox->mutex = sys_sem_new_internal(1);
  mbox->wait_send = 0;
  *mb = mbox;

  return ERR_OK;
}

void sys_mbox_free(struct sys_mbox **mb){
  if ((mb != NULL) && (*mb != NULL)) {
    struct sys_mbox *mbox = *mb;
    sys_sem_free_internal(mbox->not_empty);
    sys_sem_free_internal(mbox->not_full);
    sys_sem_free_internal(mbox->mutex);
    mbox->not_empty = mbox->not_full = mbox->mutex = NULL;
    delete mbox;
  }
}

int8_t
sys_mbox_trypost(struct sys_mbox **mb, void *msg){
  uint8_t first;
  struct sys_mbox *mbox;
  LWIP_ASSERT("invalid mbox", (mb != NULL) && (*mb != NULL));
  mbox = *mb;
  if ((mbox->last + 1) >= (mbox->first + SYS_MBOX_SIZE)) {
    printf("Returning with memory error!\n");
    return ERR_MEM;
  }
  mbox->msgs[mbox->last % SYS_MBOX_SIZE] = msg;
  if (mbox->last == mbox->first) {
    first = 1;
  } else {
    first = 0;
  }
  mbox->last++;
  if (first) {
    sys_sem_signal(&mbox->not_empty);
  }
  return ERR_OK;
}

void
sys_mbox_post(struct sys_mbox **mb, void *msg)
{
  uint8_t first;
  struct sys_mbox *mbox;
  LWIP_ASSERT("invalid mbox", (mb != NULL) && (*mb != NULL));
  mbox = *mb;
  while ((mbox->last + 1) >= (mbox->first + SYS_MBOX_SIZE)) {
    mbox->wait_send++;
    sys_arch_sem_wait(&mbox->not_full, 0);
    mbox->wait_send--;
  }
  mbox->msgs[mbox->last % SYS_MBOX_SIZE] = msg;
  if (mbox->last == mbox->first) {
    first = 1;
  } else {
    first = 0;
  }
  mbox->last++;
  if (first) {
    sys_sem_signal(&mbox->not_empty);
  }
}

uint32_t
sys_arch_mbox_tryfetch(struct sys_mbox **mb, void **msg)
{
  struct sys_mbox *mbox;
  LWIP_ASSERT("invalid mbox", (mb != NULL) && (*mb != NULL));
  mbox = *mb;
  if (mbox->first == mbox->last) {
    return SYS_MBOX_EMPTY;
  }
  if (msg != NULL) {
    *msg = mbox->msgs[mbox->first % SYS_MBOX_SIZE];
  }

  mbox->first++;
  if (mbox->wait_send) {
    sys_sem_signal(&mbox->not_full);
  }
  return 0;
}


uint32_t
sys_arch_mbox_fetch(struct sys_mbox **mb, void **msg, uint32_t timeout)
{
  uint32_t time_needed = 0;
  struct sys_mbox *mbox;
  LWIP_ASSERT("invalid mbox", (mb != NULL) && (*mb != NULL));
  mbox = *mb;
  while (mbox->first == mbox->last) {
    if (timeout != 0) {
      time_needed = sys_arch_sem_wait(&mbox->not_empty, timeout);
      if (time_needed == SYS_ARCH_TIMEOUT) {
        return SYS_ARCH_TIMEOUT;
      }
    } else {
      sys_arch_sem_wait(&mbox->not_empty, 0);
    }
  }
  if (msg != NULL) {
    *msg = mbox->msgs[mbox->first % SYS_MBOX_SIZE];
  }
  mbox->first++;
  if (mbox->wait_send) {
    sys_sem_signal(&mbox->not_full);
  }
  return time_needed;
}


static sc_core::sc_mutex lwprot_mutex;
static sc_core::sc_process_handle lwprot_thread;
static sc_core::sc_process_handle SC_DEAD_PROCESS = sc_core::sc_process_handle();
static int lwprot_count = 0;


uint32_t
sys_arch_protect(void)
{
    /* Note that for the UNIX port, we are using a lightweight mutex, and our
     * own counter (which is locked by the mutex). The return code is not actually
     * used. */
    if (lwprot_thread != sc_core::sc_get_current_process_handle())
    {
        /* We are locking the mutex where it has not been locked before *
        * or is being locked by another thread */
        lwprot_mutex.lock();
        lwprot_thread = sc_core::sc_get_current_process_handle();
        lwprot_count = 1;
    }
    else
        /* It is already locked by THIS thread */
        lwprot_count++;
    return 0;
}
void
sys_arch_unprotect(uint32_t pval)
{
    LWIP_UNUSED_ARG(pval);
    if (lwprot_thread == sc_core::sc_get_current_process_handle())
    {
        if (--lwprot_count == 0)
        {
            lwprot_thread = SC_DEAD_PROCESS;
            lwprot_mutex.unlock();
        }
    }
}

void
sys_time_wait(char* function_name, char* input)
{
   os_model_context* os_model = sim_ctxt.get_os_ctxt( sc_core::sc_get_current_process_handle() );
   long long psec = os_model->profile->get_latency_in_psec(std::string(function_name), std::string(input));
   if(psec == 0){
      //std::cout << "In device " << os_model->node_id << " , function name:" << function_name << " with input:" << input << ", no profile data!" << std::endl;
      return;
   }
   os_model->os_port->timeWait(psec, sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle()));
   os_model->os_port->syncGlobalTime(sim_ctxt.get_task_id(sc_core::sc_get_current_process_handle()));

   double sec = os_model->profile->get_latency_in_sec(std::string(function_name), std::string(input));
   //std::cout << "In device " << os_model->node_id << " , function name:" << function_name << " with input:" << input << ", latency is:" << sec << "s" << std::endl;
}

void
record_static(char* function_name, char* input, char* record_name)
{
   os_model_context* os_model = sim_ctxt.get_os_ctxt( sc_core::sc_get_current_process_handle() );
   double sec = os_model->profile->get_latency_in_sec(std::string(function_name), std::string(input));
   //if(std::string(record_name) == "application")
   //   std::cout << "In device " << os_model->node_id << " , function name:" << function_name << " with input:" << input << ", latency is:" << sec << "s" << std::endl;

   if(os_model->node_id != (sim_ctxt.cluster)->gateway_id) {
         (sim_ctxt.result)->accumulate_edge_result(os_model->node_id, std::string(record_name), sec);
   } else {
         (sim_ctxt.result)->accumulate_gateway_result(std::string(record_name), sec);
   }


}


#ifdef __cplusplus
}
#endif



