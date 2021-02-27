// File:	rpthread.c

// List all group member's name:
// username of iLab:
// iLab Server:

#include "rpthread.h"

// INITAILIZE ALL YOUR VARIABLES HERE
// YOUR CODE HERE
#define THREAD_STACK_SIZE SIGSTKSZ
#define MAX_PRIORITY 4
#define BOOST_AFTER_TIME_SLICE threadID * 2
rpthread_t threadID = 1;
uint mutexID = 1;
Queue* readyQueue = NULL;
Queue* blockedQueue = NULL;
Queue* exitQueue = NULL;
Queue* joinQueue = NULL;
schedulerNode* scheduleInfo = NULL; 
ucontext_t scheduler = {0}; //Can change this to be the scheduler context only instead of allocating all the space for a TCB struct.
tcb* current = NULL;
volatile int blockedQueueMutex = 0;
struct itimerval timer = {0};
struct itimerval zero = {0};
struct sigaction signalHandler = {0};

/* create a new thread */
int rpthread_create(rpthread_t * thread, pthread_attr_t * attr, 
                      void *(*function)(void*), void * arg) {

	// create Thread Control Block
	// create and initialize the context of this thread
	// allocate space of stack for this thread to run
	// after everything is all set, push this thread int
	// YOUR CODE HERE
	
	// First time being run, therefore it will have to create the main thread, 
	// new thread, and the scheduler thread.
	if(scheduler.uc_stack.ss_size == 0) {
		printf("[D]: Initial Setup\n");
		initializeScheduler();
		initializeScheduleQueues();  
		if(getcontext(&(scheduler)) == -1){
			perror("[D]: Failed to initialize the context of the TCB.\n");
			exit(-1);
		};
		scheduler.uc_link = NULL;
		scheduler.uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
		if(scheduler.uc_stack.ss_sp == NULL) {
			perror("[D]: Failed to allocate space for the stack of the TCB.\n");
			exit(-1);
		}
		scheduler.uc_stack.ss_size = THREAD_STACK_SIZE;
		scheduler.uc_stack.ss_flags = 0; 
		makecontext(&scheduler, (void*) schedule, 0);
		
		tcb* mainTCB = initializeTCBHeaders();
		printf("Main thread %d\n", mainTCB->id);
		current = mainTCB;
		// Because mainContext is obtained from getContext(), it will resume at the 
		// getContext call (see getContext(2) man page) which is initializeTCB() 
		// therefore it will realloc the stack and everything but we do not want 
		// that so we have to recall getcontext in here. Obviously if the mainTCB
		// gets swapped via swapcontext, then we do not have to worry about 
		// it coming back here. This is used for safety! Probably can remove.
		
		// If we were able to use makeContext(), then when we return to the thread,
		// it would run the function specified in the makeContext() which is why
		// the other threads are fine except Main thread.
		if(getcontext(&mainTCB->context) == -1) {
			perror("[D]: Failed to initialize the context of the mainTCB.\n");
			return -1;
		}
		if (mainTCB->context.uc_link == NULL) {
			printf("[D]: This place should only be accessed once! If it is accessed more than once, this is an error because the main thread is now mallocing a new stack each time this thread runs.\n");
			ucontext_t* threadContext = &(mainTCB->context); 
			threadContext->uc_link = &(scheduler);
			/**
				The main context does not need to be malloced technically but I
				am mallocing it so if the user decides to pthread_exit the main
				context, we don't run into a free issue where it tries to free
				the context stack (I am not entirely sure what happens if you 
				try to free the main context or if the main context is malloced) 
				so I will be mallocing the main context stack. Note this will not 
				affect the outcome because the main context should swapcontext
				to exit out and thus will save the current context and therefore
				the behavior should be the same. 
			*/
			threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
			if(threadContext->uc_stack.ss_sp == NULL) {
				perror("[D]: Failed to allocate space for the stack of the MAIN TCB.\n");
				return -1;
			}
			threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
			threadContext->uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
			//mainTCB->stack = threadContext->uc_stack;
		} else {
			return 0;
		}
		initializeSignalHandler();
		initializeTimer();
	}
	pauseTimer();
	//printf("[D]: Creating new child thread.\n");
	tcb* newThreadTCB = initializeTCB();
	(*thread) = newThreadTCB->id;
	makecontext(&(newThreadTCB->context), (void*)function, 1, arg);
	enqueue(readyQueue, newThreadTCB);
	//printf("[D]: Done creating the child thread, resuming!\n");
	resumeTimer();
	return 0;
};

/*
	Mallocs and initializes only the headers of the TCB struct but not the
	context. 
*/
tcb* initializeTCBHeaders() {
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	if(threadControlBlock == NULL) {
		perror("[D]: Failed to allocate space for the TCB.\n");
		exit(-1);
	}
	//Initializes the attributes of the TCB.
	threadControlBlock->id = threadID++; 
	threadControlBlock->joinTID = 0;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	//threadControlBlock->runtime = 0;
	threadControlBlock->desiredMutex = 0;
	threadControlBlock->exitValue = NULL;
	return threadControlBlock;
}

/**
	Mallocs and initializes the TCB struct, including the context.
*/

tcb* initializeTCB() {
	tcb* threadControlBlock = malloc(sizeof(tcb) * 1);
	if(threadControlBlock == NULL) {
		perror("[D]: Failed to allocate space for the TCB.\n");
		exit(-1);
	}
	
	//Initializes the attributes of the TCB.
	threadControlBlock->id = threadID++; //Probably should make threadID start 1 instead of 0 because now the joinTID's range (also mutexTID is out of range) != id range (since id is using uint while the others is using int. We would treat 0 as "-1" or uninitialized because no thread should have ID 0.
	threadControlBlock->joinTID = 0;  
	threadControlBlock->priority = MAX_PRIORITY;
	threadControlBlock->status = READY;
	//threadControlBlock->runtime = 0;
	threadControlBlock->desiredMutex = 0;
	threadControlBlock->exitValue = NULL;
	
	//Initializes the context of the TCB.
	if(getcontext(&(threadControlBlock->context)) == -1){
		perror("[D]: Failed to initialize the context of the TCB.\n");
		exit(-1);
	};
	
	ucontext_t* threadContext = &(threadControlBlock->context); //This is created just to make it easier to use it without having to clutter the code with &(theardControlBlock->context)
	threadContext->uc_link = (scheduler.uc_stack.ss_size == 0) ? NULL : &(scheduler);
	threadContext->uc_stack.ss_sp = malloc(THREAD_STACK_SIZE);
	if(threadContext->uc_stack.ss_sp == NULL) {
		perror("[D]: Failed to allocate space for the stack of the TCB.\n");
		exit(-1);
	}
	threadContext->uc_stack.ss_size = THREAD_STACK_SIZE;
	threadContext->uc_stack.ss_flags = 0; //Can either be SS_DISABLE or SS_ONSTACK 
	//threadControlBlock->stack = threadContext->uc_stack;
	
	return threadControlBlock;
}
/**
	Mallocs and initializes all the queues required for the scheduling.
	Ready queue indicates which threads are ready to be scheduled.
	Join queue indicates which threads are waiting for another thread to 
	terminate.
	Blocked queue indicates which threads are waiting for mutex.
	Exit queue indicates which threads have exited but not been joined yet.
*/
void initializeScheduleQueues() {
	readyQueue = initializeQueue();
	joinQueue = initializeQueue();
	blockedQueue = initializeQueue();
	exitQueue = initializeQueue();
}

/**
	Mallocs and initializes the global scheduler struct.
*/
void initializeScheduler() {
	scheduleInfo = malloc(sizeof(schedulerNode) * 1);
	if (scheduleInfo == NULL) {
		perror("[D]: Failed to allocate space for the schedule Info\n");
		exit(-1);
	}
	scheduleInfo->numberOfQueues = MAX_PRIORITY;
	scheduleInfo->priorityQueues = calloc(MAX_PRIORITY, sizeof(Queue)); // Hoping this zeros out all the queues, if not have to traverse each and memset to '0'
	if (scheduleInfo->priorityQueues == NULL) {
		perror("[D]: Failed to allocate space for the priority queues\n");
		exit(-1);
	}
	//scheduleInfo->scheduler = NULL;
	//scheduleInfo->current = NULL;
	scheduleInfo->usedEntireTimeSlice = '0';
	scheduleInfo->timeSlices = 0; 
}

/**
	Registers the SIGPROF signal to be handled with the timer_interrupt_handler
	function. 
*/
void initializeSignalHandler() {
	signalHandler.sa_handler = &timer_interrupt_handler;
	if(sigaction(SIGPROF, &signalHandler, NULL) == -1) {
		perror("[D]: Could not initialize the timer interrupt handler!\n");
		exit(-1);
	}
}


/*
	Starts the timer with no interval, just sets it to the time slice because
	once the timer runs out and sends the signal (SIGPROF), the timer interrupt
	handler automatically swaps to the scheduler context and the schedule 
	context automatically restarts the timer. 
*/
void initializeTimer() {
	//The initial and reset values of the timer. 
	//timer.it_interval.tv_sec = (TIMESLICE * 1000) / 1000000;
	//timer.it_interval.tv_usec = (TIMESLICE * 1000) % 1000000;
	
	//How long the timer should run before outputting a SIGPROF signal. 
	// (The timer will count down from this value and once it hits 0, output a signal and reset to the IT_INTERVAL value)
	
	timer.it_value.tv_sec = (TIMESLICE * 1000) / 1000000;
	timer.it_value.tv_usec = (TIMESLICE * 1000) % 1000000;

	printf("[D]: The timer has been initialized.\n Time interval is %ld seconds, %ld microseconds.\n The Time remaining is %ld seconds, %ld microseconds.\n", timer.it_interval.tv_sec, timer.it_interval.tv_usec, 		timer.it_value.tv_sec, timer.it_value.tv_usec);
	
	setitimer(ITIMER_PROF, &timer, NULL);
}

/**
	Allocates memory for the queue struct and sets the default values, 
	returns the pointer to the malloced queue. 
*/
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

// Must initialize the queue via initializeQueue() before calling this method
void enqueue(Queue* queue, tcb* threadControlBlock) {
	if (queue == NULL || threadControlBlock == NULL) {
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
	//char debug[] = "Timer Interrupt Happened\n";
	//write(1, &debug, sizeof(debug));
	if (signum == SIGPROF) {
		scheduleInfo->usedEntireTimeSlice = '1';
		swapcontext(&(current->context), &(scheduler));
	} else {
		char error[] = "[E]: ERROR SIGNAL OCCURRED IN TIMER INTERRUPT THAT WAS NOT SIGPROF\n";
		write(1, &error, sizeof(error)); // Using write because it's thread safe
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
	
	//printf("[D]: Time Paused, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);
	//printf("[D]: The timer has been paused!\n");
}

void resumeTimer() { 
	//printf("[D]: Time resuming, time left: %ld seconds, %ld microseconds\n", timer.it_value.tv_sec, timer.it_value.tv_usec);

	//How long the timer should run before outputting a SIGPROF signal. 
	// (The timer will count down from this value and once it hits 0, output a signal and reset to the IT_INTERVAL value)
	
	timer.it_value.tv_sec = (((TIMESLICE * 1000) / 1000000) == 0) ? 0 : ((TIMESLICE * 1000) / 1000000) - (timer.it_value.tv_sec % ((TIMESLICE * 1000) / 1000000));
	timer.it_value.tv_usec = (((TIMESLICE * 1000) % 1000000) == 0) ? 0 : ((TIMESLICE * 1000) % 1000000) - (timer.it_value.tv_usec % ((TIMESLICE * 1000) % 1000000));

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

	disableTimer();
	//printf("Entered PThread Yield\n");
	swapcontext(&(current->context), &(scheduler));

	return 0;
};

/* terminate a thread */
void rpthread_exit(void *value_ptr) {
	// Deallocated any dynamic memory created when starting this thread
	
	// YOUR CODE HERE
	disableTimer();
	//printf("Entered PThread Exit\n");
	if (value_ptr != NULL) {
		current->exitValue = value_ptr; 
	} //If the return value is not set, can we assume we can completely just free this thread and no other thread will try to join on this thread?
	
	//Currently there's a 1:1 relationship between exit and join, every exit thread must have a thread that joins on it...is this correct?
	free(current->context.uc_stack.ss_sp); // I am not entirely sure if we can free the stack but I'm assuming yes, if we're exiting the thread.
	current->context.uc_stack.ss_sp = NULL;
	current->context.uc_stack.ss_size = 0;
	//printf("[D]: Attempting to find a thread that is waiting on this thread %d\n", current->id);
	tcb* joinThread = findFirstOfJoinQueue(joinQueue, current->id);
	if (joinThread != NULL) {
		joinThread->status = READY;
		joinThread->joinTID = 0;
		joinThread->exitValue = current->exitValue;
		//printf("[D]: Found a thread %d that is waiting on this exiting thread %d, putting it on the ready queue\n", joinThread->id, current->id);
		enqueue(readyQueue, joinThread);
		//printf("[D]: Freeing exit thread %d, no longer required?\n", current->id);
		free(current);
	} else {
		//printf("[D]: Found no thread that is waiting on this thread %d, added to the exit queue\n", current->id);
		enqueue(exitQueue, current);
	}
	current = NULL;
	setcontext(&(scheduler));
};


/* Wait for thread termination */
int rpthread_join(rpthread_t thread, void **value_ptr) {
	
	// wait for a specific thread to terminate
	// de-allocate any dynamic memory created by the joining thread
  
	// YOUR CODE HERE
	pauseTimer();
	//printf("Entered PThread Join\n");
	//According to how PTHREAD works, threads cannot join on the same thread meaning once a thread is joined on, the joined thread's pthread struct should be freed and before that should return the retval to the thread that is doing the joinning  
	//Join threads are taken off the run queue because it wastes run time and should only resume when if the thread exits right? Therefore Two cases: Thread Exits then another thread attempts to join or threads attempts to join then thread exits? Therefore we have to check the exit queue in here to see if the thread has already exit and check in the exit, if there is a thread waiting to join on it?
	// What happens if a thread attempts to join but the thread does not exist or already exited, should it just be stuck forever on the join queue?
	//printf("[D]: Finding exit thread\n");
	tcb* exitThread = findFirstOfQueue(exitQueue, thread);
	//printf("[D]: Found exit thread\n"); 
	if(exitThread == NULL) {
		//printf("[D]: Exit thread %d does not exist currently.\n", thread);
		current->status = WAITING;
		current->joinTID = thread;
		//printf("[D]: Current thread is waiting, going on the join queue.\n");
		enqueue(joinQueue, current);
		//printf("[D]: Current thread %d has been added to the join queue.\n", current->id);
		swapcontext(&(current->context), &(scheduler)); 
		pauseTimer();
		//printf("[D]: Resuming thread %d, was on join queue but exit thread %d has exited!\n", current->id, thread);
		if(value_ptr != NULL) {
			*value_ptr = current->exitValue;
		}
		current->exitValue = NULL;
	} else {
		//printf("[D]: Found exit thread %d for the current thread %d, freeing the exit thread.\n", exitThread->id, current->id);
		if(value_ptr != NULL) {	
			*value_ptr = exitThread->exitValue; //Apparently you can deference void** (but it will only store void*, if you attempt to store anything else it will be a complier warning)
		}
		free(exitThread);
	}
	resumeTimer();
	return 0;
};

/* initialize the mutex lock */
int rpthread_mutex_init(rpthread_mutex_t *mutex, 
                          const pthread_mutexattr_t *mutexattr) {
	//initialize data structures for this mutex
	// YOUR CODE HERE
	printf("Entered Mutex Init\n");
	if(mutex == NULL){
		return -1;
	}
	
	//Assuming rpthread_mutex_t has already been malloced otherwise we would have to return a pointer (since we would be remallocing it)
	mutex->id = mutexID++;
	mutex->tid = 0; 
	mutex->lock = 0;
	mutex->waitingThreadID = 0;
	
	return 0;
};

/* aquire the mutex lock */
int rpthread_mutex_lock(rpthread_mutex_t *mutex) {
        // use the built-in test-and-set atomic function to test the mutex
        // if the mutex is acquired successfully, enter the critical section
        // if acquiring mutex fails, push current thread into block list and //  
        // context switch to the scheduler thread

        // YOUR CODE HERE
        
        
        //Where to find the built-in test and set atomic function
        // So I'm just going to use chars as the lock even though this is not really test_and_set but w/e
        //printf("Entered Mutex Lock\n");
       	if (mutex == NULL) {
       		return -1;
       	}
       	
       	while (__sync_lock_test_and_set(&(blockedQueueMutex), 1)) {
			rpthread_yield();
			// YIELD instead of SPINWAIT until this thread can access the 
			// blockQueue mutex since only one thread should modify the block 
			// queue at a time for thread safe and we do not want to waste run
			// time so we yield.
		}
       	
        while (__sync_lock_test_and_set(&(mutex->lock), 1)) {
        	
        	disableTimer(); // Since we cannot guarantee the disableTimer executes
        					// after the lock instruction, then we have to wrap 
        					// this in the blockedQueueMutex. (We want this the 
        					// WHOLE for loop to execute atomically.) 
        	
        	//printf("[D]: Current thread %d wants a mutex %d but it is already locked by thread %d.\n", current->id, mutex->id, mutex->tid);
		    current->status = BLOCKED;
		    current->desiredMutex = mutex->id;

		    //printf("[D]: Adding current thread to the block queue\n");
		    enqueue(blockedQueue, current); 
		    
		    //printf("[D]: Succesfully added current thread %d to the block queue\n", current->id);
		    
		    __sync_lock_release(&(blockedQueueMutex)); 
		    
		    swapcontext(&(current->context), &(scheduler)); // The blocked thread will return here and thus we will have to check if the mutex is still locked or not.
        	while(__sync_lock_test_and_set(&(blockedQueueMutex), 1)) {
				rpthread_yield();
				// YIELD instead of SPINWAIT until this thread can access the 
				// blockQueue mutex since only one thread should modify the block 
				// queue at a time for thread safe and we do not want to waste run
				// time so we yield.
			}
			//printf("[D]: Resuming thread %d, Mutex %d was unlocked and now can maybe be obtained!\n", current->id, mutex->id);
			/*
		   	if (mutex->waitingThreadID != current->id) {
        		printf("[D]: ERROR in waitingThreadID %d, should always be the current->id or this thread %d\n", mutex->waitingThreadID, current->id);
        	}
        	*/
			mutex->waitingThreadID = 0;
			
        } 
    	//printf("[D]: This thread %d is locking this mutex!\n", current->id);
    	mutex->tid = current->id;
    	__sync_lock_release(&(blockedQueueMutex)); 
        
        
        return 0;
};

/* release the mutex lock */
int rpthread_mutex_unlock(rpthread_mutex_t *mutex) {
	// Release mutex and make it available again. 
	// Put threads in block list to run queue 
	// so that they could compete for mutex later.
		
	// YOUR CODE HERE
	//printf("Entered Mutex Unlock\n");
	
	// Checking if the thread has the mutex so other threads cannot just unlock 
	// mutexes they do not have or if the mutex is unlocked already
	if (mutex == NULL || mutex->tid != current->id || mutex->lock == 0) {
		printf("[D]: Entered a bad place\n");
		return -1;
	}
	
	mutex->tid = 0;
	__sync_lock_release(&(mutex->lock));
	
	while(__sync_lock_test_and_set(&(blockedQueueMutex), 1)) {
		rpthread_yield();
		// YIELD instead of SPINWAIT until this thread can access the 
		// blockQueue mutex since only one thread should modify the block 
		// queue at a time for thread safe and we do not want to waste run
		// time so we yield.
	}

	//If we do not have a thread in the readyQueue, already waiting for this mutex, find and add one. 
	if (mutex->waitingThreadID == 0) {
		// First come, first serve implementation of a mutex.
		tcb* unblockedThread = findFirstOfBlockedQueue(blockedQueue, mutex->id);
		if (unblockedThread != NULL) {
			//printf("[D]: Succesfully found a thread %d that is waiting on this mutex %d which is locked by thread %d, adding the thread to the ready queue\n", unblockedThread->id, mutex->id, current->id);
			unblockedThread->status = READY;
			unblockedThread->desiredMutex = 0;
			enqueue(readyQueue, unblockedThread);
			mutex->waitingThreadID = unblockedThread->id; // The reason for this, is so that we can only have one THREAD (FIRST COME FIRST SERVE) where if the mutex is unlocked, can MAYBE obtain the mutex however if the current thread still needs the mutex and has time slice remaining, allow it to potentially lock the mutex again.
		} else {
			//printf("[D]: No thread is waiting on mutex %d\n", mutex->id);
		}
	}
	
	__sync_lock_release(&(blockedQueueMutex)); 
	
	//printf("[D]: Mutex is now unlocked.\n");
	
	return 0;
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
	volatile int isLocked = mutex->lock;
	if(threadWaiting == 1 || isLocked == 1 || mutex->waitingThreadID != 0) {
		//A thread is waiting or using this mutex, what to do? Currently going to just return -1 or error. 
		resumeTimer();
		return -1;
	}
	
	// No thread is waiting for this mutex, can destroy freely 
	mutex->id = 0;
	mutex->tid = 0; 
	mutex->lock = 0;
	mutex->waitingThreadID = 0;
	
	resumeTimer();
	return 0;
};

void freeQueue(Queue* queue) {
	QueueNode* current = queue->head;
	while(current != NULL) {
		QueueNode* temp = current;
		free(temp->node);
		current = current->next;
		free(temp);
	}
	free(queue);
}

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
	
	//NOTE the timer should be disabled or paused before entering this function.
	if(current != NULL && (current->status == WAITING || current->status == BLOCKED)) { 
		// Basically check if the current Thread is now blocked (meaning it is waiting on a mutex)
		// or if it is WAITING (meaning it's waiting on a exit thread), then it 
		// should not be placed in the ready queue aka be scheduled again.
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
	//free(scheduleInformation);
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
		//printf("[D]: The next thread to run is thread %d\n", nextRun->id);
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
	//printf("[D]: Entered Scheduler\n");
	scheduleInfo->timeSlices++;
	
	// Boosting the priority every X time slices, should probably change to a timer 
	if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE) {
		//printf("[D]: Boosting priorities!\n");
		for(int priorityQueueLevel = 0; priorityQueueLevel < MAX_PRIORITY - 1; priorityQueueLevel++) {
			tcb* readyTCB = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
			while(readyTCB != NULL){
				readyTCB->priority += 1;
				enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
				readyTCB = dequeue(&scheduleInfo->priorityQueues[priorityQueueLevel]);
			}
		}
	}
	
	//Enqueue the READY threads into the priority queues (Also boost if time to boost)
	/**
	OLD CODE FOR ENQUEUING READY THREAD
	int readyQueueSize = readyQueue->size;
	for(int readyThread = 0; readyThread < readyQueueSize; ++readyThread) {
		tcb* readyTCB = dequeue(readyQueue);
		if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE && readyTCB->priority != MAX_PRIORITY) {
			readyTCB->priority += 1;
		}
		enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
	}
	*/
	tcb* readyTCB = dequeue(readyQueue);
	while(readyTCB != NULL) {
		if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE && readyTCB->priority != MAX_PRIORITY) {
			readyTCB->priority += 1;
		}
		enqueue(&scheduleInfo->priorityQueues[readyTCB->priority - 1], readyTCB);
		readyTCB = dequeue(readyQueue);
	}
	
	if(scheduleInfo->timeSlices == BOOST_AFTER_TIME_SLICE) {
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

