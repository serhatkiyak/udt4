#ifndef WIN32
   #include <arpa/inet.h>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <udt.h>
#include <errno.h>

using namespace std;

static int MAX_PARALLEL_FILES = 5;
static int BUFFER_SIZE = 1024;

static void *file_downloader(void *arg);
int main_recv(char* server_ip, char* server_port, char* remote_filename, char* local_filename);
int main(int argc, char* argv[]);

struct file_downloader_parameter
{
	char server_ip[1024];
	char server_port[1024];
	char remote_filename[1024];
	char local_filename[1024];
};

static void *file_downloader(void *arg)
{
	struct file_downloader_parameter *p = (struct file_downloader_parameter *) arg;
	main_recv(p->server_ip, p->server_port, p->remote_filename, p->local_filename);
	free(p);
}


int main_recv(char* server_ip, char* server_port, char* remote_filename, char* local_filename)
{
   // use this function to initialize the UDT library
   UDT::startup();

   struct addrinfo hints, *peer;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   UDTSOCKET fhandle = UDT::socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);

   if (0 != getaddrinfo(server_ip, server_port, &hints, &peer))
   {
      cout << "incorrect server/peer address. " << server_ip << ":" << server_port << endl;
      return -1;
   }

   // connect to the server, implict bind
   if (UDT::ERROR == UDT::connect(fhandle, peer->ai_addr, peer->ai_addrlen))
   {
      cout << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   freeaddrinfo(peer);


   // send name information of the requested file
   int len = strlen(remote_filename);

   if (UDT::ERROR == UDT::send(fhandle, (char*)&len, sizeof(int), 0))
   {
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   if (UDT::ERROR == UDT::send(fhandle, remote_filename, len, 0))
   {
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   // get size information
   int64_t size;

   if (UDT::ERROR == UDT::recv(fhandle, (char*)&size, sizeof(int64_t), 0))
   {
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   if (size < 0)
   {
      cout << "no such file " << remote_filename << " on the server\n";
      return -1;
   }

   // receive the file
   fstream ofs(local_filename, ios::out | ios::binary | ios::trunc);
   int64_t recvsize; 
   int64_t offset = 0;

   if (UDT::ERROR == (recvsize = UDT::recvfile(fhandle, ofs, offset, size)))
   {
      cout << "recvfile: " << UDT::getlasterror().getErrorMessage() << endl;
      return -1;
   }

   UDT::close(fhandle);

   ofs.close();

   // use this function to release the UDT library
   UDT::cleanup();

   return 0;
}	

int main(int argc, char* argv[])
{
   if ((argc < 5) || (argc % 2 == 0) || (0 == atoi(argv[2])))
   {
      cout << "usage: recvfile server_ip server_port remote_filename1 local_filename1 remote_filename2 local_filename2 ..." << endl;
      return -1;
   }

   int numOfFiles = (argc - 3) / 2;
   if(numOfFiles > MAX_PARALLEL_FILES)
   {
      numOfFiles = MAX_PARALLEL_FILES;
      cout<<"Downloading up to "<<MAX_PARALLEL_FILES<<" parallel files at once!"<<endl;
   }

   char* server_ip = argv[1];
   char* server_port = argv[2];

	int s;
	struct addrinfo hints;
    	struct addrinfo *result;

	memset(&hints, 0, sizeof(struct addrinfo));
   	hints.ai_family = AF_INET;  
   	hints.ai_socktype = SOCK_STREAM; 
   	hints.ai_flags = 0;
    	hints.ai_protocol = 0;

	s = getaddrinfo(server_ip, server_port, &hints, &result);
        if (s != 0) 
	{
       		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        	return -1;
        } 

	int sock;
	int ret = 1;
	char receive_buf[BUFFER_SIZE];

	struct addrinfo *rp;

	for (rp = result; rp != NULL; rp = rp->ai_next)
        {
	        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        	if (sock == -1)
		{
            		continue;
		}

       		if ((ret = connect(sock, rp->ai_addr, rp->ai_addrlen)) != -1)
		{
            		break;
		}        

    		close(sock);
    	}

	if (ret != 0) 
	{
		printf ("Errno: %s & ERROR: connect failed in first connection trial 11111111\n",strerror(errno));
		exit(1);
	}	

	read (sock, receive_buf, BUFFER_SIZE);
	int thread_port = atoi(receive_buf);

	close(sock);
	sock = socket (AF_INET, SOCK_STREAM, 0);

	char *ipAddress = rp->ai_addr->sa_data;
	struct sockaddr_in sa;

	bzero(&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(thread_port);
	inet_pton (AF_INET, ipAddress, &sa.sin_addr);
	
	int t;
	for(t=0; t<5; t++)
	{	
		ret = connect (sock, (const struct sockaddr *) &sa, sizeof (sa));
		if(ret != 0)
		{
			sleep(1);
		}
		else
		{
			break;
		}
	}

	if (ret != 0) 
	{
		printf ("Errno: %s & ERROR: connect failed in second connection trial\n",strerror(errno));
		exit(1);
	}


	int cur_id=0;
	int ids[numOfFiles];
	int i;
	for(i=0; i<numOfFiles; i++)
	{
		ids[i] = -1;
	}

	int curFile = 1;
	while(curFile <= numOfFiles)
	{
   		char* remote_filename = argv[2*curFile+1];
  		char* local_filename = argv[2*curFile+2];		

		int test = write(sock, remote_filename, BUFFER_SIZE);
		if(test == -1)
		{
			cout<<"Something is wrong with writing!"<<endl;
		}

		test = read(sock, receive_buf, BUFFER_SIZE);
		if(test == -1)
		{
			cout<<"Something is wrong with reading!"<<endl;
		}

		pthread_t tid;
		struct file_downloader_parameter *par = (struct file_downloader_parameter*) malloc(sizeof(struct file_downloader_parameter)); 
		
		strcpy(par->server_ip, server_ip);
		strcpy(par->server_port, receive_buf);
		strcpy(par->remote_filename, remote_filename);
		strcpy(par->local_filename, local_filename);

		ret = pthread_create(&(tid), NULL, &file_downloader, (void *) par);
		if (ret != 0) 
		{
			printf("ERROR: thread create failed \n");
			exit(1);
		}

		ids[cur_id++] = tid;

		curFile++;
	
	}

	int test = write(sock, "EXIT", BUFFER_SIZE);
	if(test == -1)
	{
		cout<<"Something is wrong with writing!"<<endl;
	}

	for(i=0; i<numOfFiles; i++)
	{
		if(ids[i] != -1)
		{
			pthread_join(ids[i], NULL);
		}
	}

	return 0;
}