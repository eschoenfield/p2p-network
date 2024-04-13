//Author:   Ely Schoenfield & Eulises Yair Lopez
//Class:    EECE-446
//Semester: Spring 2023

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h> 
#include <dirent.h>
#include <arpa/inet.h>

#define MAX_LINE 256

int lookup_and_connect( const char *host, const char *service );
void join(int socket, uint32_t ID);
void publish(int socket);
void search(int socket);
void fetch(int socket);

int main(int argc, char *argv[]){
	if(argc != 4){
        fprintf(stderr, "Usage: ./peer IP_Address/Hostname Port_Num Peer_ID\n");
		//IP:   if jaguar use localhost or 127.0.0.1
		//port: 2000 < port < 65536
		//ID:   1 < ID < 4294967296
        exit(1);
    }

	char cmd[10];
	int s;
	char *host = argv[1];
	char *portNum = argv[2];
	uint32_t peerID = atoi(argv[3]);
	if((s = lookup_and_connect(host, portNum)) < 0){
		exit(1);
	}
	
	while(1){
		printf("Enter a command: ");
		scanf("%s",cmd);
		if(strncmp(cmd,"JOIN",5)==0){
			//send a JOIN request to the registry
			join(s, peerID);
		}
		else if(strncmp(cmd,"PUBLISH",8)==0){
			//send a PUBLISH request to the registry
			publish(s);
		}
		else if(strncmp(cmd,"SEARCH",7)==0){
			//send a SEARCH request to the registry
			search(s);
		}
		else if(strncmp(cmd,"FETCH",6)==0){
			//send a FETCH request to the registry
			fetch(s);
		}
		else if(strncmp(cmd,"EXIT",5)==0){
			//exit the peer application
			break;
		}
	}
	return 0;
}

int lookup_and_connect( const char *host, const char *service ) {
	struct addrinfo hints;
	struct addrinfo *rp, *result;
	int s;
	/* Translate host name into peer's IP address */
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;
	if ( ( s = getaddrinfo( host, service, &hints, &result ) ) != 0 ) {
		fprintf( stderr, "stream-talk-client: getaddrinfo: %s\n", gai_strerror( s ) );
		return -1;
	}
	/* Iterate through the address list and try to connect */
	for ( rp = result; rp != NULL; rp = rp->ai_next ) {
		if ( ( s = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol ) ) == -1 ) {
			continue;
		}
		if ( connect( s, rp->ai_addr, rp->ai_addrlen ) != -1 ) {
			break;
		}
		close( s );
	}
	if ( rp == NULL ) {
		perror( "stream-talk-client: connect" );
		return -1;
	}
	freeaddrinfo( result );
	return s;
}

void join(int socket, uint32_t ID){
	//action 0 signifies join action
	uint8_t action = 0;
	//switch host byte order to network byte order
	uint32_t netID = htonl(ID);
	//buffer is 5 Bytes, 1B for action and 4B for ID
	char buf[5];
	//copy action and netID memory to buffer
	memcpy(buf, &action, 1);
	memcpy(buf+1, &netID, 4);
	//send join request
	if(send(socket, buf, sizeof(buf), 0) < 0){
		perror("join() error\n");
		exit(1);
	}
}

void publish(int socket) {
    //open SharedFiles folder
    DIR *dir = opendir("SharedFiles");
    if(dir == NULL){
        perror("no SharedFiles folder");
        return;
    }
    //PUBLISH action is 1
    uint8_t action = 1;
    uint32_t numFiles = 0;
	//size is equal to 1B for action and 4B for file count
    int totalSize = 5;
    //read files in SharedFiles directory
    struct dirent *file;
    while((file = readdir(dir)) != NULL){
		//check for regular files, don't publish directories or recurse into subdirectories
        if(file->d_type == DT_REG){
            //file name should always be less than 100 bytes
            if(strlen(file->d_name) > 100){
				//continue: skips this file index and continues indexing through ShareFiles folder
                continue;
            }
            //append file name to PUBLISH request (+1 is for NULL terminator)
            totalSize += strlen(file->d_name) + 1;
            numFiles++;
        }
    }
    //SharedFiles is empty
    if(numFiles == 0){
        return;
    }
    //request size should always be less than 1200 Bytes
    if(totalSize > 1200){
        perror("PUBLISH request size is larger than 1200 Bytes");
		exit(1);
    }
    //create buffer with max size of the PUBLISH request
	char buf[1200];
	//tailBuf is the tail index of buf
	int tailBuf = 5;
    //copy action and file count (network byte order) to the buffer
    memcpy(buf, &action, 1);
    uint32_t netNumFiles = htonl(numFiles);
    memcpy(buf + 1, &netNumFiles, 4);
    //reset file stream for copying
    rewinddir(dir);
    //copy file names to the buffer
    while((file = readdir(dir)) != NULL){
        if(file->d_type == DT_REG) {
            strcpy(buf + tailBuf, file->d_name);
        	tailBuf += strlen(file->d_name) + 1;
        }
    }
    //send PUBLISH request
    if(send(socket, buf, totalSize, 0) < 0){
        perror("send error");
		exit(1);
    }
    closedir(dir);
}

void search(int socket) {
    //SEARCH action 2
    uint8_t action = 2;
    //read file name input
    char fileName[101];
    printf("Enter a file name: ");
    scanf("%s", fileName);
    //create SEARCH request
	//get size of request
    int totalSize = 1 + strlen(fileName) + 1;
	//create buffer size of request
    char buf[totalSize];
    buf[0] = action;
    strcpy(buf + 1, fileName);
    //send SEARCH request
    if(send(socket, buf, totalSize, 0) < 0){
        perror("send error");
        exit(1);
    }
    //receive the SEARCH response
    uint8_t response[10];
    if(recv(socket, response, sizeof(response), 0) < 0){
        perror("recv error");
        exit(1);
    }

    //parse the Search response
    uint32_t peerID = ntohl(*(uint32_t *)(response));			//4 Byte ID
    uint32_t peerAddress = ntohl(*(uint32_t *)(response + 4));	//4 Byte Adr
    uint16_t peerPort = ntohs(*(uint16_t *)(response + 8));		//2 Byte Port

	//response will return zeros for peerID, Address, and Port if file is not found
    if(peerID != 0){
        printf("File found at\n");
        printf("Peer %u\n", peerID);
        printf("%u.%u.%u.%u", (peerAddress >> 24) & 0xFF, (peerAddress >> 16) & 0xFF, (peerAddress >> 8) & 0xFF, peerAddress & 0xFF);
        printf(":%u\n", peerPort);
    } 
    else {
        printf("File not indexed by registry\n");
    }
}

void fetch(int socket){
    //SEARCH action 2
    uint8_t action = 2;
	//file name is 100 B plus the NULL terminator
    char fileName[101];
    //read file name input
    printf("Enter a file name: ");
    scanf("%s", fileName);
    //create SEARCH request
	//get size of request
    int totalSize = 1 + strlen(fileName) + 1;
	//create buffer size of request
    char buf[totalSize];
    buf[0] = action;
    strcpy(buf + 1, fileName);
	//send SEARCH request
    if(send(socket, buf, totalSize, 0) < 0){
        perror("send error");
        exit(1);
    }
    //receive the SEARCH response
	uint8_t response[10];
    if(recv(socket, response, sizeof(response), 0) < 0) {
        perror("recv error");
        exit(1);
    }

    //parse the SEARCH response
    uint32_t peerAddress = ntohl(*(uint32_t *)(response + 4));	//4 Byte Adr
    uint16_t peerPort = ntohs(*(uint16_t *)(response + 8));		//2 Byte Port

	uint32_t reverseAddress = htonl(peerAddress);
	//convert peerAddress to char array
	char newHost[INET_ADDRSTRLEN];
	if(inet_ntop(AF_INET, &reverseAddress, newHost, INET_ADDRSTRLEN) == NULL){
		//error check
		perror("inet_ntop error");
	}

	//convert peerPort to char array
	char newService[6];
	snprintf(newService, sizeof(newService), "%d", peerPort);

    //connect to peer that PUBLISH'ed file
	int peerSocket=0;
	if((peerSocket = lookup_and_connect(newHost, newService)) < 0){
		exit(1);
	}

	//FETCH action 3
    uint8_t FETCHaction = 3;
	int peerBufSize = 1 + strlen(fileName) + 1;
	//create buffer size of request
	char peerBuf[peerBufSize];
    peerBuf[0] = FETCHaction;
    strcpy(peerBuf + 1, fileName);
    //send FETCH request
	if(send(peerSocket, peerBuf, totalSize, 0) < 0){
        perror("send error");
        exit(1);
    }

	FILE *fstream;
	//open file (binary file 'w+b')
	if((fstream = fopen(fileName,"w+b")) == NULL){
		perror("error opening file");
		exit(1);
	}
    int firstLoop = 1;
	int curBytesRec = 0;
	int chunkSize = 10;
	char finalBuf[chunkSize];
    //partial receives and writes
	while((curBytesRec = recv(peerSocket, finalBuf, sizeof(finalBuf), 0)) > 0){
        if(firstLoop == 1){
            //error check FETCH response
            if(finalBuf[0] != 0){
                perror("FETCH recv error");
		        exit(1);
            }
            firstLoop = 0;
            //remove action Byte from being written to file
            curBytesRec -= 1;
            for(int i = 0; i < curBytesRec; i++){
                finalBuf[i] = finalBuf[i + 1];
            }
        }
        //write to file
        fwrite(finalBuf, 1, curBytesRec, fstream);
	}
    //close file stream
	fclose(fstream);
}