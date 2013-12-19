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
#include <signal.h>
#include <fcntl.h>

#define SERVER "127.0.0.1"
#define PORT "5794"
#define SIZEBUF 1024
#define PROTO_ACK 1
#define PROTO_RESEND 2
#define PROTO_MARK 3
#define PROTO_END 4


int oobData = 0;
int server;

struct fileInfo
{
    char fileName[255];
    long int offset;
};

struct UdpPacket
{
	unsigned short mark;
	unsigned short data_size;
	unsigned short checksum;
	char data[SIZEBUF];
};

struct sockaddr_in* InitializeAddr(struct sockaddr_in *addr, char *ser, uint16_t port);
int IsNumericString(char *str);
int kbhit(void);

void signal_handler(int sig)
{
	char flag[1];
	recv(server, &flag, 1, MSG_OOB);
	printf("Catch urgent data: %d\n", oobData);
	fflush(stdout);
}

int connectUDP(char *serv, uint16_t port, struct sockaddr_in *server_addr){
	//struct sockaddr_in server_addr;
    InitializeAddr(server_addr, serv, port);
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 100000;
	if (setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
		perror("Error");
	}
    if(server<0)
        perror("Socket error");
    if (connect(server, (struct sockaddr*)server_addr, sizeof(server_addr)) < 0)
        perror("Connect error");
    return server;
}

int startUDP(char *serv, char *port){
	struct sockaddr_in server_addr;
	struct sockaddr_in temp_addr;
    int ser = connectUDP(serv, atoi(port), &temp_addr);
    struct fileInfo fileinfo;
	char t[256];
	char del[256];
	
	strcpy(t, serv);
    socklen_t serverlen = sizeof(temp_addr);
    char buffer[SIZEBUF];
	FILE *f;
	int rc = 1;
	printf("Enter file name ->");
	scanf("%s", fileinfo.fileName);
	f = fopen(fileinfo.fileName, "ab");
	strcpy(del,fileinfo.fileName);
	fileinfo.offset = ftell(f);
	if(sendto(ser,&fileinfo,sizeof(fileinfo),0,(struct sockaddr *) &temp_addr, serverlen )<0)	//send to server file info
        perror("Send download parts error");
	else
		puts("Send fileinfo to server");

	uint16_t new_port;
	if( recvfrom(ser, &new_port , sizeof(port), 0, (struct sockaddr*) &temp_addr, &serverlen) < 0) {
		perror("Can't recieve new port from server");
		return 0;
	}
	printf("New port to connect %hu\n", new_port);
	server = connectUDP(t, new_port, &server_addr);
	puts("Start receiving file...");
	sendto(server,&fileinfo,sizeof(fileinfo),0,(struct sockaddr *) &server_addr, serverlen );

	struct UdpPacket packet;
	int i=0;
	double ft;
	while(rc)
	{
		memset(buffer, 0, SIZEBUF);
		rc = recvfrom(server, &packet, sizeof(packet), 0, (struct sockaddr*) &server_addr, &serverlen);
		//printf("%s pAAAAAAAA\n", packet.data);
		if (strcmp( packet.data , "File already downloaded" )==0)
			{
				puts("File already downloaded!");
				
				fclose(f);
				
				close(server);
				return 0;
			}
		
		if (strcmp( packet.data , "File not found" ) ==0 )
			{
				puts("File not found");
				fclose(f);
				remove(del);
				close(server);
				return 0;
			}
		else if (packet.data_size == 0)
			{
				puts("Server doesn't respond");
				fclose(f);
				close(server);
				return 0;
			}
		if(rc < 0)
		{
			
			puts("Server not response");
			fclose(f);
			close(server);
			return 0;
		}
		int code = PROTO_ACK;
		if(packet.mark == PROTO_MARK) {
			fwrite(&packet.data, packet.data_size, 1, f);
			i++;
			printf("\033[0E%i packs getted", i);
			if(packet.data_size < SIZEBUF)
				break;
			sendto(server, &code, sizeof(code), 0,  (struct sockaddr *)&server_addr, serverlen);
		}
		else
		{
			puts("Data corrupted");
			code = PROTO_RESEND;
			sendto(server, &code, sizeof(code), 0, (struct sockaddr*)&server_addr, serverlen);
		}
		
		
		
	}
	int code = PROTO_END;
	sendto(server, &code, sizeof(code), 0,  (struct sockaddr *)&server_addr, serverlen);
	puts("\nEnd");
	fclose(f);
	close(server);
	return 0;
}

int connectTCP(char *serv,char *port){
	puts(serv);
	int server = socket(AF_INET, SOCK_STREAM, 0);
	if(server < 0) perror("Create socket error");
	struct sockaddr_in dest;
	
	InitializeAddr(&dest, serv, atoi(port));
	int rc = 0;
	if((rc = connect(server, (struct sockaddr *)&dest, sizeof(dest))) < 0){
		perror("Client-connect() error");
		close(server);
		exit(-1);
	}
	return server;
}
void getFileTCP(int server){
	struct fileInfo fileinfo;
	FILE *f;
	printf("Enter file name ->");
	scanf("%s", fileinfo.fileName);
	f = fopen(fileinfo.fileName, "ab");
	fileinfo.offset = ftell(f);
    printf("%lu -> ReadPosition\n", fileinfo.offset);
	int rc = 1;
	if((write(server, (struct fileInfo*)&fileinfo, sizeof(fileinfo))) < 0) {
		perror("File info write error");
		exit(0);
	}
	char buffer[SIZEBUF];
	char flag[1];
	puts("Sending...");
	while(rc)
	{
		memset(buffer, 0, SIZEBUF);
		rc=read(server, buffer, SIZEBUF);
		oobData += rc;
		if(!rc) break;
		fwrite(&buffer, rc, 1, f);
		write(server, flag, 1 );
	}
	puts("End");
	fclose(f);
	close(server);
	return;
}
int startTCP(char *serv, char* port){
	int sock = connectTCP(serv, port);
	fcntl(sock, F_SETOWN, getpid());
	signal(SIGURG, signal_handler);
	getFileTCP(sock);
	return 0;
}

int main(int argc, char *argv[])
{
	char serv[255];
	char port[255];
	char proto[255];
	if(argc > 3)	{
		strcpy(serv, argv[1]);
		strcpy(port, argv[2]);
		strcpy(proto, argv[3]);
	}
	else {
        strcpy(serv, SERVER);
        strcpy(port, PORT);
        strcpy(proto, "tcp");
	}
	printf("Connecting to the %s, port %s, protocol %s.\n", serv, port, proto);
	if(!strcmp(proto, "tcp"))
		startTCP(serv, port);
	if(!strcmp(proto, "udp"))
		startUDP(serv, port);
	return 0;
}


int IsNumericString(char *str)
{
    int i = 0;
    for(i = 0; i < strlen(str); i ++) {
        if((str[i] > 'a' && str[i] < 'z') || (str[i] > 'A' && str[i] < 'Z'))
            return 0;
    }
    return 1;
}

struct sockaddr_in* InitializeAddr(struct sockaddr_in *addr, char *ser, uint16_t port)
{
    struct hostent *hptr;
//    struct servent *sptr;
    addr->sin_family = AF_INET;
    if(!IsNumericString(ser)) {
        if((hptr = gethostbyname(ser)))
            memcpy(&addr->sin_addr.s_addr, hptr->h_addr, hptr->h_length );
    } else {
        addr->sin_addr.s_addr = inet_addr(ser);
    }
   
        addr->sin_port = htons(port);
    return addr;
}


int kbhit(void)
{
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
