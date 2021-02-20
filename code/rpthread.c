// File:	rpthread.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "rpthread.h"

// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define THREAD_STACK_SIZE SIGSTKSZ
#define MAX_PRIORITY 4
#define BOOST_AFTER_TIME_SLICE 50
rpthread_t threadID = 0;
uint mutexID = 1;
Queue* readyQueue = NULL;
Queue* blockedQueue = NULL;
Queue* exitQueue = NULL;
Queue* joinQueue = NULL;
schedulerNode* scheduleInfo = NULL;
tcb* scheduler = NULL; 
tcb* current = NULL;
int flag = 1;
struct itimerval timer = {0};
struct itimerval zero = {0};
/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// create Thread Control Block
	// create and initialize the context of this thread
	// allocate space of stack for this thread to run
	// after everything is all set, push this thread int
	// YOUR CODE HERE
	
	//First time being run, therefore it will have to create the main thread, new thread, and the scheduler thread.
	if(scheduler == NULL) {
		printf("Initial Setup\n");
		initializeScheduler();
		initializeScheduleQueues();  
		scheduler = initializeTCB();
		scheduleInfo->scheduler = scheduler;
		makecontext(&(scheduler->context), (void*) schedule, 0);
		tcb* mainTCB = initializeTCBHeaders();
		printf("Main thread %d\n", mainTCB->id);
		scheduleInfo->current = mainTCB;
		current = scheduleInfo->current;
		//Because mainContext is obtained from getContext(), it will resume at the getContext call (see getContext(2) man page) which is initializeTCB() therefore it will realloc the stack and everything but we do not want that so we have to recall getcontext in here.
		//If we were able to use makeContext(), then when we return to the thread, it would run the function specified in the makeContext() which is why the other threads are fine except Main thread.
		if(getcontext(&mainTCB->context) == -1) {
			perror("[D]: Failed to initialize the context of the mainTCB.\n");
			return -1;
		}
		if(mainTCB->context.uc_link == NULL) {
			printf("[D]: This place should only be accessed once! If it is accessed more than once, this is an error because the main thread is now mallocing a new stack each time this thread runs.\n");
			ucontext_t* threadContext = &(mainTCB->context); 
			threadContext->uc_link = &(scheduler->context);
			threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
			if(threadContext->uc_stack.ss_sp == NULL) {
				perror("[D]: Failed to allocate space for the stack of the MAIN TCB.\n");
				exit(-1);
			}
			threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
			threadContext->uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
			mainTCB->stack = threadContext->uc_stack;
		} else {
			return 0;
		}
		initializeSignalHandler();
		initializeTimer();
	}
	pauseTimer();
	printf("[D]: Creating new child thread.\n");
	tcb* newThreadTCB = initializeTCB();
	(*thread) = newThreadTCB->id;
	makecontext(&(newThreadTCB->context), (void*)function, 1, arg);
	enqueue(readyQueue, newThreadTCB);
	printf("Entered PThread Create Exit\n");
	resumeTimer();
	return 0;
};

tcb* initializeTCBHeaders() {
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	if(threadControlBlock == NULL) {
		perror("[D]: Failed to allocate space for the TCB.\n");
		exit(-1);
	}
	//Initializes the attributes of the TCB.
	threadControlBlock->id = threadID++; //Probably should make threadID start 1 instead of 0 because now the joinTID's range (also mutexTID is out of range) != id range. We would treat 0 as "-1" or uninitialized because no thread should have ID 0.
	threadControlBlock->joinTID = -1;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	threadControlBlock->runtime = 0;
	threadControlBlock->desiredMutex = -1;
	threadControlBlock->exitValue = NULL;
	return threadControlBlock;
}

tcb* initializeTCB() {
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	if(threadControlBlock == NULL) {
		perror("[D]: Failed to allocate space for the TCB.\n");
		exit(-1);
	}
	
	//Initializes the attributes of the TCB.
	threadControlBlock->id = threadID++; //Probably should make threadID start 1 instead of 0 because now the joinTID's range (also mutexTID is out of range) != id range (since id is using uint while the others is using int. We would treat 0 as "-1" or uninitialized because no thread should have ID 0.
	threadControlBlock->joinTID = -1;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	threadControlBlock->runtime = 0;
	threadControlBlock->desiredMutex = -1;
	threadControlBlock->exitValue = NULL;
	
	//Initializes the context of the TCB.
	if(getcontext(&(threadControlBlock->context)) == -1){
		perror("[D]: Failed to initialize the context of the TCB.\n");
		exit(-1);
	};
	
	ucontext_t* threadContext = &(threadControlBlock->context); //This is created just to make it easier to use it without having to clutter the code with &(theardControlBlock->context)
	threadContext->uc_link = (scheduler == NULL) ? NULL : &(scheduler->context);
	threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
	if(threadContext->uc_stack.ss_sp == NULL) {
		perror("[D]: Failed to allocate space for the stack of the TCB.\n");
		exit(-1);
	}
	threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
	threadContext->uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
	threadControlBlock->stack = threadContext->uc_stack;
	
	return threadControlBlock;
}

void initializeScheduleQueues() {
	readyQueue = initializeQueue();
	joinQueue = initializeQueue();
	blockedQueue = initializeQueue();
	exitQueue = initializeQueue();
}

void initializeScheduler() {
	scheduleInfo = malloc(sizeof(schedulerNode) * 1);
	scheduleInfo->numberOfQueues = MAX_PRIORITY;
	scheduleInfo->priorityQueues = calloc(MAX_PRIORITY, sizeof(Queue)); // Hoping this zeros out all the queues, if not have to traverse each and memset to '0'
	scheduleInfo->scheduler = NULL;
	scheduleInfo->current = NULL;
	scheduleInfo->usedEntireTimeSlice = '0';
	scheduleInfo->timeSlices = 0; 
}

void initializeSignalHandler() {
	struct sigaction signalHandler = {0};
	signalHandler.sa_handler = &timer_interrupt_handler;
	sigaction(SIGPROF, &signalHandler, NULL);
}

void initializeTimer() {
	//The initial and reset values of the timer. 
	timer.it_interval.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_interval.tv_usec = (TIMESLICE * 1000) % 1000000;
	
	//How long the timer should run before outputting a SIGPROF signal. 
	// (The timer will count down from this value and once it hits 0, output a signal and reset to the IT_INTERVAL value)
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 1000000;
	printf("[D]: The timer has been initialized. Time interval is %ld seconds, %ld microseconds. The Time remaining is %ld seconds, %ld microseconds.\n", timer.it_interval.tv_sec, timer.it_interval.tv_usec, timer.it_value.tv_sec, timer.it_value.tv_usec);
	
	setitimer(ITIMER_PROF, &timer, NULL);
}
 
Queue* initializeQueue() {
	Queue* queue = malloc(sizeof(Queue) * 1); 
	if(queue == NULL) {
		perror("[D]: Failed to allocate space for a queue.\n");
		exit(-1);
	}
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
	if(newNode == NULL) {
		perror("[D]: Failed to allocate space for a queueNode.\n");
		exit(-1);
	}
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
			if(currentNode->next == queue->tail) {
				queue->tail = currentNode;
			}
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
			if(currentNode->next == queue->tail) {
				queue->tail = currentNode;
			}
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

tcb* findFirstOfBlockedQueue(Queue* queue, int mutexID) {
	if(queue == NULL || queue->head == NULL) {
		return NULL;
	}
	QueueNode* currentNode = queue->head;
	// Checking if the first node has the data
	if(currentNode->node->desiredMutex == mutexID){
		return dequeue(queue);
	}
	// Will traverse through all the node and check except the 1st node 
	while(currentNode->next != NULL) {
		if(currentNode->next->node->desiredMutex == mutexID) {
			if(currentNode->next == queue->tail) {
				queue->tail = currentNode;
			}
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
	disableTimer();
	if (signum == SIGPROF) {
		//printf("[D]: Timer interrupt happened! Saving the current context and changing to the scheduler content! Also disabling the current timer so no interrupts in the scheduling thread.\n");
		scheduleInfo->usedEntireTimeSlice = '1';
		swapcontext(&(current->context), &(scheduler->context));
	} else {
		printf("[D]: Random Signal occurred but was not the timer. This should never happen??? Signal caught: %d\n", signum);
	}
}

void disableTimer() {
	//To disable, set it_value to 0, regardless of it_interval. (According to the man pages)
	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 0;
	setitimer(ITIMER_PROF, &timer, NULL);
	//printf("[D]: The timer has been disabled!\n");
} 

void startTimer() { //Should it just call initialize timer instead?
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 1000000;
	//printf("[D]: The timer is starting again. Time interval is %ld seconds, %ld microseconds.\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	setitimer(ITIMER_PROF, &timer, NULL);
} 

void pauseTimer() {
	setitimer(ITIMER_PROF, &zero, &timer);
	printf("[D]: Time Paused, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	//printf("[D]: The timer has been paused!\n");
}

void resumeTimer() { 
	printf("[D]: Time resuming, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	setitimer(ITIMER_PROF, &timer, NULL);
	//printf("[D]: The timer is resuming!\n");
}

void printQueue(Queue* queue) {
	QueueNode* currentNode = queue->head;
	while(currentNode != NULL){
		printf("Thread ID %d\n", currentNode->node->id);
		currentNode = currentNode->next;
	}
}

/* give CPU possession to other user-level threads voluntarily */
int rpthread_yield() {
	// change thread state from Running to Ready
	// save context of this thread to its thread control block
	// switch from thread context to scheduler context

	// YOUR CODE HERE
	// Scheduler will change the status to READY 
	disableTimer();
	printf("Entered PThread Yield\n");
	swapcontext(&(current->context), &(scheduler->context));

	return 0;
};

/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	
	// YOUR CODE HERE
	disableTimer();
	printf("Entered PThread Exit\n");
	if (value_ptr != NULL) {
		current->exitValue = value_ptr; //Need to set to return value but how?
	} //If the return value is not set, can we assume we can completely just free this thread and no other thread will try to join on this thread?
	
	//Currently there's a 1:1 relationship between exit and join, every exit thread must have a thread that joins on it...is this correct?
	// Can we free this or do we still need this for a copy of the exitValue?
	printf("[D]: Attempting to find a thread that is waiting on this thread %d\n", current->id);
	tcb* joinThread = findFirstOfJoinQueue(joinQueue, current->id);
	if (joinThread != NULL) {
		joinThread->status = READY;
		joinThread->joinTID = -1;
		joinThread->exitValue = current->exitValue;
		printf("[D]: Found a thread %d that is waiting on this exiting thread %d, putting it on the ready queue\n", joinThread->id, current->id);
		enqueue(readyQueue, joinThread);
		printf("[D]: Freeing exit thread, no longer required?\n");
		free(current->context.uc_stack.ss_sp);
		free(current);
	} else {
		printf("[D]: Found no thread that is waiting on this thread %d, added to the exit queue\n", current->id);
		enqueue(exitQueue, current);
		getcontext(&(current->context));
	}
	current = NULL;
	setcontext(&(scheduler->context));
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	pauseTimer();
	printf("Entered PThread Join\n");
	//According to how PTHREAD works, threads cannot join on the same thread meaning once a thread is joined on, the joined thread's pthread struct should be freed and before that should return the retval to the thread that is doing the joinning  
	//Join threads are taken off the run queue because it wastes run time and should only resume when if the thread exits right? Therefore Two cases: Thread Exits then another thread attempts to join or threads attempts to join then thread exits? Therefore we have to check the exit queue in here to see if the thread has already exit and check in the exit, if there is a thread waiting to join on it?
	// What happens if a thread attempts to join but the thread does not exist or already exitted, should it just be stuck forever on the join queue?
	//printf("[D]: Finding exit thread\n");
	tcb* exitThread = findFirstOfQueue(exitQueue, thread);
	//printf("[D]: Found exit thread\n");
	if(exitThread == NULL) {
		printf("[D]: Exit thread %d does not exist currently.\n", thread);
		current->status = WAITING;
		current->joinTID = thread;
		//printf("[D]: Current thread is waiting, going on the join queue.\n");
		enqueue(joinQueue, current); //Should it be on its own seperate queue (joinQueue) or in BLOCKEDQUEUE? If seperate queue then there will be less time to search since BLOCKEDQUEUE has the threads that are waiting on a mutex while this is waiting on a certain thread.
		printf("[D]: Current thread %d has been added to the join queue.\n", current->id);
		disableTimer();
		flag = 1;
		swapcontext(&(current->context), &(scheduler->context)); 
		pauseTimer();
		printf("[D]: Resuming thread %d, was on join queue but exit thread %d has exited!\n", current->id, thread);
		if(value_ptr != NULL) {
			*value_ptr = current->exitValue;
		}
		current->exitValue = NULL;
	} else {
		printf("[D]: Found exit thread %d for the current thread %d, freeing the exit thread.\n", exitThread->id, current->id);
		current->joinTID = -1;
		if(value_ptr != NULL) {	
			*value_ptr = exitThread->exitValue; //Apparently you can deference void** (but it will only store void*, if you attempt to store anything else it will be a complier warning)
		}
		free(exitThread->context.uc_stack.ss_sp);
		free(exitThread);
		//enqueue(exitQueue, exitThread);
		//printf("[D]: Re-added Thread %d to the exit queue!\n", exitThread->id);
		//printQueue(exitQueue);
	}
	resumeTimer();
	return 0;
};

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//initialize data structures for this mutex
	// YOUR CODE HERE
	pauseTimer();
	printf("Entered Mutex Init\n");
	if(mutex == NULL){
		resumeTimer();
		return -1;
	}
	
	//Assuming rpthread_mutex_t has already been malloced otherwise we would have to return a pointer (since we would be remallocing it)
	mutex->id = mutexID++;
	mutex->tid = -1; 
	mutex->lock = '0';
	
	resumeTimer();
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
        pauseTimer();
        //printf("Entered Mutex Lock\n");
       	if(mutex == NULL){
       		resumeTimer();
       		return -1;
       	}
       	
        if(mutex->lock == '1') {
        	printf("[D]: Current thread wants a mutex but it is already locked.\n");
		    current->status = BLOCKED;
		    current->desiredMutex = mutex->id;
		    //printf("[D]: Adding current thread to the block queue\n");
		    enqueue(blockedQueue, current); 
		    printf("[D]: Succesfully added current thread to the block queue\n");
		    disableTimer();
		    swapcontext(&(current->context), &(scheduler->context)); 
		    printf("[D]: Mutex has been unlocked, this thread can now run!\n");
        } else if (mutex->lock == '0') {
        	//printf("[D]: This thread %d is locking this mutex!\n", current->id);
        	mutex->lock = '1'; 
        	mutex->tid = current->id;
        	// current->desiredMutex = mutex->id; // Might be able to remove this instruction since technically this thread has this mutex and therefore to unlock the mutex, we can check the tid of the mutex to see if the thread can unlock it.
        	resumeTimer();
        } else {
        	
        }
        return 0;
};

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.
		
	// YOUR CODE HERE
	pauseTimer();
	//printf("Entered Mutex Unlock\n");
	if (mutex == NULL) {
		return -1;
	}
	
	//Checking if the thread has the mutex so other threads cannot just unlock mutexes they do not have 
	// We are going to assume if the user calls this, the mutex is already locked so we don't need to check the lock field, (doing this so when it will work with test_and_set since test_and_set sets the value and returns the old value of what it was
	if (current->id == mutex->tid) {
		//Note the problem with this for loop is that only the threads with the highest priority will run before the lower priority threads and thus obtain the lock before the lower priority threads 
		//Maybe we should just do first come, first serve for mutexes, I'm not entirely sure if pthread does this but in the book, they do this.
		/**
		int maxthreads = blockedQueue->size; 
		QueueNode* currentBlock = blockedQueue->head;
		for (int index = 0; index < maxthreads; index++) { 
			tcb* currentThread = dequeue(blockedQueue);
			//Means this thread requires this mutex, and now the mutex is free, can be removed from the block list and placed on the readyQueue
			if (currentThread->desiredMutex == mutex->id) {
				currentThread->status = READY; 
				enqueue(readyQueue, currentThread);
			} else {
				enqueue(blockedQueue, currentThread);
			}
		}
		mutex->lock = '0';
		mutex->tid = -1;
		current->desiredMutex = -1; // If the current->desiredMutex = mutex->id instruction on line 261 is removed, remove this line too.
		*/
		tcb* unblockedThread = findFirstOfBlockedQueue(blockedQueue, mutex->id);
		if(unblockedThread != NULL) {
			printf("[D]: Succesfully found a thread that is waiting on this mutex, adding the thread to the ready queue\n");
			unblockedThread->status = READY;
			unblockedThread->desiredMutex = -1;
			enqueue(readyQueue, unblockedThread);
			mutex->tid = unblockedThread->id;
		} else {
			//printf("[D]: No thread is waiting on mutex %d\n", mutex->id);
			mutex->lock = '0';
			mutex->tid = -1;
		} 

		//printf("[D]: Mutex is now unlocked.\n");
		resumeTimer();
		return 0;
	}
	resumeTimer(); 
	return -1;
};


/* destroy the mutex */
int rpthread_mutex_destroy(rpthread_mutex_t *mutex) {
	// Deallocate dynamic memory created in rpthread_mutex_init
	pauseTimer();
	printf("Entered Mutex Destroy\n");
	if(mutex == NULL){
		resumeTimer();
		return -1;
	}
	// Should we be able to free mutexes that are in use or required/wanted by threads?
	int threadWaiting = checkExistBlockedQueue(blockedQueue, mutex->id);
	char isLocked = mutex->lock;
	if(threadWaiting == 1 || isLocked == '1') {
		//A thread is waiting or using this mutex, what to do? Currently going to just return -1 or error. 
		resumeTimer();
		return -1;
	} else {
		// No thread is waiting for this mutex, can destroy freely
		// Can't they pass in a non-malloced mutex? Should I leave it so the client has to free the mutex if they malloced it?
		
	}
	resumeTimer();
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
	//printf("[D]: Entered Scheduler\n");
	if(flag) {
		flag = 0;
		current = NULL;
	}
	// schedule policy
	#ifndef MLFQ
		// Choose RR
		//printf("[D]: RR\n");
		sched_rr();
	#else 
		// Choose MLFQ
		//printf("[D]: MLFQ\n");
		sched_mlfq();
	#endif
	
	startTimer();
	setcontext(&(current->context));
}

/* Round Robin (RR) scheduling algorithm */
static void sched_rr() {
	// Your own implementation of RR
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	if(scheduleInfo->priorityQueues != NULL) {
		free(scheduleInfo->priorityQueues);
		scheduleInfo->priorityQueues = NULL;
		scheduleInfo->numberOfQueues = 0;
	}
	tcb* nextRun = dequeue(readyQueue);
	if(nextRun != NULL){
		if(current != NULL){
			enqueue(readyQueue, current);
		}
		current = nextRun;
	} 
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq() {
	// Your own implementation of MLFQ
	// (feel free to modify arguments and return types)

	// YOUR CODE HERE
	
	
	//Enqueue the READY threads into the priority queues (Also boost if time to boost)
	scheduleInfo->timeSlices++;
	int readyQueueSize = readyQueue->size;
	for(int readyThread = 0; readyThread < readyQueueSize; ++readyThread) {
		tcb* readyTCB = dequeue(readyQueue);
		if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE && readyTCB->priority != MAX_PRIORITY) {
			readyTCB->priority += 1;
		}
		enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
	}
	
	// Boosting the priority every X time slices, should probably change to a timer 
	if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE) {
		printf("[D]: Boosting priorities!\n");
		for(int priorityQueueLevel = 0; priorityQueueLevel < MAX_PRIORITY - 1; priorityQueueLevel++) {
			tcb* readyTCB = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
			while(readyTCB != NULL){
				readyTCB->priority += 1;
				enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
				readyTCB = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
			}
		}
		scheduleInfo->timeSlices = 0;
	}
	
	//If the current thread used the whole time slice, decrease its priority.
	if(scheduleInfo->usedEntireTimeSlice == '1') {
		if(current != NULL && current->priority != 1) {
			current->priority -= 1;
		}
		scheduleInfo->usedEntireTimeSlice = '0';
	} 
	//Find the next thread to run by searching through the highest priority queue. 
	for (int priorityQueueLevel = MAX_PRIORITY - 1; priorityQueueLevel >= 0; priorityQueueLevel--) { 
		tcb* nextRun = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
		if(nextRun != NULL) {
			if(current != NULL) {
				enqueue(&scheduleInfo->priorityQueues[current->priority - 1], current);
			}
			current = nextRun;
			break;
		}
	}
}

// Feel free to add any other functions you need

// YOUR CODE HERE

