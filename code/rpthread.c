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
uint mutexID = 0;
Queue* runQueue = NULL;
Queue* blockedQueue = NULL;
tcb* scheduleNode = NULL; //NOT SURE WHAT TO DO HERE still
tcb* current = NULL;
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
	getcontext(&(threadControlBlock->context));
	threadContext.uc_link = scheduleNode; //Not sure if I should link to scheduler context or how this should work
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
   	if(scheduleNode == NULL){
   		scheduleNode = threadControlBlock;
   	} else {
   		enqueue(runQueue, threadControlBlock);
   	}
   	return 0;
};

void initialize(Queue* Queue) {
	Queue = malloc(sizeof(Queue) * 1); 
	Queue->size = 0;
	Queue->head = NULL;
	Queue->tail = NULL;
}

void enqueue(Queue* queue, tcb* threadControlBlock) {
	if (queue == NULL) {
		initialize();
	}
	QueueNode* newNode = malloc(sizeof(Queue) * 1);
   	newNode->node = threadControlBlock;
   	newNode->next = NULL;
   	if (queue->head == NULL) {
   		queue->head = queue->tail = newNode;
   	} else {
   		 queue->tail->next = newNode;
   		 queue->tail = new Node;
   	}
   	queue->size++;
	return 0;
}

tcb* dequeue(Queue* queue) {
	if(queue == NULL || queue->head == NULL){
		return NULL;
	}
	tcb* popped = queue->head->node;
	QueueNode* temp = queue->head;
	queue->head = queue->head->next;
	queue->size--;
	if(queue->size == 0){
		queue->tail = NULL;
	}
	free(temp);
	return popped;
}

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context

	// YOUR CODE HERE
	current->status = READY;
	swapcontext(&(current->context), &(scheduler->context));
	enqueue(runQueue, current);
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
	free(current);
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	
	//TODO, not entirely sure what to do 
	if(runQueue == NULL) return 0;
		
	void* temp = malloc(sizeof(void*) * 1);
	*((char*)(value_ptr)) = temp;
	return 0;
};

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//initialize data structures for this mutex
	// YOUR CODE HERE
	if(mutex == NULL){
		return -1;
	}
	
	//Assuming rpthread_mutex_t has already been malloced otherwise we would have to return a pointer (since we would be remallocing it)
	mutex->id = mutexID++;
	mutex->tid = -1; 
	mutex->lock = '0';
	return 0;
};

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex) {
        // use the built-in test-and-set atomic function to test the mutex
        // if the mutex is acquired successfully, enter the critical section
        // if acquiring mutex fails, push current thread into block list and //  
        // context switch to the scheduler thread

        // YOUR CODE HERE
        
        
        //Where to find the built-in test and set atomic function....?????? I found only c++ versions 
        // So I'm just going to use int as the locks even though this is not really test_and_set but w/e
       	if(mutex == NULL){
       		return -1;
       	}
       	
        if(mutex->lock == '1') {
        	//Shouldn't the swapping and setting to block be done in the caller function and not in here? 
		    current->status = BLOCKED;
		    current->desiredMutex = mutex->id;
		    enqueue(blockedQueue, current); 
		    swapcontext(&(current->context), &(scheduler->context)); 
		    //Shouldn't it call schedule now?.... what is going on?
		    
        } else if (mutex->lock == '0') {
        	mutex->lock = '1'; 
        	mutex->tid = current->id;
        	return 0;
        } else {
        	
        }
        return -1;
};

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.

	// YOUR CODE HERE
	if (mutex == NULL) {
		return -1;
	}
	
	if (mutex->lock == '1' && current->id == mutex->tid) {
		QueueNode* currentBlock = blockedQueue->head;
		while (currentBlock != NULL) {
			if (mutex->id == currentBlock->desiredMutex) {
				//TODO
				//MOVE THIS NODE OUT OF BLOCK LIST AND MOVE IT TO THE RUNNING QUEUE 
			}
		}
		mutex->lock = '0';
		return 0;
	} else if (mutex->lock == '0') {
	
	} else {
	
	}
	return -1;
};


/* destroy the mutex */
int rpthread_mutex_destroy(rpthread_mutex_t *mutex) {
	// Deallocate dynamic memory created in rpthread_mutex_init
	if(mutex == NULL){
		return -1;
	}
	// Should we be able to free mutexes that are in use or required/wanted by threads?
	free(mutex);
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
	if(sched == RR) { //Probably need to do strcmp here
		sched_rr();
	} else if (sched == MLFQ) { //Probably need to do strcmp here
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

