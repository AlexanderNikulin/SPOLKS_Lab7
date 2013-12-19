#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>

#define SERVER "127.0.0.1"
#define PORT "5794"
#define PROTO_MARK 3
#define PROTO_ACK 1
#define PROTO_RESEND 2
#define PROTO_END 4


#define SIZEBUF 1024

int oobSend = 0;
FILE *file;
char *name;

struct fileInfo
{
    char fileName[255];
    long int offset;
};

struct UdpPacket {
	unsigned short mark;
	unsigned short data_size;
	unsigned short checksum;
	char data[SIZEBUF];
};

struct request_udp {
        int rsock;
        socklen_t rlen;
        struct sockaddr_in raddr;
        char rbuf[SIZEBUF];
        size_t rbuflen;
};

struct sockaddr_in* InitializeAddr(struct sockaddr_in *addr, char *server, char *port);
int IsNumericString(char *str);
int kbhit(void);

void CreateUdpPacket( struct UdpPacket *packet, char data[SIZEBUF], int size) {
	packet->mark = PROTO_MARK;
	memcpy(packet->data, data, size);
	packet->data_size = size;

}

int connectUDP(uint port) {
	struct sockaddr_in server;
    int sock=socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Opening socket");
        exit(0);
	}
    bzero(&server,sizeof(server));
    server.sin_family=AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port=htons(port);
    int	optval = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
    if (bind(sock,(struct sockaddr *)&server,sizeof(server))<0) 
    {
		perror("Binding error");
		exit(0);
	}
    return sock;
}

int GenerateUDPSocket(int sock, uint16_t *port) {
	struct sockaddr_in local_sockaddr;
    int client_sockfd = socket ( PF_INET, SOCK_DGRAM, IPPROTO_UDP );
           socklen_t local_len = sizeof(local_sockaddr);
    bzero(&local_sockaddr,sizeof(local_sockaddr));
    local_sockaddr.sin_family=AF_INET;
    local_sockaddr.sin_addr.s_addr = INADDR_ANY;
    local_sockaddr.sin_port=htons(0);
    int	optval = 1;
	setsockopt(client_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    if (bind(client_sockfd,(struct sockaddr *)&local_sockaddr,sizeof(local_sockaddr))<0) {
		perror("Binding error");
		exit(0);
	}
	getsockname(client_sockfd, ( struct sockaddr* ) &local_sockaddr, &local_len);
	if(port != NULL)
		*port = ntohs ( local_sockaddr.sin_port );
    return client_sockfd;
}

void sendFileUDP(struct fileInfo fileinfo, int client, struct sockaddr_in dest) {
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 100000;
	if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
	  perror("Error");
	}
	struct fileInfo fileinf;
	FILE *f;
	struct UdpPacket packet;
	char *exception1 = "File not found";
	char *exception2 = "File already downloaded";
	socklen_t serverlen = sizeof(dest);
	char buffer[SIZEBUF];
	if(!(f = fopen(fileinfo.fileName, "rb")))
	{
		puts("File not found!");
		CreateUdpPacket(&packet, exception1, strlen(exception1));
		recvfrom(client, &fileinf, sizeof(fileinf), 0,(struct sockaddr *) &dest,&serverlen);
		sendto(client, &packet, sizeof(packet), 0, (struct sockaddr *) &dest, serverlen);
		fclose(f);
		return;
	}
	
	recvfrom(client, &fileinf, sizeof(fileinf), 0,(struct sockaddr *) &dest,&serverlen);
    printf("name <%s> offset <%lu> b\n", fileinf.fileName, fileinf.offset);
	//
	fseek(f, 0, SEEK_END);
	long int file_size = ftell(f);
	fseek(f, 0, SEEK_SET);
	fseek(f, fileinfo.offset, SEEK_SET);
	if(file_size == fileinfo.offset) {
		puts("File already downloaded!");
		CreateUdpPacket(&packet, exception2, strlen(exception2));
		sendto(client, &packet, sizeof(packet), 0, (struct sockaddr *) &dest, serverlen);
		fclose(f);
		return;
	}
	long int i = 0;
	int rc = 0;
	puts("Start sending file");
	char accept[SIZEBUF];
	int bytesRead = 0;
	for( i = fileinfo.offset; i < file_size; i += SIZEBUF)
	{
		bytesRead = fread(buffer, 1, SIZEBUF, f);
		if(!bytesRead){ 
			puts("Can't read from file");  
			break;
		}
		CreateUdpPacket(&packet, buffer, bytesRead);
		rc = sendto(client, &packet, sizeof(packet), 0, (struct sockaddr *)&dest, serverlen);
		for(;;) {		//check answer
			rc = recvfrom(client, accept, SIZEBUF, 0, (struct sockaddr*) &dest, &serverlen);
			
			if(rc > 0) {
				int code;
				memcpy(&code, accept, sizeof(code));
				if(code == PROTO_RESEND) {
					fseek(f, -bytesRead, SEEK_CUR);
				}
				if(code == PROTO_END) 
				{
					puts("Done.");
					break;
				}
				break;
			}
			else {
				
				puts("Client doesn't respond.");
				close(client);
				fclose(f);
				return;
			}
		}
	}
	puts("Sending for client ended.");
	fclose(f);
	close(client);
	return;
}

void startUDPserver(uint port) {
	fd_set rfds, afds, wfds;
	int nfds;
	int sock = connectUDP(port);
	uint16_t new_port;
	char buf[SIZEBUF];

	int status;
	nfds = getdtablesize();
	FD_ZERO(&afds);
	FD_ZERO(&wfds);
	FD_SET(sock,&afds);
	FD_SET(sock, &wfds);
	int client_socket;
	struct fileInfo fileinfo;
	struct sockaddr_in from;
	
	while(1)
	{
		memcpy(&rfds, &afds, sizeof(rfds));
		socklen_t fromlen = sizeof(from);
		bzero(&from,sizeof(from));
        memset(buf,0,sizeof(buf));
        puts("Waiting connection");
        fflush(stdout);
        if(select(nfds, &rfds, NULL, NULL, (struct timeval *)0) < 0)
			return;
		
		/*if(FD_ISSET(sock, &rfds)) {
			recvfrom(sock, &fileinfo, sizeof(fileinfo), 0,(struct sockaddr *) &from,&fromlen); //receive file info from client
			printf("File name %s offset %lu b\n", fileinfo.fileName, fileinfo.offset);
			client_socket = GenerateUDPSocket(sock, &new_port);        						//generated new socket and new port for parallel udp
			FD_SET(client_socket, &afds);
		}	*/		
		for(client_socket = 0; client_socket < nfds; client_socket ++)
		{
			if(FD_ISSET(sock, &rfds)) {
				recvfrom(sock, &fileinfo, sizeof(fileinfo), 0,(struct sockaddr *) &from,&fromlen); //receive file info from client
				printf("File name %s offset %lu b\n", fileinfo.fileName, fileinfo.offset);
				client_socket = GenerateUDPSocket(sock, &new_port);        						//generated new socket and new port for parallel udp
				FD_SET(client_socket, &afds);
					
				puts("###test_string###");
				fflush(stdout);
				status = sendto(sock, &new_port, sizeof(new_port), 0, (struct sockaddr*)&from, fromlen); //send to client new port number for reconnect to server
				if(status < 0) {
					perror("Can't send new port to client");
				}
				else
					printf("Send to client port %hu\n", new_port);
				switch(fork())
				{
					case 0 :
						
						//recvfrom(sock, &fileinfo, sizeof(fileinfo), 0,(struct sockaddr *) &from,&fromlen);
						puts("Send info:");
						sendFileUDP(fileinfo, client_socket, from);								//start sending file to client
						
						exit(0);
					case -1 :
						perror("Error fork()");
						break;
					default: 
						break;
				}
				FD_CLR(client_socket, &rfds);
			}
		}
				sock = connectUDP(port);
	}
}

int connectTCP(char *serv, char *port) {
	struct sockaddr_in server;
	int sock;
	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Server-socket() error");
	} 
	int length = sizeof(server);
	bzero(&server,length);
    InitializeAddr(&server, serv, port);
    if(bind(sock, (struct sockaddr*)&server, length) < 0)
    {
		perror("Binding error");
		exit(0);
	}
	listen(sock, 8);
	return sock;
}

void sendFileTCP(void* sockDescriptor) {
	int sock = (intptr_t)sockDescriptor;
	struct fileInfo fileinfo;
	int rc = read(sock, (struct fileInfo*)&fileinfo, sizeof(fileinfo));//чтение имени файла
	printf("File name -> %s\n", fileinfo.fileName);
	printf("%lu \n", fileinfo.offset);
	FILE *f;
	
	if ((f=fopen(fileinfo.fileName,"rb")))
	{
		puts("File is open");
		char flag[1];
		
		fseek(f, 0, SEEK_END);
		long int file_size = ftell(f);
		fseek(f, 0, SEEK_SET);
		
		long int bytesSend = 0;
		printf("%lu - offset\n", fileinfo.offset);
		fseek(f, fileinfo.offset, SEEK_SET);
		printf("New position-> %lu\n", ftell(f));
		int i = 0;
		char buffer[SIZEBUF];
		for(i = fileinfo.offset; i < file_size; i += SIZEBUF)
		{
			rc=fread(buffer, 1, SIZEBUF, f); // read file
			if(!rc) break;
			rc=write(sock, buffer, rc);	//send to client
			bytesSend += rc;
			/*__fpurge(stdin);
			if(kbhit()) {
				printf("Transmitted data %lu\n", bytesSend);
				rc = send(sock, "q", 1, MSG_OOB);
				if(!rc){ perror("Wriomg!"); puts("Something wrong!\n"); break;}
			}*/
			if(!rc ) break;
			rc = read(sock, flag, 1);
			if(!rc ) break;
		}
		fclose(f);
	}	
	close(sock);
	return;
}

void startTCPserver(char *serv,char *port) {
	int sock;
	fd_set rfds, afds;
	int nfds;
	
	int	optval = 1;
	intptr_t client;
	struct sockaddr_in from;
	socklen_t fromlen;
	
	sock = connectTCP(serv, port);
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof( optval));

	nfds = getdtablesize();
	FD_ZERO(&afds);
	FD_SET(sock, &afds);
	
	fromlen = sizeof(struct sockaddr_in );
	while(1)
	{
		memcpy(&rfds, &afds, sizeof(rfds));
		puts("Waiting incoming...");
		if(select(nfds, &rfds, NULL, NULL, (struct timeval *)0) < 0)
			return;
		if(FD_ISSET(sock, &rfds)) {
			client = accept(sock, (struct sockaddr *)&from, &fromlen);
			printf("Have connection from the client: %s\n", inet_ntoa(from.sin_addr));
			FD_SET(client, &afds);
		}
		for(client = 0; client < nfds; client ++)
		{
			if((client != sock) && FD_ISSET(client, &rfds)) {
				switch(fork()){
					case -1: exit(0);
					case 0: {
						puts("I'm child");
						close(sock);
						sendFileTCP((void*)client);
						puts("I was so young....");
						exit(0);
					}
					default : {
						sleep(1);
						close(client);
					}
				}
				FD_CLR(client, &afds);
			}
		}
	}
}


int main(int argc, char *argv[]) {
	char server[255];
	char port[255];
	char proto[255];
	if(argc > 3)	{
		strcpy(server, argv[1]);
		strcpy(port, argv[2]);
		strcpy(proto, argv[3]);

	}
	else {
		strcpy(server, SERVER);
		strcpy(port, PORT);
		strcpy(proto, "tcp");
	}
	if(argc == 2) {
		strcpy(proto, argv[1]);
	}
	printf("Connecting to the %s, port %s, protocol %s.\n", server, port, proto);
    if(!strcmp(proto, "tcp")) {
		puts("Using TCP protocol");
		startTCPserver(server, port);
	}
	if(!strcmp(proto, "udp")) {
		puts("Using UDP protocol");
		startUDPserver(atoi(port));
	}
	puts("Uncorrect proto");
	return 0;
}


struct sockaddr_in* InitializeAddr(struct sockaddr_in *addr, char *server, char *port) {
    struct hostent *hptr;
    struct servent *sptr;
    addr->sin_family = AF_INET;
    if(!IsNumericString(server)) {
        if((hptr = gethostbyname(server)))
            memcpy(&addr->sin_addr.s_addr, hptr->h_addr, hptr->h_length );
    } else {
        addr->sin_addr.s_addr = inet_addr(server);
    }
    if(!IsNumericString(port))
    {
        if((sptr = getservbyname(port, "tcp")))
            addr->sin_port = sptr->s_port;
    } else {
        addr->sin_port = htons((unsigned short) atoi(port));
    }
    return addr;
}

int IsNumericString(char *str) {
    int i = 0;
    for(i = 0; i < strlen(str); i ++) {
        if((str[i] > 'a' && str[i] < 'z') || (str[i] > 'A' && str[i] < 'Z'))
            return 0;
    }
    return 1;
}

int kbhit(void) {
	struct termios oldt, newt;
	int ch;
	int oldf;

	tcgetattr(STDIN_FILENO, &oldt);
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

	ch = getchar();

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	if(ch != EOF)
	{
		ungetc(ch, stdin);
		return 1;
	}
	return 0;
}
