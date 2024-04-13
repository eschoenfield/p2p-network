//Ely Schoenfield and Eulises Yair Lopez
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define SERVER_PORT "5432"
#define MAX_LINE 256
#define MAX_PENDING 5
#define MAX_PEERS 5
#define MAX_FILES 10
#define MAX_FILENAME_LEN 100

//used for making new peer object when JOIN is recieved
typedef struct peer_entry {
	uid_t id; // ID of peer
	int socket_descriptor; // Socket descriptor for connection to peer
	char files[MAX_FILES][MAX_FILENAME_LEN]; // Files published by peer
	struct sockaddr_in address; // Contains IP address and port number
}shmemobj;
//reference: writeup
//array of current peers that JOIN'ed the registry
shmemobj* currPeers[MAX_PEERS];
//Initialize currPeers array to NULL


/*
 * Create, bind and passive open a socket on a local interface for the provided service.
 * Argument matches the second argument to getaddrinfo(3).
 *
 * Returns a passively opened socket or -1 on error. Caller is responsible for calling
 * accept and closing the socket.
 */
int bind_and_listen( const char *service );

/*
 * Return the maximum socket descriptor set in the argument.
 * This is a helper function that might be useful to you.
 */
int find_max_fd(const fd_set *fs);

int main(int argc, char *argv[]){
	if(argc != 2){
        fprintf(stderr, "Usage: ./registry portNum\n");
        return -1;
    }
	const char *server_port = argv[1];
	for(int i = 0; i < MAX_PEERS; i++){
    	currPeers[i] = NULL;
	}
	// all_sockets stores all active sockets. Any socket connected to the server should
	// be included in the set. A socket that disconnects should be removed from the set.
	// The server's main socket should always remain in the set.
	fd_set all_sockets;
	FD_ZERO(&all_sockets);
	// call_set is a temporary used for each select call. Sockets will get removed from
	// the set by select to indicate each socket's availability.
	fd_set call_set;
	FD_ZERO(&call_set);

	// listen_socket is the fd on which the program can accept() new connections
	int listen_socket = bind_and_listen(server_port);
	FD_SET(listen_socket, &all_sockets);

	// max_socket should always contain the socket fd with the largest value, just one
	// for now.
	int max_socket = listen_socket;
	while(1) {
		call_set = all_sockets;
		int num_s = select(max_socket+1, &call_set, NULL, NULL, NULL);
		if( num_s < 0 ){
			perror("ERROR in select() call");
			return -1;
		}
		//check each potential socket.
		//skip standard IN/OUT/ERROR -> start at 3.
		for( int s = 3; s <= max_socket; ++s ){
			//skip sockets that aren't ready
			if( !FD_ISSET(s, &call_set) )
				continue;

			//a new connection is ready
			if( s == listen_socket ){
				int newConnection = accept(listen_socket, NULL, NULL);
				if(newConnection < 0){
					perror("accept() error");
					exit(1);
				}
				// and update FD_SET and max_socket
				else{
					//add file descriptor from all_sockets set
					FD_SET(newConnection, &all_sockets);
					//update max_socket
					if(newConnection > max_socket){
						max_socket = newConnection;
					}
				}
			}

			// A connected socket is ready
			else{
				char recvBuf[MAX_LINE];
				int bytesRecv = recv(s, recvBuf, sizeof(recvBuf), 0);
				//error check
				if(bytesRecv < 0){
					perror("recv() error");
					//close the socket
					close(s);
					//remove socket from the set
					FD_CLR(s, &all_sockets);
				}
				//socket closed
				else if(bytesRecv == 0){
					//each socket has only one peer associated
					//files aren't persistent when peers disconnect
					//free space in currPeers
					for(int i=0; i<MAX_PEERS; i++){
						if(currPeers[i] != NULL && currPeers[i]->socket_descriptor == s){
							free(currPeers[i]);
							currPeers[i] = NULL;
							//move peers to front
							if(currPeers[i+1] != NULL){
								for(int j=i; j<MAX_PEERS; j++){
									currPeers[j] = currPeers[j+1];
									// free(currPeers[j+1]);
									if(currPeers[j+1] == NULL){
										break;
									}
								}
							}
							break;
						}
					}
					//close the socket
					close(s);
					//remove socket from the set
					FD_CLR(s, &all_sockets);
				}
				//check for JOIN request action byte
				else if(recvBuf[0] == 0){
					uid_t id;
					uint32_t tempCastID;
					//copy the last four bytes of JOIN req for peer ID
					memcpy(&tempCastID, recvBuf+1, sizeof(uint32_t));
					//cast to correct data type and from network byte order to host byte
					id = (uid_t)ntohl(tempCastID);
				
					struct sockaddr_in addr;
					socklen_t len = sizeof(addr);
					int ret = getpeername(s, (struct sockaddr*)&addr, &len);
					// printf("peer addr: %d\n", addr);
					if(ret < 0){
						printf("error: getpeername()\n");
					}
					//create a new peer_entry
					//check if empty
					if(currPeers[0] == NULL){
						currPeers[0] = malloc(sizeof(shmemobj));
						currPeers[0]->id = id;
						currPeers[0]->address = addr;
						currPeers[0]->socket_descriptor = s;
						for(int f=0; f<MAX_FILES; f++){
							currPeers[0]->files[f][0] = '\0';
						}
						printf("TEST] JOIN %u\n", id);
					}
					//not empty
					else{
						int i;
						for(i=0; i<MAX_PEERS; i++){
							if(currPeers[i] == NULL){
								currPeers[i] = malloc(sizeof(shmemobj));
								currPeers[i]->id = id;
								currPeers[i]->address = addr;
								currPeers[i]->socket_descriptor = s;
								for(int f=0; f<MAX_FILES; f++){
									currPeers[i]->files[f][0] = '\0';
								}
								printf("TEST] JOIN %u\n", id);
								break;
							}
							else if(i==(MAX_PEERS-1) && currPeers[MAX_PEERS-1] != NULL){
								printf("error: max %d peers in registry at a time\n", MAX_PEERS);
							}
						}
					}
				}
				//check for PUBLISH request action byte
				else if(recvBuf[0] == 1){
					//initialize to an invalid value
					int pubPeer = -1;  
					uid_t fileCount;
					uint32_t tempCastCount;

					//copy the last four bytes of PUBLISH req for the number of files
					memcpy(&tempCastCount, recvBuf + 1, sizeof(uint32_t));
					//convert to the correct data type and from network byte order to host byte
					fileCount = (uid_t)ntohl(tempCastCount);
					if(fileCount > MAX_FILES){
						printf("error: too many files\n");
						break;
					}

					for(int i = 0; i < MAX_PEERS; i++){
						// find peer with matching socket desc
						if(currPeers[i] != NULL && currPeers[i]->socket_descriptor == s){
							pubPeer = i;
						}
					}

					int offset = 5;
					//look for each file
					for(int c=0; c < fileCount; c++){
						int sizeCurFile = strlen(recvBuf+offset) + 1;
						// check if the file name is within the allowed length
						if(sizeCurFile > MAX_FILENAME_LEN){
							printf("PUBLISH req error: file name too long\n");
							break;
						}

						for(int insert=0; insert<MAX_FILES; insert++){
							if(currPeers[pubPeer]->files[insert][0] == '\0'){
								memcpy(currPeers[pubPeer]->files[insert], recvBuf + offset, sizeCurFile);								
								offset += sizeCurFile;
								break;
							}
						}

					}

					printf("TEST] PUBLISH %d ", fileCount);
					for(int f =0; f < fileCount; ++f){
						if(f == fileCount-1){
							printf("%s", currPeers[pubPeer]->files[f]);
						}
						else{
							printf("%s ", currPeers[pubPeer]->files[f]);
						}
					}
					printf("\n");

				}

				// //check for SEARCH request action byte
				else if(recvBuf[0] == 2){
					int fileFound = -1;
					char searchFileName[MAX_FILENAME_LEN];
					char sResp[10];
					//intialize to all zeros
					memset(sResp, 0, sizeof(sResp));
					//parse the filename
        			memcpy(searchFileName, recvBuf + 1, sizeof(recvBuf) - 1);
					for(int i = 0; i < MAX_PEERS; i++){
						for(int f = 0; f < MAX_FILES; f++){
							//found file
							if(strcmp(currPeers[i]->files[f], searchFileName) == 0){
								//create SEARCH response
								int tmpID = currPeers[i]->id;
								tmpID = htonl(tmpID);  // Convert ID to network byte order
								memcpy(sResp, &tmpID, sizeof(tmpID));

								//parse currPeers[i]->address into ipAdr and portNum
								uint32_t ipAdr = currPeers[i]->address.sin_addr.s_addr;
								uint16_t portNum = currPeers[i]->address.sin_port;
								char ipStr[INET_ADDRSTRLEN];
                				inet_ntop(AF_INET, &(currPeers[i]->address.sin_addr), ipStr, INET_ADDRSTRLEN);

								//copy to search response
								memcpy(sResp + sizeof(tmpID), &ipAdr, sizeof(ipAdr));
								memcpy(sResp + sizeof(tmpID) + sizeof(ipAdr), &portNum, sizeof(portNum));
								//update flag
								fileFound = 0;
								//print registry output
								printf("TEST] SEARCH %s %u %s:%u\n", searchFileName, currPeers[i]->id, ipStr, ntohs(portNum));
								//send search response
								send(s, sResp, 10, 0);
								break;
							}
						}
						if(fileFound == 0){
							break;
						}
					}
					if(fileFound == -1){
						printf("TEST] SEARCH %s 0 0.0.0.0:0\n", searchFileName);
						memset(sResp, 0, sizeof(sResp));
						send(s, sResp, 10, 0);
					}
				}
				else{
					printf("error\n");
				}
			}
		}
	}
}

int find_max_fd(const fd_set *fs) {
	int ret = 0;
	for(int i = FD_SETSIZE-1; i>=0 && ret==0; --i){
		if( FD_ISSET(i, fs) ){
			ret = i;
		}
	}
	return ret;
}

int bind_and_listen( const char *service ) {
	struct addrinfo hints;
	struct addrinfo *rp, *result;
	int s;

	/* Build address data structure */
	memset( &hints, 0, sizeof( struct addrinfo ) );
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;

	/* Get local address info */
	if ( ( s = getaddrinfo( NULL, service, &hints, &result ) ) != 0 ) {
		fprintf( stderr, "stream-talk-server: getaddrinfo: %s\n", gai_strerror( s ) );
		return -1;
	}

	/* Iterate through the address list and try to perform passive open */
	for ( rp = result; rp != NULL; rp = rp->ai_next ) {
		if ( ( s = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol ) ) == -1 ) {
			continue;
		}

		if ( !bind( s, rp->ai_addr, rp->ai_addrlen ) ) {
			break;
		}

		close( s );
	}
	if ( rp == NULL ) {
		perror( "stream-talk-server: bind" );
		return -1;
	}
	if ( listen( s, MAX_PENDING ) == -1 ) {
		perror( "stream-talk-server: listen" );
		close( s );
		return -1;
	}
	freeaddrinfo( result );

	return s;
}