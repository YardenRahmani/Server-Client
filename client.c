#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#define BUFFER_SIZE 1032
#define FILE_MAX_NAME_SIZE 32

#include "winsock2.h"
#include "stdio.h"
#include "string.h"
#include "time.h"
#include "math.h"

#pragma comment(lib, "ws2_32.lib")

typedef struct fileEntry {
	int genTime; // time a job was generated by client
	int endProcTime; // time a job finished (time notice was recieved by client)
	struct fileEntry* next;
} fileEntry; // struct for saving data of log file in linked list, each line will be a node

struct threadArgs {
	double lambda;
	double T;
}; // struct for giving arguments to the send thread

struct {
	int maxTotalTime;
	int minTotalTime;
	double avgTotalTime;
} statistics;

SOCKET clientSock; // socket for communication with server, one thread send and the other receive, no need for mutex
fileEntry* entriesHead = NULL; // global linked list for the log file entries
HANDLE logSemaphore = NULL; // mutex to ensure the nodes in the log file linked list are created before job termination is written
clock_t startClock; // clock for program start

fileEntry* createFileEntry(int genTime) {
	// create a new entry for log file
	fileEntry* newEntry = (fileEntry*)malloc(sizeof(fileEntry));
	if (newEntry != NULL) {
		newEntry->genTime = genTime; // save the generation time of a job
		newEntry->endProcTime = 0; // end time initialized to 0 and changes if job not dropped
		newEntry->next = NULL;
	}
	return newEntry;
}

void addFileEntry(clock_t genClock) {
	// saving the log file entries in a linked list by the order of jobs creation
	fileEntry* newEntry;

	if (NULL == (newEntry = createFileEntry(1000 * (double)genClock / CLOCKS_PER_SEC))) {
		printf("error creating file entry, log file will not be reliable\n");
		return;
	}
	if (entriesHead == NULL) {
		entriesHead = newEntry;
	}
	else {
		fileEntry* iter = entriesHead;
		while (iter->next != NULL) {
			iter = iter->next;
		}
		iter->next = newEntry;
	}
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

int getJobNum(char* ptr) {
	// turn the string of job number to an int, reading from right to left
	int res = 0, exp = 1;

	while (isdigit(*ptr)) {
		res += (*ptr - '0') * exp;
		exp *= 10;
		ptr--;
	}
	return res;
}

void checkStatistics(fileEntry* curJob) {
	// check for job statistics, since only one thread has access no need for mutex
	int jobTime = curJob->endProcTime - curJob->genTime;

	if (jobTime > statistics.maxTotalTime) {
		statistics.maxTotalTime = jobTime;
	}
	if (jobTime < statistics.minTotalTime) {
		statistics.minTotalTime = jobTime;
	}
	statistics.avgTotalTime += jobTime;
}

int parseRecv(char* received, int amount, clock_t recvClock, int* dropsPtr) {
	// a function to parse the incoming bytes stream from the server
	char* parsePtr = received, * packetStart = received;
	int packetSize = 0, jobNum;
	fileEntry* iter;

	while (amount > 0) {
		// the masseges will come in format of jobXF\n or jobXT\n, where X is the job number, T for thrown, F for finished
		if (*parsePtr != '\n') { // look for the end of a packet
			packetSize++;
		}
		else {
			jobNum = getJobNum(parsePtr - 2);
			WaitForSingleObject(logSemaphore, INFINITE); // for case node for specific job was not created yet by other thread
			iter = entriesHead;
			while (jobNum > 0) { // find the job in the linked list
				iter = iter->next;
				jobNum--;
			}
			if (*(parsePtr - 1) == 'F'){ // F is a state of job finished
				iter->endProcTime = 1000 * (double)recvClock / CLOCKS_PER_SEC;
				checkStatistics(iter);
			}
			else { // T is a state of job thrown
				(*dropsPtr)++;
			}
			packetSize = 0;
			packetStart = parsePtr + 1;
		}
		amount--;
		parsePtr++;
	}
	int i = packetSize; // if part of a packet was sent it will be coppied to the begginning of the buffer
	while (i > 0) {
		*received = *packetStart;
		received++;
		packetStart++;
		i--;
	}
	return packetSize; // the amount of chars in the not full packet or zero
}

DWORD WINAPI recvThreadFunc(LPVOID lpParam) {
	// thread for receiveing data from server
	char buffer[BUFFER_SIZE];
	int recAmount = 0, remainder = 0, totalDrops = 0;
	fd_set readFds;

	FD_ZERO(&readFds);
	memset(buffer, 0, BUFFER_SIZE);
	while (TRUE) {
		FD_SET(clientSock, &readFds);
		select(clientSock + 1, &readFds, NULL, NULL, NULL); // blocking select
		if (0 != WSAGetLastError()) {
			printf("error %d\n", WSAGetLastError());
		}
		if (FALSE == FD_ISSET(clientSock, &readFds)) { // should never be true unless error, blocking select
			printf("no data, error %d\n", WSAGetLastError());
		}
		else {
			recAmount = recv(clientSock, buffer + remainder, BUFFER_SIZE - remainder, 0);
			clock_t curClock = clock(); // save the clock of the packet arrive
			if (recAmount == 0) { // since the select is blocking, recAmount is 0 when connection closed
				return totalDrops;
			}
			else {
				remainder += recAmount; // total bytes count currently in buffer
				remainder = parseRecv(buffer, remainder, curClock, &totalDrops);
			}
		}
	}
}

DWORD WINAPI sendThreadFunc(LPVOID lpParam) {
	// thread for sending jobs
	struct threadArgs args = *((struct threadArgs*)lpParam);
	double lambda = args.lambda;
	double T = args.T;
	int jobCount = 0;
	char* temp = NULL;

	while (TRUE) {
		double u = (double)rand() / RAND_MAX; // jobs arrive in poisson distribution, time between event is exponential
		double dt = (-1 / lambda) * log(1-u) * 1000;
		Sleep((int)dt);
		time_t curClock = clock();
		if (((double)startClock / CLOCKS_PER_SEC) + T < (double)curClock / CLOCKS_PER_SEC) {
			// if current time is greater then start time + T, its time to finish
			shutdown(clientSock, SD_SEND);
			return jobCount;
		}
		int k = strlen("job\n") + log10(jobCount + 1) + 2; // size of chars array needed for job name
		if (NULL == (temp = (char*)malloc(k * sizeof(char)))) {
			printf("allocation failed\n");
		}
		else {
			sprintf(temp, "job%d\n", jobCount); // create the packet in format "jobX\n" where X is the job number
			if (-1 == sendAll(clientSock, temp, strlen(temp))) {
				printf("error in sending a job\n");
			}
			addFileEntry(clock()); // add a line to the log file for a new job
			ReleaseSemaphore(logSemaphore, 1, NULL); // mark a new line for the log file created
			jobCount++;
			free(temp);
		}
	} 
}

void logFileWrite(FILE* logFile) {
	// write the jobs statistics, no need for mutex as threads finished
	fileEntry* curWrite = entriesHead, * temp;

	while (curWrite != NULL) {
		fprintf(logFile, "%d ", curWrite->genTime);
		if (curWrite->endProcTime == 0) { // case job was thrown
			fprintf(logFile, "0 0\n");
		}
		else { // case job was finished
			fprintf(logFile, "%d %d\n", curWrite->endProcTime, curWrite->endProcTime - curWrite->genTime);
		}
		temp = curWrite;
		curWrite = curWrite->next;
		free(temp);
	}
}

int initializeClient(char* ip, int port, int runId) {
	// function for connecting to client, create mutex

	struct sockaddr_in remote_addr;
	clientSock = socket(AF_INET, SOCK_STREAM, 0);
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_addr.s_addr = inet_addr(ip);
	remote_addr.sin_port = htons(port);
	if (0 != connect(clientSock, (SOCKADDR*)&remote_addr, sizeof(remote_addr))) {
		printf("Error in client's connect(), unable to connect to server\n");
		WSACleanup();
		return -1;
	}
	if (NULL == (logSemaphore = CreateSemaphore(NULL, 0, MAXLONG, NULL))) {
		printf("mutex creation failed, safety may be compromised\n");
	}
}

void createThreads(HANDLE* sendPtr, HANDLE* recvPtr, struct threadArgs* argsAddr, FILE** fileAddr) {
	// function for creating the programs threads
	if (NULL == (*sendPtr = CreateThread(NULL, 0, sendThreadFunc, argsAddr, 0, NULL))) {
		printf("failed to create a thread, communication is not possible\n");
		closesocket(clientSock);
		WSACleanup();
		return;
	}
	if (NULL == (*recvPtr = CreateThread(NULL, 0, recvThreadFunc, NULL, 0, NULL))) {
		printf("failed to create a thread, communication will be only from client to server\n");
	}
}

void waitAndClose(HANDLE* sendPtr, HANDLE* recvPtr, int runId, int seed, struct threadArgs args) {
	// function for waiting on threads, closing mutex and write log file
	int totalPkts, totalDrops;
	char fileName[FILE_MAX_NAME_SIZE];
	FILE* logFile = NULL;

	WaitForSingleObject(*sendPtr, INFINITE);
	GetExitCodeThread(*sendPtr, &totalPkts);
	CloseHandle(*sendPtr);
	WaitForSingleObject(*recvPtr, INFINITE);
	GetExitCodeThread(*recvPtr, &totalDrops);
	CloseHandle(*recvPtr);
	CloseHandle(logSemaphore);
	closesocket(clientSock);
	statistics.avgTotalTime = statistics.avgTotalTime / (totalPkts - totalDrops);
	sprintf(fileName, "client_%d.log", runId);
	if (NULL == (logFile = fopen(fileName, "w"))) {
		printf("failed to create log file\n");
	}
	else {
		fprintf(logFile, "client_%d.log: seed = %d, lambda = %g, T = %g", runId, seed, args.lambda, args.T);
		fprintf(logFile, ", total_pkts = %d, total_drops = %d", totalPkts, totalDrops);
		fprintf(logFile, ", total_time_min = %d, total_time_max = %d", statistics.minTotalTime, statistics.maxTotalTime);
		fprintf(logFile, ", total_time_avg = %g\n", statistics.avgTotalTime);
		logFileWrite(logFile);
		fclose(logFile);
	}
	WSACleanup();
	fprintf(stderr, "client_%d.log: seed = %d, lambda = %g, T = %g", runId, seed, args.lambda, args.T);
	fprintf(stderr, ", total_pkts = %d, total_drops = %d", totalPkts, totalDrops);
	fprintf(stderr, ", total_time_min = %d, total_time_max = %d", statistics.minTotalTime, statistics.maxTotalTime);
	fprintf(stderr, ", total_time_avg = %g\n", statistics.avgTotalTime);
}

void main(int argc, char* argv[]) {
	// the main function initialize the client, create threads, wait for them to finish and write the log file
	startClock = clock();
	WSADATA wsaData;
	HANDLE recvThread, sendThread;
	struct threadArgs args;
	FILE* logFile = NULL;
	
	args.lambda = strtod(argv[5], NULL);
	args.T = strtod(argv[6], NULL);
	int seed = atoi(argv[3]);
	srand(seed);
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		printf("Error at WSAStartup\n");
		return;
	}
	if (-1 == initializeClient(argv[1], atoi(argv[2]), atoi(argv[4]))) {
		return;
	}
	createThreads(&sendThread, &recvThread, &args, &logFile);
	waitAndClose(&sendThread, &recvThread, atoi(argv[4]), seed, args);
}