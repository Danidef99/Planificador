#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include "my_io.h"

//#include "mythread.h"
#include "interrupt.h"

#include "queue.h"

TCB* scheduler();
void activator();
void timer_interrupt(int sig);
void disk_interrupt(int sig);


/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N]; 

/* Current running thread */
static TCB* running;
static int current = 0;

struct queue *colaRR;
struct queue *colaSJF;

/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Thread control block for the idle thread */
static TCB idle;

static void idle_function()
{
  while(1);
}

void function_thread(int sec)
{
    //time_t end = time(NULL) + sec;
    while(running->remaining_ticks)
    {
      //do something
    }
    mythread_exit();
}


/* Initialize the thread library */
void init_mythreadlib() 
{
  int i;

  /* Create context for the idle thread */
  if(getcontext(&idle.run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(-1);
  }

  idle.state = IDLE;
  idle.priority = SYSTEM;
  idle.function = idle_function;
  idle.run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  idle.tid = -1;

  if(idle.run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }

  idle.run_env.uc_stack.ss_size = STACKSIZE;
  idle.run_env.uc_stack.ss_flags = 0;
  idle.ticks = QUANTUM_TICKS;
  makecontext(&idle.run_env, idle_function, 1); 

  t_state[0].state = INIT;
  t_state[0].priority = LOW_PRIORITY;
  t_state[0].ticks = QUANTUM_TICKS;

  if(getcontext(&t_state[0].run_env) == -1)
  {
    perror("*** ERROR: getcontext in init_thread_lib");
    exit(5);
  }	

  for(i=1; i<N; i++)
  {
    t_state[i].state = FREE;
  }

  t_state[0].tid = 0;
  running = &t_state[0];

  /* Initialize disk and clock interrupts */
  init_disk_interrupt();
  init_interrupt();

  colaRR = queue_new();
  colaSJF = queue_new();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */ 
int mythread_create (void (*fun_addr)(),int priority,int seconds)
{
  int i;
  
  if (!init) { init_mythreadlib(); init=1;}

  for (i=0; i<N; i++)
    if (t_state[i].state == FREE) break;

  if (i == N) return(-1);

  if(getcontext(&t_state[i].run_env) == -1)
  {
    perror("*** ERROR: getcontext in my_thread_create");
    exit(-1);
  }

  t_state[i].state = INIT;
  t_state[i].priority = priority;
  t_state[i].function = fun_addr;
  t_state[i].execution_total_ticks = seconds_to_ticks(seconds);
  t_state[i].remaining_ticks = t_state[i].execution_total_ticks;
  t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
  
  if(t_state[i].run_env.uc_stack.ss_sp == NULL)
  {
    printf("*** ERROR: thread failed to get stack space\n");
    exit(-1);
  }

  t_state[i].tid = i;
  t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
  t_state[i].run_env.uc_stack.ss_flags = 0;
  makecontext(&t_state[i].run_env, fun_addr,2,seconds);

 if(priority == HIGH_PRIORITY){
   if(running->priority == LOW_PRIORITY){ //nuevo thread alta p. y viejo baja p. lo expulsa
      running->ticks = QUANTUM_TICKS;
      disable_interrupt();
      enqueue(colaRR, running);
      enable_interrupt();
      TCB* oldRunning = running;
      running = &t_state[i];
      current = i;
      activator(oldRunning);
   }
   else if (running->remaining_ticks > t_state[i].remaining_ticks){ // nuevo y viejo alta p. , nuevo mas corto
      disable_interrupt();
      sorted_enqueue(colaSJF, running, running->remaining_ticks);
      enable_interrupt();
      TCB* oldRunning = running;
      running = &t_state[i];
      current = i;
      activator(oldRunning);
   }
   else // nuevo y viejo alta p. pero nuevo mas largo lo metemos en colaSJF
   {
     disable_interrupt();
     sorted_enqueue(colaSJF, &t_state[i], t_state[i].remaining_ticks);
     enable_interrupt();
   }
 }
 else //nuevo thread es baja prioridad lo meto en colaRR
  {
    t_state[i].ticks = QUANTUM_TICKS;
    disable_interrupt();
    enqueue(colaRR, &t_state[i]);
    enable_interrupt();
  }
  return i;
} 
/****** End my_thread_create() ******/


/* Read disk syscall */
int read_disk()
{
   return 1;
}

/* Disk interrupt  */
void disk_interrupt(int sig)
{

}


/* Free terminated thread and exits */
void mythread_exit() {
  int tid = mythread_gettid();	

  printf("*** THREAD %d FINISHED\n", tid);	
  t_state[tid].state = FINISHED;
  free(t_state[tid].run_env.uc_stack.ss_sp); 

  TCB* oldRunning = running;
  running = scheduler();
  current = running->tid;
  activator(oldRunning);    
}


void mythread_timeout(int tid) {

    printf("*** THREAD %d EJECTED\n", tid);
    t_state[tid].state = FREE;
    free(t_state[tid].run_env.uc_stack.ss_sp);

    TCB* oldRunning = running;
    running = scheduler();
    current = running->tid;
    activator(oldRunning);    
}


/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) 
{
  int tid = mythread_gettid();	
  t_state[tid].priority = priority;
  if(priority ==  HIGH_PRIORITY){
    t_state[tid].remaining_ticks = 195;
  }
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) 
{
  int tid = mythread_gettid();	
  return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
  if (!init) { init_mythreadlib(); init=1;}
  return current;
}


/* SJF para alta prioridad, RR para baja*/

TCB* scheduler()
{
  if (!queue_empty(colaSJF)){
      disable_interrupt();
      TCB* next = dequeue(colaSJF);
      enable_interrupt();
      return next;
  }
  if (!queue_empty(colaRR)){
      disable_interrupt();
      TCB* next = dequeue(colaRR);
      enable_interrupt();
      return next;
  }
  printf("*** FINISH\n");
  printf("mythread_free: No hay threads\n");
  exit(1);
}


/* Timer interrupt */
void timer_interrupt(int sig)
{
  running->remaining_ticks--;
  if(running->priority==LOW_PRIORITY){
    running->ticks--;
    if(running->ticks==0){
        running->state=INIT;
        running->ticks=QUANTUM_TICKS;
        disable_interrupt();
        enqueue(colaRR, running);
        enable_interrupt();     
        TCB* oldRunning = running; 
        running = scheduler();
        current = running->tid;
        running->state=INIT;
        activator(oldRunning);

    }
  }
} 

/* Activator */
void activator(TCB* old)
{
  if (old->state == FREE){
    printf("***   THREAD  %d TERMINATED: SETCONTEXT  OF %d\n", old->tid, current);
    setcontext (&(running->run_env));
  }
  else if(old->priority==LOW_PRIORITY && running->priority==HIGH_PRIORITY){
    printf("*** THREAD %d PREEMTED : SETCONTEXT OF %d\n", old->tid, current);  
    swapcontext(&(old->run_env), &(running->run_env));
  }
  else if (old->tid != current)
  {
    printf("*** SWAPCONTEXT FROM %d TO %d\n", old->tid, current);  
    swapcontext(&(old->run_env), &(running->run_env));
  }
  //  printf("mythread_free: After setcontext, should never get here!!...\n");		
}


