#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <signal.h>
#include <thread>         // std::this_thread::sleep_for
#include <chrono> 
#include <algorithm>    // std::max
#include <fstream>
#include <deque>          // std::deque
#include <list>           // std::list
#include "queue.h"
#include <signal.h>
#include <dirent.h>
#include <time.h>

#define price_per_widget 10
#define TOTAL_NUM_WIDGETS 1000
#define TOTAL_AMT_MONEY 10000
#define EMPTY "EMPTY"
#define FILE_NAME "snapshot_"
#define GLOBAL_STATES "globalState_"

#define PORT00 3490
#define BACKLOG 10	
#define MAXDATASIZE 256 // max number of bytes we can get at once 

using namespace std;

typedef struct mp1_process_t_
{
	pid_t pid;
	bool snapshot_initiator;
	queue_t* event_queue;
	queue_t* snapshot_queue;
	long money;
	long widgets;
	unsigned int id;
	unsigned int ltime;
	unsigned int marker_received;

	vector< unsigned int > * vtime ;
	vector<struct receiver_t * > * myRecvWorkers; 

	int num_processes;
	bool state_recorded;
	bool isTakingSnapshot;
	int snapshot_th;
	char* filename;
	pthread_mutex_t process_lock;
	pthread_cond_t data_ready;
}mp1_process;

typedef struct channel_state_t
{
	long money;
	long widgets;
}channel_state;

typedef struct receiver_t
{
	mp1_process* parent;
	bool open_recording;
	unsigned int parentID;
	channel_state* cState;
	pthread_t t_id;
	unsigned int from_process;
	char* listening_on_port;
	char message_received[MAXDATASIZE];
}receiver;


vector<vector<string> > channels;

int send_message(  int sender, unsigned int receiver, const char* msg);


void cast_marker(mp1_process* p)
{
	char* mmsg;
	char* vtimeStr;
	for(int i=0; i<p->num_processes; i++ )
	{
		mmsg=NULL; vtimeStr=NULL;
		if(i!=p->id)
		{
			p->ltime++;
			(*p->vtime)[p->id]++;

			for(int i=0; i<p->vtime->size(); i++ )
			{
				if(vtimeStr)
					asprintf(&vtimeStr, "%s,%d", vtimeStr ,(*p->vtime)[i]);
				else
					asprintf(&vtimeStr, "%d" , (*p->vtime)[i]);	
			}
			asprintf(&mmsg, "MARKER: seq-%d from-%u logical-%u vector-%s", p->snapshot_th+1, p->id, p->ltime, vtimeStr );
			send_message(p->id, i, mmsg );
			printf("Process#%d sent marker-%d to process#%d during %d-th snapshot \n", p->id, p->snapshot_th+1 ,i, p->snapshot_th+1 );
			if(mmsg) free(mmsg);
			if(vtimeStr) free(vtimeStr);
		}
	}
}


unsigned int update_logic_timeStamp(unsigned int curr, unsigned int received)
{
	// printf("the origin logical time stamp is %d , and the received is %d\n", curr, received);
	if(received>curr)
		return received+1;
	else
		return curr+1;
}

void update_vector_timeStamp(vector<unsigned int >* curr, const vector<unsigned int >& received, int id )
{
	(*curr)[id]++;
	for(int i=0; i<curr->size(); i++)
	{
		if(i!=id)
		{
			(*curr)[i]=max((*curr)[i],received[i]);
		}
	}
}

void record_PState_to_file(const mp1_process* p)
{
	char* state=NULL;
	char* vState=NULL;
	FILE* pFile;
	for(int i=0; i<p->vtime->size(); i++ )
	{
		if(vState)
			asprintf(&vState, "%s %d", vState ,(*p->vtime)[i]);
		else
			asprintf(&vState, " %d" , (*p->vtime)[i]);	
	}
	asprintf(&state,"id %d : snapshot %d : logical %d : vector%s : money %ld : widgets %ld\n",
			 p->id, p->snapshot_th+1, p->ltime, vState, p->money, p->widgets );

	pFile=fopen(p->filename, "a+");
	fputs(state, pFile);
	fclose(pFile);

	if(state) free(state);
	if(vState) free(vState);
	return;
}


void record_CState_to_file(mp1_process* p, unsigned int from, unsigned int logical,  char* vtime, bool empty )
{	
	char* state=NULL;
	long m = (*p->myRecvWorkers)[from]->cState->money ;
	long w = (*p->myRecvWorkers)[from]->cState->widgets ;
	FILE* pFile=fopen(p->filename, "a+");

	if(empty || (m==0 && w==0) )
	{
		asprintf(&state, "id %u : snapshot %d : logical %u : vector %s : message %u to %u : %s\n",
				 p->id, p->snapshot_th+1, logical, vtime,  from  , p->id  , EMPTY );
	}
	else
	{
		asprintf(&state, "id %u : snapshot %d : logical %u : vector %s : message %u to %u : money %ld widgets %ld\n",
				 p->id, p->snapshot_th+1, logical, vtime, from, p->id, m, w);
	}
	fputs(state, pFile);
	fclose(pFile);
	if(state) 
		free(state);
}


void change_process_state(mp1_process* p, long money, long widgets)
{
	p->widgets+=widgets;
	p->money+=money;
}

void init_channels(  vector<vector<string> > & channels )
{
	// cout<< "channels has size "<<channels.size()<<" and size "<< channels[0].size()<<endl;
	int s=0;
	for(int i=0; i<channels.size(); i++)
		for(int j=0; j<channels[0].size(); j++)
		{
			channels[i][j]=to_string(PORT00+s);
			// cout<<"channels("<<i<<","<<j<<")="<< channels[i][j] <<endl;
			s++;
		}
}

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


void get_vector_t(const char* vec,  vector<unsigned int > & t )
{
	char* str=NULL;
	asprintf(&str, "%s", vec);
	char * pch;
  	pch = strtok (str,",");
  	while (pch != NULL)
  	{
  		unsigned int x = atoi(pch);
  		t.push_back(x);
    	pch = strtok (NULL, ",");
  	}  
}

void show_state(mp1_process* p)
{
	char* vState=NULL;
	for(int i=0; i<p->vtime->size(); i++ )
	{
		if(vState)
			asprintf(&vState, "%s %d", vState ,(*p->vtime)[i]);
		else
			asprintf(&vState, " %d" , (*p->vtime)[i]);	
	}
	printf("process#%d's current state: snapshot-%d logical-%d : vector%s money-%ld widgets-%ld\n",
			 p->id, p->snapshot_th , p->ltime, vState, p->money, p->widgets );

	if(vState) free(vState);
	return;
}


void taking_snapshot(mp1_process* p) //MARKER: seq-1 from-1 logical-1 vector-1,1,1,1 
{
	char* vtimeStr=NULL;

	record_PState_to_file(p);
	p->state_recorded=true;

	//open all the incoming channels!
	for(int i=0; i<p->num_processes; i++)
	{
		if((*p->myRecvWorkers)[i]->open_recording==false )
			(*p->myRecvWorkers)[i]->open_recording=true;
	}
	printf("Process%d finishes recording its state and opening its incoming channels during %d'th snapshot \n", 
			p->id, p->snapshot_th+1);
	
	//cast to all other processes!
	cast_marker(p);

	if(vtimeStr)
		free(vtimeStr);
}


void process_message(mp1_process* p, const char* msg )
{
	char* temp=NULL;
	char* vec=NULL;
	char* c;
	unsigned int l;
	unsigned int from;
	unsigned int marker_seq;
	vector< unsigned int > v;
	long m,w;
	asprintf(&temp, "%s", msg);
	if(p->id==0)

	//SEND_TO: p-0 money-100 widgets-10
	//UPDATE: logical-1 vector-1,1,1,1 money-100 widgets-100
	//MARKER: seq-1 from-1 logical-1 vector-1,1,1,1
	if(strstr(temp, "SNAPSHOT"))
	{
		char* pos=NULL;
		pos=strstr(temp, " ")+1;
		int s_th=atoi(pos);
		printf("%d'th snapshot request coming\n", s_th );
		pos=NULL;
		if(p->snapshot_th<s_th-1 || p->isTakingSnapshot )
		{
			//need to re-push snapshot onto the snapshot_queue !
			printf("The %d'th snapshot has not finished! New snapshot should wait \n", p->snapshot_th+1 );
			char* temp_=NULL;
			asprintf(&temp_, "%s", temp);
			queue_enqueue(p->snapshot_queue, temp_ );
			return;
		}
		p->ltime++;
		(*p->vtime)[p->id]++;
		p->isTakingSnapshot=true;

		printf("process# %d is initiating the %d-th snapshot.....\n", p->id, s_th );
		taking_snapshot(p);
	}

	if(strstr(temp, "SEND_TO") )
	{
		char* del;
		char* resMsg=NULL;
		unsigned int to;
		char* vtimeStr=NULL;

		c=strtok(temp, " ");
		while(c !=NULL)
		{
			if(strstr(c,"p"))
			{
				del=strstr(c,"-");
				// printf("sendto process: %s\n", del+1 );
				to=atoi(del+1);
			}
			else if(strstr(c, "money"))
			{
				del=strstr(c,"-");
				// printf("money: %s\n", del+1 );
				m=(long)(atoi(del+1));
			}
			else if(strstr(c, "widgets"))
			{
				del=strstr(c,"-");
				// printf("widgets: %s\n", del+1 );
				w=(long)(atoi(del+1));
			}
			else
			{
				// fprintf(stderr, "no match found!\n");
			}
			c=strtok(NULL, " ");
		}
		p->ltime++;
		(*p->vtime)[p->id]++;
	
		for(int i=0; i<p->vtime->size(); i++ )
		{
			if(vtimeStr)
				asprintf(&vtimeStr, "%s,%d", vtimeStr ,(*p->vtime)[i]);
			else
				asprintf(&vtimeStr, "%d" , (*p->vtime)[i]);	
		}
		asprintf(&resMsg, "UPDATE: from-%d logical-%u vector-%s money-%ld widgets-%ld", 
				 p->id ,p->ltime, vtimeStr, m, w );
		printf("process %d sends to process %d with the message: %s\n", p->id ,to ,resMsg);
		
		send_message(p->id, to, resMsg);
		change_process_state(p, (-1)*m, (-1)*w);
	}
	else if( strstr(temp, "UPDATE") ) // message formatted as "UPDATE: from-1 logical-1 vector-1,1,1,1 money-100 widgets-100"
	{
		char* del;
		c=strtok(temp, " ");
		char* channel_vtime=NULL;
		while(c !=NULL)
		{
			if(strstr(c,"from"))
			{
				del=strstr(c,"-");
				// printf("logical: %s\n", del+1 );
				from=atoi(del+1);	
			}
			else if(strstr(c,"logical"))
			{
				del=strstr(c,"-");
				// printf("logical: %s\n", del+1 );
				l=atoi(del+1);
			}
			else if(strstr(c, "vector"))
			{
				del=strstr(c,"-");
				// printf("vector: %s\n", del+1 );
				asprintf(&vec, "%s", del+1);
				asprintf(&channel_vtime, "%s", del+1);
			}
			else if(strstr(c, "money"))
			{
				del=strstr(c,"-");
				// printf("money: %s\n", del+1 );
				m=(long)(atoi(del+1));
			}
			else if(strstr(c, "widgets"))
			{
				del=strstr(c,"-");
				// printf("widgets: %s\n", del+1 );
				w=(long)(atoi(del+1));
			}
			else
			{
				// fprintf(stderr, "no match found!\n");
			}
			c=strtok(NULL, " ");
		}

		get_vector_t(vec, v);

		if((*p->myRecvWorkers)[from]->open_recording )
		{
			(*p->myRecvWorkers)[from]->cState->money+=m;
			(*p->myRecvWorkers)[from]->cState->widgets+=w;

		}
		printf("Process#%d delivered %d's message:  %s\n", p->id, from, msg  );

		p->ltime=update_logic_timeStamp(p->ltime, l );
		update_vector_timeStamp(p->vtime, v, p->id);
		change_process_state(p, m, w);

		if(channel_vtime)
			free(channel_vtime);
	}
	else if(strstr(temp, "MARKER")) //MARKER: seq-1 from-1 logical-1 vector-1,1,1,1 
	{
		char* del;
		char* marker_to_be_queued=NULL;
		asprintf(&marker_to_be_queued, "%s", temp);
		char* vtimeStr=NULL;

		c=strtok(temp, " ");
		while(c !=NULL)
		{
			if(strstr(c,"seq"))
			{
				del=strstr(c,"-");
				// printf("sendto process: %s\n", del+1 );
				marker_seq=atoi(del+1);
			}
			else if(strstr(c, "from"))
			{
				del=strstr(c,"-");
				// printf("money: %s\n", del+1 );
				from=atoi(del+1);
			}
			else if(strstr(c,"logical"))
			{
				del=strstr(c,"-");
				// printf("logical: %s\n", del+1 );
				l=atoi(del+1);
			}
			else if(strstr(c, "vector"))
			{
				del=strstr(c,"-");
				// printf("vector: %s\n", del+1 );
				asprintf(&vec, "%s", del+1);
			}
			c=strtok(NULL, " ");
		}
		get_vector_t(vec, v);

		printf("Process#%d receives marker-%d from process#%d!\n", p->id, marker_seq, from);

		if(marker_seq>p->snapshot_th+1)
		{
			printf("Process%d is not ready to deliver marker-%d yet!\n", p->id, marker_seq );
			queue_enqueue(p->event_queue, marker_to_be_queued);
		}

		p->ltime=update_logic_timeStamp(p->ltime, l );
		update_vector_timeStamp(p->vtime, v, p->id);
		
		p->marker_received++;
		if(p->state_recorded)
		{
			record_CState_to_file(p, from, l, vec, false);
		}
		else
		{
			p->isTakingSnapshot=true;
			taking_snapshot(p);
			record_CState_to_file(p, from, l, vec, true);
		}	
		if(p->marker_received==p->num_processes-1)
		{
			p->marker_received=0;
			p->state_recorded=false;

			// close all incoming channels:
			for(int i=0; i<p->num_processes; i++)
			{
				if((*p->myRecvWorkers)[i]->open_recording )
					(*p->myRecvWorkers)[i]->open_recording=false;
				(*p->myRecvWorkers)[i]->cState->money=0;
				(*p->myRecvWorkers)[i]->cState->widgets=0;
			}

			p->isTakingSnapshot=false;
			p->snapshot_th++;
			printf("Process %d has received all the marker-%d's, finishing snapshot %d's cycle\n",
					 p->id, marker_seq, p->snapshot_th);
			if(queue_size(p->snapshot_queue)>0)
			{
				queue_enqueue(p->event_queue, queue_dequeue(p->snapshot_queue));
			}
		}
	}
	if(temp)
		free(temp);
}

static void* receiver_cb(void* arg)
{
	receiver* worker=(receiver*)arg;

	char* port_num=worker->listening_on_port;

	int sockfd, new_fd, numbytes;  // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;
	unsigned int parent_id=worker->parentID;


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port_num, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return NULL;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return NULL;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}
	struct sockaddr_in* IPV4addr = (struct sockaddr_in * )(p->ai_addr) ;
	unsigned int des_port = IPV4addr->sin_port ;

	// printf("process#%u is waiting for connections from process#%u on port %d \n", parent_id, worker->from_process, des_port );

	while(1) {  // main accept() loop
		char* mesg_stored=NULL;
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) 
		{
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		// printf("process#%u: channel C-%u%u established, and process#%u is ready to receive message from process#%u !\n", 
		// 	   parent_id, worker->from_process, parent_id, parent_id, worker->from_process);

		if ((numbytes = recv(new_fd, worker->message_received, MAXDATASIZE-1, 0)) == -1) 
		{
   			perror("recv");
    			exit(1);
		}
		worker->message_received[numbytes] = '\0';
		asprintf(&mesg_stored, "%s", worker->message_received);

		bzero(worker->message_received, MAXDATASIZE);

		// if(parent_id==0)
		// 	printf(" via C%d%d: process#%d received msg: %s , ready to enter critical section \n",
		// 				 worker->from_process , parent_id ,parent_id ,mesg_stored);
		pthread_mutex_lock(&worker->parent->process_lock );
		// if(parent_id==0)
		// 	printf("process#%d with msg: %s is in critical section! Ready to enqueue the message \n", parent_id ,mesg_stored);

		queue_enqueue(worker->parent->event_queue, (void*)mesg_stored );
		pthread_cond_signal(&worker->parent->data_ready);
		pthread_mutex_unlock(&worker->parent->process_lock );

		mesg_stored=NULL;

		close(new_fd);  // parent doesn't need this
	}
	return NULL;
}

int send_message(  int sender, unsigned int receiver, const char* buf)
{
	char* msg=NULL;
	char* port;

	if(!buf)
	{
		fprintf(stderr, "message is invalid!\n" );
		return 1;
	}
	asprintf(&msg, "%s", buf);
	
	if(sender<0) //the sender is the main parent process
	{
		port=(char*)channels[receiver][receiver].c_str();
		sender=getpid();		
	}
	else  //the sender is the following child processes
	{
		port=(char*)channels[sender][receiver].c_str();
	}

	int sockfd, numbytes;  
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	char* error_msg=NULL;
	asprintf(&error_msg, "cannot establish channel from process %d to %d due to ", sender, receiver);

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo("localhost", port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,p->ai_protocol)) == -1)
		{
			fprintf(stderr, "%ssocket failure \n", error_msg);
			// perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
		{
			close(sockfd);
			// perror("client: connect");
			fprintf(stderr, "%sconnect failure \n", error_msg);
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "process %d failed to connect to process %d \n", sender, receiver);
		return 2;
	}

	// struct sockaddr_in* IPV4addr = (struct sockaddr_in * )(p->ai_addr) ;
	// unsigned int des_port = IPV4addr->sin_port ;

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);

	// printf("process#%d connecting to process#%d at %s on port %d \n", sender ,  receiver, s, des_port );

	freeaddrinfo(servinfo); // all done with this structure
	if (send(sockfd, msg, MAXDATASIZE, 0) == -1)
				perror("send");
	close(sockfd);
	return 0;
}


void init_state(mp1_process* p)
{
	p->money=TOTAL_AMT_MONEY/p->num_processes;
	p->widgets=TOTAL_NUM_WIDGETS/p->num_processes;
}

void clear_process(mp1_process* p )
{
	if(p->filename)
		free(p->filename);
	for(int i=0; i<p->myRecvWorkers->size(); i++)
	{
		if((*(p->myRecvWorkers))[i]->cState)
			free((*(p->myRecvWorkers))[i]->cState);
		free((*(p->myRecvWorkers))[i]);
	}
	if(p->event_queue)
	{
		queue_destroy(p->event_queue);
		free(p->event_queue);
	}
	delete p->myRecvWorkers;
	delete p->vtime ;
	pthread_mutex_destroy(&p->process_lock);
	pthread_cond_destroy(&p->data_ready);
}

void test_connection_channels(mp1_process* p)
{
    std::chrono::milliseconds dura( 2000 );
    std::this_thread::sleep_for( dura );
	for(int i=0; i<p->num_processes;i++)
	{
		if(i!=p->id)
		{
			char* msg=NULL;
			asprintf(&msg, "Hello, process#%d, I'm process#%d, nice to connect with you ! \n", i,p->id);
			send_message( p->id , i , msg);
		}
	}
	return;
}


void sender(unsigned int id, int num, pid_t pid)
{

	mp1_process* process_mem=(mp1_process*)malloc(sizeof(mp1_process));
	process_mem->event_queue=(queue_t*)malloc(sizeof(queue_t));
	process_mem->snapshot_queue=(queue_t*)malloc(sizeof(queue_t));
	queue_init(process_mem->snapshot_queue);
	queue_init(process_mem->event_queue);
	process_mem->id=id;
	process_mem->pid=pid;
	process_mem->ltime=0;
	process_mem->num_processes=num;
	process_mem->filename=NULL;
	process_mem->snapshot_th=0;
	process_mem->marker_received=0;
	process_mem->isTakingSnapshot=false;
	process_mem->state_recorded=false;
	process_mem->myRecvWorkers=new vector<struct receiver_t* > ;
	process_mem->vtime=new vector<unsigned int > ;
	asprintf(&process_mem->filename, "snapshot_%d.txt",id);
	for(int i=0; i<num; i++)
	{
		process_mem->vtime->push_back(0);
	}

	pthread_mutex_init(&process_mem->process_lock, NULL);
	pthread_cond_init(&process_mem->data_ready,NULL);

	if(id==0)
		process_mem->snapshot_initiator=true;
	else 
		process_mem->snapshot_initiator=false;
	init_state( process_mem);


	pthread_t* thread_id=(pthread_t*)malloc(sizeof(pthread_t)*num);

	int j=0;
	for(int otherid=0; otherid<num; otherid++ )
	{
		string port=channels[otherid][id];

		receiver* recv_worker=(receiver*)malloc(sizeof(receiver));
		recv_worker->parent=process_mem;
		recv_worker->open_recording=false;
		recv_worker->parentID = process_mem->id;
		recv_worker->from_process=(unsigned int)otherid;
		recv_worker->listening_on_port=(char*)port.c_str();
		recv_worker->cState=(channel_state*)malloc(sizeof(channel_state));
		recv_worker->cState->money=0;
		recv_worker->cState->widgets=0;
		process_mem->myRecvWorkers->push_back(recv_worker); 

		pthread_create(&thread_id[j++],NULL, receiver_cb ,(void*)recv_worker);
	}

	srand (time(NULL));
	while(1)
	{
		int offset=0;
		char* msg=NULL;

		pthread_mutex_lock(&process_mem->process_lock);
		while(queue_size(process_mem->event_queue)==0)
		{
			pthread_cond_wait(&process_mem->data_ready, &process_mem->process_lock );
		}
		msg=(char*)queue_dequeue(process_mem->event_queue);

		offset = 100+rand()%300 ;
		
		std::chrono::milliseconds dura( offset );
		std::this_thread::sleep_for( dura );
		process_message(process_mem ,msg);
		pthread_mutex_unlock(&process_mem->process_lock);

		// show_state(process_mem);
		if(msg) 
			free(msg);
	}

	//test connection!
	// test_connection_channels(process_mem );

	for(int i=0; i<num; i++)
    {
    		pthread_join(thread_id[i],NULL);
	}
	free(thread_id);
	clear_process(process_mem);
	free(process_mem);
}


bool isNum(char* m)
{
	for(int i = 0; i < (strlen(m) - 1); i++)
	{
		if(!isdigit(m[i]))
			return false;
	}
	return true;
}

void verify_condition(unsigned int num)
{
	char* statefile=NULL;
	char* line=NULL;
	long m=0;
	long w=0;
	size_t len=0;
	ssize_t read;
	asprintf(&statefile, "%s%d.txt", GLOBAL_STATES, num);
	FILE* pf=fopen(statefile, "r");
	if(!pf)
	{
		fprintf(stderr, "%s does not exist!\n", statefile);
		return;
	}
	while ((read = getline(&line, &len, pf)) != -1)
	{
		char* temp=NULL;
		char* pch=NULL;
		char* pre=NULL;
		asprintf(&temp, "%s", line);
		pch = strtok (temp," ");
  		while (pch != NULL)
  		{
  			if(strstr(pch, "widgets"))
  				asprintf(&pre, "%s", "widgets");
  			else if(strstr(pch, "money"))
  				asprintf(&pre, "%s", "money");
  			else
  			{
  				if(pre && strcmp(pre, "money")==0) 
  				{
  					m+=atoi(pch);
  					free(pre);
  					pre=NULL;
				}
  				else if(pre && strcmp(pre, "widgets")==0)
  				{
  					w+=atoi(pch);
  					free(pre);
  					pre=NULL;	
  				} 
  			}
    		pch = strtok (NULL, " ");
  		}
		if(temp)
			free(temp);
		if(pre) 
			free(pre);
	} 

  	printf("At global state#%d: money sums up to %ld, and widgets sum up to %ld \n", num, m, w );
	if(m==TOTAL_AMT_MONEY && w==TOTAL_NUM_WIDGETS )
		printf("snapshot %d forms a consistent cut! \n", num);
	else
		printf("snapshot %d forms an inconsistent cut! \n", num);
	if(statefile)
		free(statefile);
	fclose(pf);	
}

void search_snapshot(unsigned int num, unsigned int num_processes)
{
	char* filename=NULL;
	char* gStateFile=NULL;
	FILE* sFile=NULL;
	asprintf(&gStateFile, "%s%d.txt", GLOBAL_STATES, num);
	sFile=fopen(gStateFile, "a+");

	for(int i=0; i<num_processes; i++)
	{
		asprintf(&filename, "%s%d.txt", FILE_NAME, i);
		FILE* pFile;
		char* line=NULL;
		size_t len=0;
		ssize_t read;
		pFile=fopen(filename, "r");
		if(pFile==NULL)
		{
			 fprintf(stderr, "Error occurs when reading %s ! \n", filename);
      		 exit(EXIT_FAILURE);
		}
		while ((read = getline(&line, &len, pFile)) != -1) 
		{
	       	// printf("%s", line);
	       	char* temp=NULL;
	       	char* pch;
	       	asprintf(&temp, "%s", line);
	       	if(strstr(line, "snapshot") ==NULL ) 
	       	{
				continue;	       		
	       	}
  			pch = strtok (temp,":");
  			while (pch != NULL)
  			{
  				char* x;
  				char* y;
  				if(strstr(pch, "snapshot") )
  				{
  					x=pch+(strlen(pch)-1);
  					y=pch+10;
  					(*x)=0;
  					if(atoi(y)==num)
  					{
  						printf("%s\n",line);
						fputs(line, sFile);
  					}
  					break;
  				}
    			pch = strtok (NULL, ":");
  			}
  			if(temp)
  				free(temp);
   		}
  		if(line)
   			free(line);
   		fclose(pFile);
	}
	fclose(sFile);
}

void parse_search_check(const char* cmd, int num_processes)
{
	char* temp=NULL;
	int num;
	asprintf(&temp, "%s", cmd);
	char * pch;
  	pch = strtok (temp," ");
  	while (pch != NULL)
  	{
  		if(isNum(pch))
  		{
  			num=atoi(pch);
  			break;
  		}
    	pch = strtok (NULL, " ");
  	}
  	if(strstr(cmd, "search"))
  	{
	  	printf("We are looking for %d th snapshot of all processes\n", num);
	  	printf("Please note that the total amount of money is %d and total number of widgets is %d\n", 
	  				TOTAL_AMT_MONEY, TOTAL_NUM_WIDGETS );
	  	search_snapshot(num, num_processes);	
  	}
  	else
  	{	
  		printf("Checking if snapshot#%d forms a consistent cut...\n", num);
	  	verify_condition(num);
  	}
  	if(temp)
  		free(temp);
}

char* parse_main_cmds(const char* cmd, int n, int* snapshot_th)
{
	if(!cmd) return NULL;
	char* resMsg=NULL;
	char* temp=NULL;
	unsigned int from;
	unsigned int to;
	long m,w;
	char* c =NULL;
	int numTokens=0;

	asprintf(&temp, "%s", cmd);
	if(strcmp(temp,"s")==0 || strcmp(temp,"snapshot")==0 )
	{
		(*snapshot_th)++;
		asprintf(&resMsg, "SNAPSHOT: %d", *snapshot_th);
		goto stop;
	}
	if(strcmp(temp,"run")==0  )
	{
		asprintf(&resMsg, "%s", "run");
		goto stop;
	}

	c=strtok(temp, " ");
	while(c !=NULL)
	{
		if(!isNum(c))
			goto stop;

		numTokens++;
		if(numTokens==1)
		{
			from=atoi(c);
		}	
		else if(numTokens==2)
		{
			to=atoi(c);	
			if(to>=n || to<0 || from>=n || from<0)
			{
				fprintf(stderr, "process does not exist\n" );
				goto stop;
			}
			else if(to==from)
			{
				fprintf(stderr, "the two processes are not allowed to be the same! \n" );
				goto stop;
			}
		}
		else if(numTokens==3)
		{
			m=atoi(c);	
		}
		else 
		{
			w=atoi(c);	
		}

		c=strtok(NULL, " ");
	}
	if(numTokens!=4)
		goto stop;

	asprintf(&resMsg, "%u SEND_TO: p-%u money-%ld widgets-%ld", from ,to, m, w);

stop:
	if(temp) free(temp);
	return resMsg;
}

bool target_file_exist(const char* dir, const char* filename)
{
    DIR *dp;
    struct dirent *entry;
    struct stat statbuf;
    if((dp = opendir(dir)) == NULL) {
        fprintf(stderr,"cannot open directory: %s\n", dir);
        return false;
    }
    chdir(dir);
    while((entry = readdir(dp)) != NULL) 
    {
         if(strstr(entry->d_name, filename) )
         		return true;
    }
    closedir(dp);
    return false;
}

int get_sender_id(const char* mesg, int num_processes)
{     
	char* tmp=NULL;
	asprintf(&tmp, "%s", mesg);
	char* c=tmp;
	char* space = strchr(tmp,  ' ');
	unsigned int from;
	if(space && !strstr(tmp, "SNAPSHOT"))
	{
		*space=0;
		tmp=space+1;
		from=atoi(c);
		if(from>=num_processes)
		{
			fprintf(stderr, "process does not exist \n");
			return 1;
		}

		send_message(-1, from, tmp);
	}
	else
	{
		//tell process 0 to initiate snapshot!
		send_message(-1, 0, tmp);
	}
	if(tmp)tmp=NULL;
	if(c)
		free(c);
		
	return 0;
}

void run_cmds(queue_t* q, int num_processes)
{
	while(queue_size(q)>0)
	{
	
		char* command=(char*)queue_dequeue(q);
		// printf("%s\n", command );
		if(command)
			get_sender_id(command, num_processes );
		if(command)
			free(command);
	}
}
int main(int argc, char** argv)
{

	pid_t child;
	pid_t pid;
	int num_processes;
	int snapshot_th=0;
	int id=0;
	int n;
	char cwd[1024];
	queue_t* msg_queue;

	if(argc!=2 && argc!=3)
	{
		fprintf(stderr, "usage: %s <num process> OR %s <num process> f\n", argv[0], argv[0] );
		exit(1);
	}

	msg_queue=(queue_t*)malloc(sizeof(queue_t));
	queue_init(msg_queue);

	if (getcwd(cwd, sizeof(cwd)) != NULL)
    		fprintf(stdout, "Current working dir: %s\n", cwd);
  	else
  	{
    		perror("getcwd_error");
    		return -1;
  	}
	num_processes=atoi(argv[1]);

	if( target_file_exist(cwd, FILE_NAME) )
		system("make clear1");
	if( target_file_exist(cwd, GLOBAL_STATES))
		system("make clear2");

	for(int i=0; i<num_processes; i++)
	{
		vector<string> str (num_processes,"");
		channels.push_back(str);
	}
	init_channels(channels);

	for(n=0; n<num_processes; n++)
	{
		if( (child = fork())<=0 )
			break;
	}
	// fprintf(stderr, "i: %d process ID: %d parentID: %d child ID: %d\n", n, getpid(), getppid(), child );

	if(child<=0)
	{
		sender(n, num_processes, getpid());
	}
	else
	{
		char* buffer;
          char* mesg;
		size_t len=0;
		while(1)
		{
     	 	buffer=NULL;
     	 	mesg=NULL;
          	getline(&buffer, &len, stdin); 
          	char* p=buffer;
          	while(*p)
          	{
               	if(*p=='\n') 
               	*p=0;
               	p++;
         		}
         		if(strstr(buffer, "search") || strstr(buffer, "check") )
         		{
         			parse_search_check(buffer, num_processes);
         			if(buffer)
         				free(buffer);
         			continue;
         		}
		    	mesg=parse_main_cmds(buffer, num_processes, &snapshot_th);
		    	// printf("%s\n", mesg );
		    	if(!mesg)
		    	{
		    		printf("usage: <from_process> <to_process> <money> <widgets> OR...\n");
		    		printf("usage: snapshot or s \n");
		    	}
		    	else
		    	{
		    		if(argc==3 && strcmp(argv[2],"f") == 0)
		    		{
		    			if(strcmp(mesg,"run")==0 && queue_size(msg_queue)>0)
		    			{
		    				run_cmds(msg_queue, num_processes);
		    			}
		    			else
		    			{
		    				char* qMsg=NULL;
		    				asprintf(&qMsg, "%s", mesg);
		    				queue_enqueue(msg_queue, (void*)qMsg);
		    			}
		    			// printf("%s\n", argv[2]);
		    		}
		    		else
		    		{
	         			if(get_sender_id(mesg, num_processes)!=0)
	         			{
	         				printf("invalid command\n");
	         				continue;	         				
	         			}
		    		}
		    		if(mesg)
		    			free(mesg);       	
		    	}
         	     if(buffer) free(buffer);
         	}
	}

   	while (pid=waitpid(-1, NULL, 0)) {
   		if (errno == ECHILD) 
   		{
      		break;
   		}	
   		// printf("process %d is waiting for one if its children \n", getpid() );
	}
    cout<<"end of page reached....no " <<endl;
	return 0;

}