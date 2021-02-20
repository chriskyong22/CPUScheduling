// File:	rpthread_t.h

// List all group member's name:
// username of iLab:
// iLab Server:

#ifndef RTHREAD_T_H
#define RTHREAD_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_RTHREAD macro */
#define USE_RTHREAD 1

#ifndef TIMESLICE
/* defined timeslice to 5 ms, feel free to change this while testing your code
 * it can be done directly in the Makefile*/
#define TIMESLICE 50
#endif

#define READY 0
#define SCHEDULED 1
#define BLOCKED 2
#define WAITING 3

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

typedef uint rpthread_t;

typedef struct threadControlBlock {
	/* add important states in a thread control block */
	// thread Id
	// thread status
	// thread context
	// thread stack
	// thread priority
	// And more ...

	// YOUR CODE HERE
	rpthread_t id;
	int joinTID; //The threadID of which thread THIS thread is waiting on to join with
	int priority;
	int status;
	double runtime; 
	int desiredMutex; // the Mutex it is waiting on/or have 
	ucontext_t context;
	stack_t stack;
	void* exitValue;
} tcb; 


/* mutex struct definition */
typedef struct rpthread_mutex_t {
	/* add something here */
	// YOUR CODE HERE
	
	uint id; //ID of the mutex
	int tid; //Change to rp_thread_t later when I figure out if scheduling thread will always be 0 (if so, we can initially set it to 0 since all other threads IDs > 0)
	char lock; //Using char to save memory atm 
} rpthread_mutex_t;

/* define your data structures here: */
// Feel free to add your own auxiliary data structures (linked list or queue etc...)

// YOUR CODE HERE
typedef struct QueueNode {
	tcb* node;
	struct QueueNode* next;
} QueueNode;

typedef struct Queue {
	QueueNode* head;
	QueueNode* tail;
	int size;
} Queue;

typedef struct schedulerNode {
	uint numberOfQueues; 
	Queue* priorityQueues; 
	tcb* scheduler; //Do we even need a scheduler thread, why can't we just call the schedule function if the thread is exit/blocked/or waiting for a join?
	tcb* current; //The thread that is currently running, should be popped off the priorityQueue 
	// Since RR and MLFQ both have Exit/Join/Blocked Queues, can it just be a global variable and not be stored in this struct?
	char usedEntireTimeSlice;
	uint timeSlices;
} schedulerNode; 

/* Function Declarations: */
Queue* initializeQueue();
void initializeScheduleQueues();
tcb* initializeTCB();
tcb* initializeTCBHeaders();
void initializeScheduler();
void enqueue(Queue*, tcb* threadControlBlock);
tcb* dequeue(Queue*);
tcb* findFirstOfJoinQueue(Queue* queue, rpthread_t thread);
tcb* findFirstOfQueue(Queue* queue, rpthread_t thread);
int checkExistBlockedQueue(Queue* queue, int mutexID);
void initializeTimer();
void initializeSignalHandler();
void timer_interrupt_handler(int signum);
void startTimer();
void disableTimer();
void pauseTimer();
void resumeTimer();

static void sched_mlfq();
static void sched_rr();
static void schedule();

/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, void
    *(*function)(void*), void * arg);

/* give CPU pocession to other user level threads voluntarily */
int rpthread_yield();

/* terminate a thread */
void rpthread_exit(void *value_ptr);

/* wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr);

/* initial the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, const pthread_mutexattr_t
    *mutexattr);

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex);

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex);

/* destroy the mutex */
int rpthread_mutex_destroy(rpthread_mutex_t *mutex);

#ifdef USE_RTHREAD
#define pthread_t rpthread_t
#define pthread_mutex_t rpthread_mutex_t
#define pthread_create rpthread_create
#define pthread_exit rpthread_exit
#define pthread_join rpthread_join
#define pthread_mutex_init rpthread_mutex_init
#define pthread_mutex_lock rpthread_mutex_lock
#define pthread_mutex_unlock rpthread_mutex_unlock
#define pthread_mutex_destroy rpthread_mutex_destroy
#endif

#endif
