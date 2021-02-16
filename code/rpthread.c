// File:	rpthread.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "rpthread.h"

// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define THREAD_STACK_SIZE 1024*64
#define MAX_PRIORITY 10;
rpthread_t threadID = 0;
rQueue* runQueue = NULL;
tcb* scheduler = NULL;
/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

   // create Thread Control Block
   // create and initialize the context of this thread
   // allocate space of stack for this thread to run
   // after everything is all set, push this thread int
   // YOUR CODE HERE
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	threadControlBlock->id = threadID++;
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	threadControlBlock->runtime = 0;
	ucontext_t threadContext = malloc(sizeof(uncontext_t) * 1);
	getcontext(&threadContext);
	threadContext.uc_link = scheduler;
	threadContext.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
	threadContext.uc_stack.ss_size = THREAD_STACK_SIZE;
	threadcontext.uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
	if(arg != NULL){
		makecontext(&threadContext, function, 1, arg);
	}else{
		makecontext(&threadContext, function, 0);
	}
	
   	threadControlBlock->context = threadContext;
   	threadControlBlock->stack = threadContext.uc_stack;
   	if(scheduler == NULL){
   		scheduler = threadControlBlock;
   	} else {
   		enqueue(threadControlBlock);
   	}
   	return 0;
};

void initialize() {
	runQueue = malloc(sizeof(rQueue) * 1); 
	runQueue->size = 0;
	runQueue->head = NULL;
	runQueue->tail = NULL;
}

void enqueue(tcb* threadControlBlock) {
	if (runQueue == NULL) {
		initialize();
	}
	rQueueNode* newNode = malloc(sizeof(rQueue) * 1);
   	newNode->node = threadControlBlock;
   	newNode->next = NULL;
   	if (runQueue->head == NULL) {
   		runQueue->head = runQueue->tail = newNode;
   	} else {
   		 runQueue->tail->next = newNode;
   		 runQueue->tail = new Node;
   	}
   	runQueue->size++;
	return 0;
}

tcb* dequeue() {
	if(rQueue == NULL || rQueue->head == NULL){
		return NULL;
	}
	tcb* popped = rQueue->head->node;
	rQueueNode* temp = rQueue->head;
	rQueue->head = rQueue->head->next;
	rQueue->size--;
	free(temp);
	return popped;
}

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context

	// YOUR CODE HERE
	tcb* current;
	current->status = READY;
	swapcontext(&(current->context), &(scheduler->context));
	enqueue(current);
	return 0;
};

/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	
	// YOUR CODE HERE
	if (value_ptr != NULL) {
		*((char*)value_ptr) = ...; //Need to set to return value but how?
	}
	current->status = READY;
	free(current->uc_stack.ss_sp);
	free(current->context);
	free(current);
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	if(runQueue == NULL) return 0;
	rQueueNode* current = runQueue->head;
	while(current->id != thread) {
		current = current->next;
	}
	if(current == NULL) return 0;
	while(runningThread != thread) {
		
	}
	void* returnValue = *((char*)(value_ptr));
	return rpthread_exit(value_ptr);;
};

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//initialize data structures for this mutex

	// YOUR CODE HERE
	return 0;
};

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex) {
        // use the built-in test-and-set atomic function to test the mutex
        // if the mutex is acquired successfully, enter the critical section
        // if acquiring mutex fails, push current thread into block list and //  
        // context switch to the scheduler thread

        // YOUR CODE HERE
        return 0;
};

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	return 0;
};


/* destroy the mutex */
int rpthread_mutex_destroy(rpthread_mutex_t *mutex) {
	// Deallocate dynamic memory created in rpthread_mutex_init

	return 0;
};

/* scheduler */
static void schedule() {
	// Every time when timer interrup happens, your thread library 
	// should be contexted switched from thread context to this 
	// schedule function

	// Invoke different actual scheduling algorithms
	// according to policy (RR or MLFQ)

	// if (sched == RR)
	//		sched_rr();
	// else if (sched == MLFQ)
	// 		sched_mlfq();

	// YOUR CODE HERE
	if(scheduler == NULL){
		rpthread_create(&threadID, NULL, schedule, NULL);
	}
	if(sched == RR) {
		sched_rr();
	} else if (sched == MLFQ) {
		sched_mlfq();
	}
// schedule policy
#ifndef MLFQ
	// Choose RR
#else 
	// Choose MLFQ
#endif

}

/* Round Robin (RR) scheduling algorithm */
static void sched_rr() {
	// Your own implementation of RR
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// Your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
}

// Feel free to add any other functions you need

// YOUR CODE HERE

