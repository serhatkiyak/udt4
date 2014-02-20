#ifndef WIN32
   #include <cstdlib>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <fstream>
#include <iostream>
#include <cstring>
#include <udt.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sstream>

using namespace std;

#ifndef WIN32
void* sendfile(void*);
#else
DWORD WINAPI sendfile(LPVOID);
#endif


static int MAX_NUMBER_OF_CLIENTS = 5;
static int MAX_PARALLEL_FILES = 5;
static int BUFFER_SIZE = 1024;

static void *client_communicator(void *arg);
static void *file_uploader(void *arg);
int main_send(char* server_port);
void serve(int listenerPortNumber);
int main(int argc, char* argv[]);

struct client_communicator_parameter
{
	int clientListenerPortNum;
};

struct file_uploader_parameter
{
	int fileUploaderPortNum;
};

static void *client_communicator(void *arg)
{
	struct client_communicator_parameter *p = (struct client_communicator_parameter *) arg;
	int port = p->clientListenerPortNum;

	int list_sock;
	int conn_sock;
	struct sockaddr_in sa, ca;
	socklen_t ca_len;
	char ipaddrstr[64];
	int fileNumber = 1;
	int fileCounter = MAX_PARALLEL_FILES;

	int ids[MAX_PARALLEL_FILES];
	int i;
	for(i=0; i<MAX_PARALLEL_FILES; i++)
	{
		ids[i] = -1;
	}
	int cur_id = 0;

	list_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(list_sock < 0)
	{
		printf ("Errno: %s & Error: could not open socket\n",strerror(errno));
		exit(4);
	}

	bzero (&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if(bind (list_sock, (struct sockaddr *) &sa, sizeof(sa)) < 0)
	{
		printf ("Errno: %s & Error: could not bind in mainFunction\n",strerror(errno));
		exit(5);
	}

	listen (list_sock, 1);
	bzero (&ca, sizeof(ca));
	ca_len = sizeof(ca);
	conn_sock = accept (list_sock, (struct sockaddr *) &ca, &ca_len);
	inet_ntop(AF_INET, &(ca.sin_addr), ipaddrstr, 64);
	ntohs(ca.sin_port);

	int nextPortNum;
	char receive_buf[BUFFER_SIZE];
	int size = read(conn_sock, receive_buf, BUFFER_SIZE);
	if(size == -1)
	{
		cout<<"Something is wrong with reading!"<<endl;
	}

	while(!(receive_buf[0]=='E' && receive_buf[1]=='X' && receive_buf[2]=='I' && receive_buf[3]=='T') && fileCounter > 0)
	{
		fileCounter--;
		nextPortNum = port + fileNumber;
		char portString[BUFFER_SIZE];
		sprintf(portString, "%d", nextPortNum);
		int test = write(conn_sock, portString, BUFFER_SIZE);
		if(test == -1)
		{
			cout<<"Something is wrong with writing!"<<endl;
		}

		pthread_t tid;
		struct file_uploader_parameter *par = (struct file_uploader_parameter*) malloc(sizeof(struct file_uploader_parameter)); 
		
		par->fileUploaderPortNum = nextPortNum;

		int ret = pthread_create(&(tid), NULL, &file_uploader, (void *) par);

		if (ret != 0) 
		{
			printf("ERROR: thread create failed \n");
			exit(1);
		}

		ids[cur_id++] = tid;

		fileNumber++;
		int size = read(conn_sock, receive_buf, BUFFER_SIZE);
		if(size == -1)
		{
			cout<<"Something is wrong with reading!"<<endl;
		}
	}

	for(i=0; i<MAX_PARALLEL_FILES; i++)
	{
		if(ids[i] != -1)
		{
			pthread_join(ids[i], NULL);
		}
	}

	cout<<"Done serving to the current client!"<<endl;
	close(conn_sock);
	free(p);
}

static void *file_uploader(void *arg)
{
	struct file_uploader_parameter *p = (struct file_uploader_parameter *) arg;
	int port = p->fileUploaderPortNum;

	stringstream strs;
        strs << port;
  	string temp_str = strs.str();
  	char* portString = (char*) temp_str.c_str();
	main_send(portString);
	free(p);
}

int main_send(char* server_port)
{
   // use this function to initialize the UDT library
   UDT::startup();

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   string service = server_port;

   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return 0;
   }

   UDTSOCKET serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

   // Windows UDP issue
   // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
#ifdef WIN32
   int mss = 1052;
   UDT::setsockopt(serv, 0, UDT_MSS, &mss, sizeof(int));
#endif

   if (UDT::ERROR == UDT::bind(serv, res->ai_addr, res->ai_addrlen))
   {
      cout << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   freeaddrinfo(res);

   cout << "server is ready at port: " << service << endl;

   UDT::listen(serv, 10);

   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);

   UDTSOCKET fhandle;

   while (true)
   {
      if (UDT::INVALID_SOCK == (fhandle = UDT::accept(serv, (sockaddr*)&clientaddr, &addrlen)))
      {
         cout << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
         return 0;
      }

      char clienthost[NI_MAXHOST];
      char clientservice[NI_MAXSERV];
      getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
      cout << "new connection: " << clienthost << ":" << clientservice << endl;

      #ifndef WIN32
         pthread_t filethread;
         pthread_create(&filethread, NULL, sendfile, new UDTSOCKET(fhandle));
         pthread_detach(filethread);
      #else
         CreateThread(NULL, 0, sendfile, new UDTSOCKET(fhandle), 0, NULL);
      #endif
   }

   UDT::close(serv);

   // use this function to release the UDT library
   UDT::cleanup();

   return 0;
}

void serve(int listenerPortNumber)
{
	int list_sock;
	int conn_sock;
	struct sockaddr_in sa, ca;
	socklen_t ca_len;
	char ipaddrstr[64];

	list_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(list_sock < 0)
	{
		printf ("Errno: %s & Error: could not open socket\n",strerror(errno));
		exit(4);
	}

	bzero (&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(listenerPortNumber);
	if(bind (list_sock, (struct sockaddr *) &sa, sizeof(sa)) < 0)
	{
		printf ("Errno: %s & Error: could not bind in mainFunction\n",strerror(errno));
		exit(5);
	}

	listen (list_sock, MAX_NUMBER_OF_CLIENTS);

	int curPortNum = listenerPortNumber + (MAX_PARALLEL_FILES + 1);
	while(1)
	{
		bzero (&ca, sizeof(ca));
		ca_len = sizeof(ca);
		conn_sock = accept (list_sock, (struct sockaddr *) &ca, &ca_len);
		inet_ntop(AF_INET, &(ca.sin_addr), ipaddrstr, 64);
		ntohs(ca.sin_port);

		char portString[BUFFER_SIZE];
		sprintf(portString, "%d", curPortNum);
		int test = write(conn_sock, portString, BUFFER_SIZE);
		if(test == -1)
		{
			cout<<"Something is wrong with writing!"<<endl;
		}
		
		pthread_t tid;
		struct client_communicator_parameter *par = (struct client_communicator_parameter*) malloc(sizeof(struct client_communicator_parameter)); 
		
		par->clientListenerPortNum = curPortNum;

		int ret = pthread_create(&(tid), NULL, &client_communicator, (void *) par);

		if (ret != 0) 
		{
			printf("ERROR: thread create failed \n");
			exit(1);
		}

		curPortNum += (MAX_PARALLEL_FILES + 1);
	}
}

int main(int argc, char* argv[])
{
   //usage: sendfile [server_port]
   if ((2 < argc) || ((2 == argc) && (0 == atoi(argv[1]))))
   {
      cout << "usage: sendfile [server_port]" << endl;
      return 0;
   }
   char* server_port = argv[1];
   serve(atoi(server_port));
   return 0;
}

#ifndef WIN32
void* sendfile(void* usocket)
#else
DWORD WINAPI sendfile(LPVOID usocket)
#endif
{
   UDTSOCKET fhandle = *(UDTSOCKET*)usocket;
   delete (UDTSOCKET*)usocket;

   // aquiring file name information from client
   char file[1024];
   int len;

   if (UDT::ERROR == UDT::recv(fhandle, (char*)&len, sizeof(int), 0))
   {
      cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   if (UDT::ERROR == UDT::recv(fhandle, file, len, 0))
   {
      cout << "recv: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }
   file[len] = '\0';

   // open the file
   fstream ifs(file, ios::in | ios::binary);

   ifs.seekg(0, ios::end);
   int64_t size = ifs.tellg();
   ifs.seekg(0, ios::beg);

   // send file size information
   if (UDT::ERROR == UDT::send(fhandle, (char*)&size, sizeof(int64_t), 0))
   {
      cout << "send: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   UDT::TRACEINFO trace;
   UDT::perfmon(fhandle, &trace);

   // send the file
   int64_t offset = 0;
   if (UDT::ERROR == UDT::sendfile(fhandle, ifs, offset, size))
   {
      cout << "sendfile: " << UDT::getlasterror().getErrorMessage() << endl;
      return 0;
   }

   UDT::perfmon(fhandle, &trace);
   cout << "speed = " << trace.mbpsSendRate << "Mbits/sec" << endl;

   UDT::close(fhandle);

   ifs.close();

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}