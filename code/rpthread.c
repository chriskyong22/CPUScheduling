// File:	rpthread.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "rpthread.h"

// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define THREAD_STACK_SIZE SIGSTKSZ
#define MAX_PRIORITY 10;
rpthread_t threadID = 0;
uint mutexID = 1;
Queue* runQueue = NULL;
Queue* blockedQueue = NULL;
Queue* exitQueue = NULL;
Queue* joinQueue = NULL;
tcb* scheduler = NULL; //NOT SURE WHAT TO DO HERE still
tcb* current = NULL;
struct itimerval timer = {0};
/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// create Thread Control Block
	// create and initialize the context of this thread
	// allocate space of stack for this thread to run
	// after everything is all set, push this thread int
	// YOUR CODE HERE

	//If the scheduler has not been initialized meaning there are no threads running, should it create the scheduler now first then try to create other threads?
	if(scheduler == NULL) {
		// First time.
		tcb* mainTCB = initializeTCB();
		makecontext(&(mainTCB->context), (void*) main, 1, arg); //How do I set this thread to be the main execution? 
		scheduler = initializeTCB();
		makecontext(&(scheduler->context), (void*) schedule, 0);
		mainTCB->context.uc_link = &(scheduler->context);
		initializeSignalHandler();
		initializeTimer();
	} 
	tcb* newThreadTCB = initializeTCB();
	makecontext(&(newThreadTCB->context), (void*)function, 1, arg);
	enqueue(runQueue, newThreadTCB);

	return 0;
};

tcb* initializeTCB() {
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	
	//Initializes the attributes of the TCB.
	threadControlBlock->id = threadID++; //Probably should make threadID start 1 instead of 0 because now the joinTID's range (also mutexTID is out of range) != id range. We would treat 0 as "-1" or uninitialized because no thread should have ID 0.
	threadControlBlock->joinTID = -1;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	threadControlBlock->runtime = 0;
	threadControlBlock->desiredMutex = -1;
	threadControlBlock->exitValue = NULL;
	
	//Initializes the context of the TCB.
	getcontext(&(threadControlBlock->context));
	ucontext_t* threadContext = &(threadControlBlock->context);
	if (scheduler == NULL) {
		threadContext->uc_link = NULL;
	} else {
		threadContext->uc_link = &(scheduler->context); //Not sure if I should link to scheduler context or how this should work
	}
	threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
	threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
	threadContext->uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
	threadControlBlock->stack = threadContext->uc_stack;
	
	return threadControlBlock;
}

void initializeSignalHandler() {
	struct sigaction signalHandler = {0};
	signalHandler.sa_handler = &timer_interrupt_handler;
	sigaction(SIGPROF, &signalHandler, NULL);
}

void initializeTimer() {
	//The initial and reset values of the timer. 
	timer.it_interval.tv_sec = (TIMESLICE * 1000) / 100000;
	timer.it_interval.tv_usec = (TIMESLICE * 1000) % 100000;
	
	//How long the timer should run before outputting a SIGPROF signal. 
	// (The timer will count down from this value and once it hits 0, output a signal and reset to the IT_INTERVAL value)
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 100000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 100000;
	printf("[D]: The timer has been initialized. Time interval is %d seconds, %d microseconds.", (TIMESLICE * 1000) / 100000, (TIMESLICE * 1000) % 100000);
	
	setitimer(ITIMER_PROF, &timer, NULL);
}
 
Queue* initializeQueue() {
	Queue* queue = malloc(sizeof(Queue) * 1); 
	queue->size = 0;
	queue->head = NULL;
	queue->tail = NULL;
	return queue;
}

// Must initialize the queue via initialize() before calling this method
void enqueue(Queue* queue, tcb* threadControlBlock) {
	if (queue == NULL) {
		return;
	}
	QueueNode* newNode = malloc(sizeof(QueueNode) * 1);
   	newNode->node = threadControlBlock;
   	newNode->next = NULL;
   	if (queue->head == NULL) {
   		queue->head = queue->tail = newNode;
   	} else {
   		 queue->tail->next = newNode;
   		 queue->tail = newNode;
   	}
   	queue->size++;
}

tcb* dequeue(Queue* queue) {
	if(queue == NULL || queue->head == NULL) {
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

tcb* findFirstOfQueue(Queue* queue, rpthread_t thread) {
	if(queue == NULL || queue->head == NULL) {
		return NULL;
	}
	QueueNode* currentNode = queue->head;
	// Checking if the first node has the data
	if(currentNode->node->id == thread){
		return dequeue(queue);
	}
	// Will traverse through all the node and check except the 1st node 
	while(currentNode->next != NULL) {
		if(currentNode->next->node->id == thread) {
			tcb* threadControlBlock = currentNode->next->node;
			QueueNode* temp = currentNode->next;
			currentNode->next = currentNode->next->next;
			free(temp);
			queue->size--;
			return threadControlBlock;
		}
		currentNode = currentNode->next;
	}
	return NULL;
}

tcb* findFirstOfJoinQueue(Queue* queue, rpthread_t thread) {
	if(queue == NULL || queue->head == NULL) {
		return NULL;
	}
	QueueNode* currentNode = queue->head;
	// Checking if the first node has the data
	if(currentNode->node->joinTID == thread){
		return dequeue(queue);
	}
	// Will traverse through all the node and check except the 1st node 
	while(currentNode->next != NULL) {
		if(currentNode->next->node->joinTID == thread) {
			tcb* threadControlBlock = currentNode->next->node;
			QueueNode* temp = currentNode->next;
			currentNode->next = currentNode->next->next;
			free(temp);
			queue->size--;
			return threadControlBlock;
		}
		currentNode = currentNode->next;
	}
	return NULL;
}

int checkExistBlockedQueue(Queue* queue, int mutexID) {
	if(queue == NULL || queue->head == NULL) {
		return -1;
	}
	QueueNode* currentNode = queue->head;
	// Will traverse through all the node
	while(currentNode != NULL) {
		if(currentNode->node->desiredMutex == mutexID) {
			return 1;
		}
		currentNode = currentNode->next;
	}
	return -1;
}

void timer_interrupt_handler(int signum) {
	if(signum == SIGPROF) {
		printf("[D]: Timer interrupt happened! Switching to Scheduler via setcontext\n");
		setcontext(&(scheduler->context));
	} else {
		printf("[D]: Random Signal occurred but was not the timer, %d\n", signum);
	}
}

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context

	// YOUR CODE HERE
	current->status = READY;
	enqueue(runQueue, current);
	//TODO: STOP TIMER INTERRUPT before swapping to scheduler
	swapcontext(&(current->context), &(scheduler->context));

	return 0;
};

/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	
	// YOUR CODE HERE
	if (value_ptr != NULL) {
		current->exitValue = value_ptr; //Need to set to return value but how?
	} //If the return value is not set, can we assume we can completely just free this thread and no other thread will try to join on this thread?
	
	//Currently there's a 1:1 relationship between exit and join, every exit thread must have a thread that joins on it...is this correct?
	free(current->context.uc_stack.ss_sp);
	tcb* joinThread = findFirstOfJoinQueue(joinQueue, current->id);
	if (joinThread != NULL) {
		joinThread->joinTID = -1;
		joinThread->exitValue = current->exitValue;
		enqueue(runQueue, joinThread);
		//Can we free current now?
		free(current);
	} else {
		enqueue(exitQueue, current);
	}
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	
	//According to how PTHREAD works, threads cannot join on the same thread meaning once a thread is joined on, the joined thread's pthread struct should be freed and before that should return the retval to the thread that is doing the joinning  
	//Join threads are taken off the run queue because it wastes run time and should only resume when if the thread exits right? Therefore Two cases: Thread Exits then another thread attempts to join or threads attempts to join then thread exits? Therefore we have to check the exit queue in here to see if the thread has already exit and check in the exit, if there is a thread waiting to join on it?
	// What happens if a thread attempts to join but the thread does not exist or already exitted, should it just be stuck forever on the join queue?
	tcb* exitThread = findFirstOfQueue(exitQueue, thread);
	if(exitThread == NULL){
		current->status = WAITING;
		current->joinTID = thread;
		enqueue(joinQueue, current); //Should it be on its own seperate queue (joinQueue) or in BLOCKEDQUEUE? If seperate queue then there will be less time to search since BLOCKEDQUEUE has the threads that are waiting on a mutex while this is waiting on a certain thread. 
	} else {
		current->status = READY;
		current->joinTID = -1;	
		*value_ptr = exitThread->exitValue; //Apparently you can deference void** (but it will only store void*, if you attempt to store anything else it will be a complier warning)
		free(exitThread);
		enqueue(runQueue, current);
	}
	
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
        
        
        //Where to find the built-in test and set atomic function....?????? I found only c++ versions is it __sync_test_and_set?
        // So I'm just going to use int as the locks even though this is not really test_and_set but w/e
       	if(mutex == NULL){
       		return -1;
       	}
       	
        if(mutex->lock == '1') {
		    current->status = BLOCKED;
		    current->desiredMutex = mutex->id;
		    enqueue(blockedQueue, current); 
		    swapcontext(&(current->context), &(scheduler->context)); 
		    //Shouldn't it call schedule now?
		    
        } else if (mutex->lock == '0') {
        	mutex->lock = '1'; 
        	mutex->tid = current->id;
        	current->desiredMutex = mutex->id; // Might be able to remove this instruction since technically this thread has this mutex and therefore to unlock the mutex, we can check the tid of the mutex to see if the thread can unlock it.
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
	
	//Checking if the thread has the mutex so other threads cannot just unlock mutexes they do not have 
	if (mutex->lock == '1' && current->id == mutex->tid) {
		QueueNode* currentBlock = blockedQueue->head;
		int maxthreads = blockedQueue->size; 
		for (int index = 0; index < maxthreads; index++) {
			tcb* currentThread = dequeue(blockedQueue);
			//Means this thread requires this mutex, and now the mutex is free, can be removed from the block list and placed on the runQueue
			if (currentThread->desiredMutex == mutex->id) {
				currentThread->status = READY; 
				enqueue(runQueue, currentThread);
			} else {
				enqueue(blockedQueue, currentThread);
			}
		}
		mutex->lock = '0';
		mutex->tid = -1;
		current->desiredMutex = -1; // If the current->desiredMutex = mutex->id instruction on line 261 is removed, remove this line too.
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
	int threadWaiting = checkExistBlockedQueue(blockedQueue, mutex->id);
	char isLocked = mutex->lock;
	if(threadWaiting == 1 || isLocked == '1') {
		//A thread is waiting or using this mutex, what to do? Currently going to just return -1 or error. 
		return -1;
	} else {
		// No thread is waiting for this mutex, can destroy freely
		// Can't they pass in a non-malloced mutex? Should I leave it so the client has to free the mutex if they malloced it?
		free(mutex);
	}
	
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

	// schedule policy
	#ifndef MLFQ
		// Choose RR
		sched_rr();
	#else 
		// Choose MLFQ
		sched_mlfq();
	#endif
	
	// TO ADD reset timer.
	setcontext(&(current->context));
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

