// File:	rpthread_t.h

// List all group member's name: Christopher Yong (cy287), Joshua Ross (jjr276)
// username of iLab: 
// iLab Server: cp.cs.rutgers.edu

#ifndef RTHREAD_T_H
#define RTHREAD_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, you have to comment the USE_RTHREAD macro */
#define USE_RTHREAD 1
#ifndef TIMESLICE
/* defined timeslice to 5 ms, feel free to change this while testing your code
 * it can be done directly in the Makefile*/
#define TIMESLICE 5
#endif

#define READY 0
#define SCHEDULED 1
#define BLOCKED 2
#define WAITING 3
#define EXITED 4
#define KILLED 5

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
	rpthread_t joinTID; //The threadID of the thread waiting to join this one
	int priority;
	int status;
	volatile int joinMutex;
	uint desiredMutex; // the Mutex it is waiting on/or have 
	ucontext_t context;
	void* exitValue;
} tcb; 


/* mutex struct definition */
typedef struct rpthread_mutex_t {
	/* add something here */
	// YOUR CODE HERE
	
	uint id; //ID of the mutex
	rpthread_t tid; // Thread ID that holds this mutex
	volatile int lock; 
	rpthread_t waitingThreadID; //Thread ID that is waiting for this mutex to be unlocked
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
	volatile int mutex;
} Queue;

typedef struct LinkedList {
	QueueNode *head;
	volatile int mutex;
} linkedList;

typedef struct schedulerNode {
	uint numberOfQueues; 
	Queue* priorityQueues; 
	char usedEntireTimeSlice;
	uint timeSlices;
} schedulerNode; 

/* Function Declarations: */

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
#define pthread_yield rpthread_yield
#endif

#endif
