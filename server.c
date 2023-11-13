#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define BUFFER_SIZE 1032
#define MAX_FILE_NAME_SIZE 32

#include "winsock2.h"
#include "stdio.h"
#include "string.h"
#include "time.h"
#include "math.h"

#pragma comment(lib, "ws2_32.lib")

typedef struct job {
	char* jobName;
	struct job* next;
} job; // struct for making a queue of jobs as a linked list

typedef struct fileEntry {
	int curSize;
	int changeTime;
	struct fileEntry* next;
} fileEntry; // struct for making a queue of entries needed to be writen to the log file, each line as a node

SOCKET serverSock; // socket for communication with the client
HANDLE sockMutex = NULL; // more then one thread is using this socket for send, mutex is needed
struct {
	int Qsize; // current size of the queue
	int Qmax; // max size of the queue
	int statisticsMax;
	double statisticsAvg;
	job* Qhead; // head of jobs linked list
} queueList; // global struct for some data of the queue
HANDLE QMutex = NULL; // mutex for global variable queueList
HANDLE QSemaphore = NULL; // semaphore counting jobs in queue for blocking the jobs thread when no job available
fileEntry* changesHead = NULL; // head of the file entries linked list
HANDLE changesMutex = NULL; // mutex for the global variable changes head
HANDLE changesSemaphore = NULL; // semaphore for blocking file writing thread when nothing to write is availabe

job* createJob() {
	// initilizing a new job pointer
	job* newJob;

	if (NULL == (newJob = (job*)malloc(sizeof(job)))) {
		printf("malloc failed for job creation\n");
		return NULL;
	}
	newJob->jobName = NULL;
	newJob->next = NULL;
	return newJob;
}

fileEntry* createEntry(clock_t newClock, int Qsize) {
	// initilizing a new file entry pointer
	fileEntry* newEntry = (fileEntry*)malloc(sizeof(fileEntry));
	if (newEntry != NULL) {
		newEntry->curSize = Qsize;
		newEntry->changeTime = 1000 * (double)newClock / CLOCKS_PER_SEC;
		newEntry->next = NULL;
	}
	return newEntry;
}

int sendAll(SOCKET s, char* buffer, int lenth) {
	int sent = 0, n = 0;

	while (sent < lenth) {
		n += send(s, buffer, lenth, 0);
		if (n == -1) return n;
		sent += n;
	}
	return sent;
}

void addQChange(clock_t changeClock, int Qsize) {
	// adding a change in the list to the end of the linked list of item needed to be writen to the log file
	fileEntry* newEntry = NULL;
	if (NULL == (newEntry = createEntry(changeClock, Qsize))) {
		printf("failed to save job finish time\n");
		return;
	}
	WaitForSingleObject(changesMutex, INFINITE);
	if (changesHead == NULL) {
		changesHead = newEntry;
	}
	else {
		fileEntry* iter = changesHead;
		while (iter->next != NULL) {
			iter = iter->next;
		}
		iter->next = newEntry;
	}
	ReleaseMutex(changesMutex);
	ReleaseSemaphore(changesSemaphore, 1, NULL); // signal to file thread a new item is available
}

void addToQ(job* newJob) {
	// adding a new job to the end of the queue
	if (queueList.Qhead == NULL) {
		queueList.Qhead = newJob;
	}
	else {
		job* iter = queueList.Qhead;
		while (iter->next != NULL) {
			iter = iter->next;
		}
		iter->next = newJob;
	}
	queueList.Qsize++;
	if (queueList.Qsize > queueList.statisticsMax) {
		queueList.statisticsMax = queueList.Qsize;
	}
	ReleaseSemaphore(QSemaphore, 1, NULL); // signal the job thread that a job is available
}

int parseRecv(char *received, int amount) {
	// a function to parse the incoming bytes stream from the client
	char* parsePtr = received, * packetStart = received;
	int packetSize = 0;
	job* newJob;

	while (amount > 0) {
		if (*parsePtr != '\n') { // look for the end of a packet marked by \n
			packetSize++;
		}
		else {
			newJob = createJob();
			newJob->jobName = (char*)malloc((packetSize + 1) * sizeof(char));
			if (newJob->jobName == NULL) {
				printf("creating job failed\n");
				free(newJob);
				continue;
			}
			strncpy(newJob->jobName, packetStart, packetSize); // packet content will be the job name
			newJob->jobName[packetSize] = '\0';
			WaitForSingleObject(QMutex, INFINITE);
			if (queueList.Qsize == queueList.Qmax) { // queue is full
				ReleaseMutex(QMutex);
				WaitForSingleObject(sockMutex, INFINITE);
				if (-1 == sendAll(serverSock, newJob->jobName, strlen(newJob->jobName)) || // send the client packet(job name)
					-1 == sendAll(serverSock, "T\n", strlen("T\n"))) { // T for thrown, \n for packet end mark
					printf("failed to send job thrown\n");
				}
				ReleaseMutex(sockMutex);
				free(newJob->jobName);
				free(newJob);
			}
			else {
				addToQ(newJob); // queue is not full, job edded to the end of the queue
				addQChange(clock(), queueList.Qsize); // add a queue size change for the file thread to write in log file
				ReleaseMutex(QMutex);
				
			}
			packetStart = parsePtr + 1; // move to the beggining of next packet if available ( if amount still > 0)
			packetSize = 0;
		}
		amount--;
		parsePtr++;

	}
	// if part of a packet was sent, packetSize will be > 0
	int i = packetSize;
	while (i > 0) {
		*received = *packetStart; // remininng chars will be written to the beginning of the buffer
		received++;
		packetStart++;
		i--;
	}
	return packetSize; // number of chars in the not whole packet is returned
}

DWORD WINAPI recvThreadFunc(LPVOID lpParam) {
	// thread for recieving data from client and inserting jobs in queue
	char buffer[BUFFER_SIZE];
	int remainder = 0, recAmount = 0;
	fd_set readFds;
	
	FD_ZERO(&readFds);
	memset(buffer, 0, BUFFER_SIZE);
	WaitForSingleObject(QMutex, INFINITE);
	while (TRUE) {
		ReleaseMutex(QMutex);
		FD_SET(serverSock, &readFds);
		select(serverSock + 1, &readFds, NULL, NULL, NULL); // blocking select, to block recv thread untill new data is available
		if (0 != WSAGetLastError()) {
			printf("error %d\n", WSAGetLastError());
		}
		if (FALSE == FD_ISSET(serverSock, &readFds)) { // should never be true unless error, blocking select
			printf("no data, error %d\n", WSAGetLastError());
		}
		else {
			// recv will write the bytes stream to the buffer
			// if a packet was not fully sent before, the data will be written in continuance
			recAmount = recv(serverSock, buffer + remainder, BUFFER_SIZE - remainder, 0);
			if (recAmount == 0) { // select did not block the thread, and 0 bytes recieved, the connection closed
				WaitForSingleObject(QMutex, INFINITE);
				ReleaseMutex(QMutex);
				ReleaseSemaphore(QSemaphore, 1, NULL); // in case job thread is stuck waiting for next job but there will be none
				return 0;
			}
			else {
				remainder += recAmount; // remainder + recAmount is the total amount of data available in the buffer
				remainder = parseRecv(buffer, remainder);
			}
		}
		WaitForSingleObject(QMutex, INFINITE);
	}
}

DWORD WINAPI jobsThreadFunc(LPVOID lpParam) {
	// thread for executing the jobs
	job* curJob;
	double mu = *((double*)lpParam);

	while (TRUE) {
		WaitForSingleObject(QSemaphore, INFINITE); // wait if no jobs in queue
		WaitForSingleObject(QMutex, INFINITE);
		if (queueList.Qsize == 0) { // if queue is empty and thread not blocked on semaphore, jobs finished
			ReleaseMutex(QMutex);
			shutdown(serverSock, SD_SEND);
			ReleaseSemaphore(changesSemaphore, 1, NULL);
			return 0;
		}
		curJob = queueList.Qhead; // take first job from queue
		queueList.Qhead = curJob->next; // advance the head of the queue
		queueList.Qsize--;
		addQChange(clock(), queueList.Qsize); // add a change in the queue to be written in log file
		ReleaseMutex(QMutex);
		// get a random number with exponential distribution with param mu, and sleep to simulate job execution
		double u = (double)rand() / RAND_MAX;
		double dt = (-1 / mu) * log(1-u) * 1000;
		Sleep(dt);
		WaitForSingleObject(sockMutex, INFINITE);
		if (-1 == sendAll(serverSock, curJob->jobName, strlen(curJob->jobName)) ||
			-1 == sendAll(serverSock, "F\n", strlen("F\n"))) { // sent to the client the job name and F for finished
			printf("failed to send job finished\n");
		}
		ReleaseMutex(sockMutex);
		free(curJob->jobName);
		free(curJob);
	}
}

DWORD WINAPI fileThreadFunc(LPVOID lpParam) {
	// thread that writes the log file
	FILE* logFile = *((FILE**)lpParam);
	fileEntry* temp;
	int changeCount = 0;
	double totalSize = 0;

	while (TRUE) {
		WaitForSingleObject(changesSemaphore, INFINITE); // wait for a change to be ready to be written
		WaitForSingleObject(changesMutex, INFINITE);
		if (changesHead == NULL) { // if thread not block on semaphore and queue is empty, finished writing
			ReleaseMutex(changesMutex);
			queueList.statisticsAvg = totalSize / changeCount;
			return 0;
		}
		temp = changesHead; // next change to be written
		changesHead = changesHead->next; // advance the linked list
		ReleaseMutex(changesMutex);
		if (logFile != NULL) fprintf(logFile ,"%d %d\n", temp->changeTime, temp->curSize); // write to log file
		totalSize += temp->curSize;
		changeCount++;
		free(temp);
	}
}

void initializeServer(int maxQSize, int runId, FILE** logFileAddr, int seed, double mu, char* fileName) {
	// function to start the server
	// initilize global viable
	queueList.Qsize = 0;
	queueList.Qmax = maxQSize;
	queueList.statisticsMax = 0;
	queueList.statisticsAvg = 0;
	queueList.Qhead = NULL;
	// create semaphores
	if (NULL == (QSemaphore = CreateSemaphore(NULL, 0, queueList.Qmax, NULL))) {
		printf("queue semaphore creation failed\n"); // limited by max queue size
	}
	if (NULL == (changesSemaphore = CreateSemaphore(NULL, 0, MAXLONG, NULL))) {
		printf("log file semaphore creation failed\n"); // limited by max size of semaphore
	}
	if (NULL == (QMutex = CreateMutex(0, FALSE, 0))) { // create mutex for global jobs list
		printf("Error creating queue mutex, safety may be compromised in threads\n");
	}
	if (NULL == (changesMutex = CreateMutex(0, FALSE, 0))) { // create mutex for global log file entries list
		printf("Error creating file changes list mutex, safety may be compromised in threads\n");
	}
	if (NULL == (sockMutex = CreateMutex(0, FALSE, 0))) { // create mutex for socket shared between 2 threads
		printf("Error creating socket mutex, safety may be compromised in threads\n");
	}
	if (NULL == (*logFileAddr = fopen(fileName, "w"))) { // create log file
		printf("opening log file failed\n");
	}
	else {
		fprintf(*logFileAddr, "%s: seed = %d, mu = %g, QSize = %d,", fileName, seed, mu, maxQSize);
		for (int i = 0; i < 50; i++) {
			fprintf(*logFileAddr, " "); // save place for statistics
		}
		fprintf(*logFileAddr, "\n");
	}
}

void createThreads(HANDLE* recvPtr, HANDLE* jobsPtr, HANDLE* filePtr, double* muAddr, FILE** logFileAddr) {
	// function for creating the 3 threads of the program
	if (NULL == (*recvPtr = CreateThread(NULL, 0, recvThreadFunc, NULL, 0, NULL))) {
		printf("failed to create a thread, communication is not possible\n");
		CloseHandle(QMutex);
		closesocket(serverSock);
		WSACleanup();
		return;
	}
	if (NULL == (*jobsPtr = CreateThread(NULL, 0, jobsThreadFunc, muAddr, 0, NULL))) {
		printf("failed to create a thread, jobs will not be executed\n");
	}
	if (NULL == (*filePtr = CreateThread(NULL, 0, fileThreadFunc, logFileAddr, 0, NULL))) {
		printf("failed to create a thread, log file will not be created\n");
	}
}

void waitAndClose(HANDLE* recvPtr, HANDLE* jobsPtr, HANDLE* filePtr, FILE** logFileAddr, char* fileName) {
	// function for closing all threads, mutex, semaphores, socket

	WaitForSingleObject(*recvPtr, INFINITE);
	CloseHandle(*recvPtr);
	WaitForSingleObject(*jobsPtr, INFINITE);
	CloseHandle(*jobsPtr);
	WaitForSingleObject(*filePtr, INFINITE);
	CloseHandle(*filePtr);
	closesocket(serverSock);
	CloseHandle(sockMutex);
	CloseHandle(QMutex);
	CloseHandle(QSemaphore);
	CloseHandle(changesMutex);
	CloseHandle(changesSemaphore);
	if (*logFileAddr != NULL) {
		fclose(*logFileAddr);
		FILE* appendFile = fopen(fileName, "r+");
		if (appendFile != NULL) { // add the statstics to the first line
			int j = 0;
			for (int i = 0; i < 3; i++) {
				char c;
				while (',' != (c = getc(appendFile))) {
					j++;
				}
			}
			fseek(appendFile, j + 1, SEEK_SET);
			fprintf(appendFile, ", max_queue_size = %d, avg_queue_size = %g", queueList.statisticsMax, queueList.statisticsAvg);
			fclose(appendFile);
		}
	}
	WSACleanup();
}

void main(int argc, char* argv[]) {
	// the main function will setup the program, use threads for the accual functionality and wait for them to finish
	WSADATA wsaData;
	SOCKET bindSock;
	HANDLE recvThread = NULL, jobThread = NULL, fileThread = NULL;
	int seed = atoi(argv[2]), runId = atoi(argv[3]), maxQSize = atoi(argv[5]);
	double mu = strtod(argv[4], NULL);
	FILE* logFile = NULL;
	
	srand(seed);
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		printf("Error at WSAStartup\n");
		return;
	}
	struct sockaddr_in my_addr;
	bindSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = INADDR_ANY;
	my_addr.sin_port = htons(atoi(argv[1]));
	bind(bindSock, (SOCKADDR*)&my_addr, sizeof(my_addr));
	listen(bindSock, 10);
	serverSock = accept(bindSock, NULL, NULL);
	closesocket(bindSock);
	char fileName[MAX_FILE_NAME_SIZE];
	sprintf(fileName, "server_%d.log", runId);
	initializeServer(maxQSize, runId, &logFile, seed, mu, fileName);
	createThreads(&recvThread, &jobThread, &fileThread, &mu, &logFile);
	waitAndClose(&recvThread, &jobThread, &fileThread, &logFile, fileName);
	fprintf(stderr, "server_%d.log: seed = %d, mu = %g, QSize = %d", runId, seed, mu, maxQSize);
	fprintf(stderr, ", max_queue_size = %d, avg_queue_size = %g\n", queueList.statisticsMax, queueList.statisticsAvg);
}