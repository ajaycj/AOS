#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define MAX_LINE_LEN 100
#define MAX_HOST_NAME_LEN 100
#define CONN_QUEUE 45
#define LEN 1024
#define INFO_LEN 256

#define ACK_POS 1
#define ACK_NEG 0

enum Relation {
    UNKNOWN,
    PARENT,
    CHILD
};

enum Message_type {
    CONTROL,
    DATA,
    DATA_ACK,
    LEARN_COMPLETE,
    DATA_INIT
};

struct node {
    int node_num;
    int port;
    char host[MAX_HOST_NAME_LEN];
    enum Relation relation;
    int node_idx;
    int learn_completed;
    int ack_received;
};

struct m_message {
    enum Message_type type;
    int from_node_num;
    char data[LEN - sizeof(int) - sizeof(enum Message_type)];
};

struct node adj_nodes[20] = { {0, 0, {0}, UNKNOWN, -1, 0, 0} };
struct node curr_node;
int num_adj_nodes;
int isRoot = 0;
volatile int parent_known = 0;
int n_learn_complete = 0;
int this_learn_complete = 0;
int data_source_num = -1;
int all_acks_recvd_glbl = 0;
pthread_mutex_t lock;
char data_messages[LEN] = {0};
char ack_messages[LEN] = {0};
char child_messages[LEN/2] = {0};

void get_adj_nodes(int node_num);
void build_config(char **lines);
void start_tcp_server();
void *handle_client(void *arg);
void handle_control_message(int node_num, int cli_sockfd);
void send_message(int idx, struct m_message msg, int retry);
void broadcast_control_message();
void broadcast_data_message(struct m_message msg);
void process_server_reply(char *reply, int adj_idx);
void *wait_for_learn_completion(void *arg);
void write_to_file();

int main(int argc, char *argv[])
{
    //local variables

    // minimum number of arguments required is 2
    if (argc != 2)    
        exit(EXIT_FAILURE);
    curr_node.node_num = atoi(argv[1]);
    get_adj_nodes(curr_node.node_num);
    
    if (pthread_mutex_init(&lock, NULL) != 0)
    {
        perror("mutex init failed:");
        return 1;
    }
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, wait_for_learn_completion, NULL);
    
    start_tcp_server();
    
    printf("Shutting down...\n");
    pthread_mutex_destroy(&lock);
    return 0;
}

// read config and gather adjacent node info
void get_adj_nodes(int node_num)
{
    int line_num = 0;
    char line[MAX_LINE_LEN], *temp_line_ptr, **lines = NULL;
    FILE *fp;
    
    fp = fopen("config3.txt", "r");
    if (!fp)
        exit(EXIT_FAILURE);

    fgets(line, MAX_LINE_LEN, fp);
    if (curr_node.node_num == atoi(line))
        isRoot = 1;
    
    while (1) 
    {
        if (!fgets(line, MAX_LINE_LEN, fp))
            break;
        temp_line_ptr = line;
        while (*temp_line_ptr != '\n' && *temp_line_ptr != '\0') temp_line_ptr++;
        *temp_line_ptr = '\0';
        if (!strlen(line))
            break;
        lines = (char **) realloc(lines, sizeof(char *) * (line_num + 1));
        *(lines + line_num) = (char *) malloc(strlen(line));
        strcpy(*(lines + line_num),line);
        line_num++;
    }
    build_config(lines);
    //free lines
        
    int i = 0;
    for (; i < line_num; ++i) {    
        //TODO    
        //free(*(lines + i));
    }
    free(lines);
    close(fp);
}

// populate the adj_nodes
void build_config(char **lines)
{
    char *word, line[MAX_LINE_LEN];
    int adj_node_num[100] = { 0 };
    int i, k;

    strcpy (line, lines[curr_node.node_num - 1]);
    word = strtok(line," ");

    i = 0;
    while (word) {
        if  (i == 1)
            strcpy(curr_node.host, word);
        else if (i == 2)
            curr_node.port = atoi(word);
        if (i > 2) {
            adj_nodes[i - 3].node_idx = i -3;
            adj_nodes[i - 3].node_num = atoi(word);
        }
	    word = strtok(NULL, " ");
        i++;
    }
    
    num_adj_nodes = i - 3;
    i = 0;
    for (; i < num_adj_nodes; ++i) {
        k = 0;
        while (lines[k]) {
            bzero(line, MAX_LINE_LEN);
            strcpy (line, lines[k]);
            word = strtok(line, " ");
            if (atoi(word) == adj_nodes[i].node_num) {
                word = strtok(NULL, " ");
                strcpy(adj_nodes[i].host, word);
                word = strtok(NULL, " ");
                adj_nodes[i].port = atoi(word);
                break;
            }
            k++;
        }
        
    }
}

void start_tcp_server(void)
{
    int listen_sockfd, cli_sockfd, cli_len;
    struct sockaddr_in servaddr;
    struct sockaddr_in dest;
    struct hostent *he;
    char hostname[MAX_HOST_NAME_LEN];
    socklen_t size;

    gethostname(hostname, MAX_HOST_NAME_LEN);
    he = gethostbyname(hostname);
    
    if (he == NULL)
        exit(EXIT_FAILURE);
    
    listen_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sockfd < 0)
        exit(EXIT_FAILURE);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(curr_node.port);
    servaddr.sin_addr.s_addr= INADDR_ANY;
    //binding
    if((bind(listen_sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) < 0)
    {
        perror("Unable to bind socket: ");
        exit(0);
    } 
    if(listen(listen_sockfd, CONN_QUEUE) == -1)
    {
        perror(" Listen Failure: ");
        exit(0);       
    }
    
    listen(listen_sockfd, 45);
    cli_len= sizeof(dest);
    
    if (isRoot)
    {
        //send message to neighbours and print ack from them
        broadcast_control_message();        
    }
    pthread_t thread_id;
    while (1) 
    {
        cli_sockfd= accept(listen_sockfd, (struct sockaddr *)&dest, &cli_len);
        if(cli_sockfd < 0)
        {
           perror("No Accept: ");  
           continue;  
        }
        if(pthread_create(&thread_id , NULL ,  handle_client , (void*) &cli_sockfd) < 0)
        {
            perror("could not create thread");            
        }        
    }
    //close(cli_sockfd);
    //close(listen_sockfd);     
}

void *handle_client(void *arg)
{
    int bytes_read, cli_sockfd = * (int *)arg;
    char buff[LEN];
    bytes_read = recv(cli_sockfd, buff, LEN, 0);
    if(bytes_read < 0) 
     {
        perror("Error in socket reading: ");
        bzero(buff,LEN);
        sprintf(buff, "%d", ACK_NEG);
        send(cli_sockfd, buff, LEN, 0);
        close(cli_sockfd);
        return NULL;
     }
    struct m_message msg;
    memcpy(&msg, buff, LEN);
    
    if(msg.from_node_num == 0){    
        bzero(buff,LEN);
        sprintf(buff, "%d", ACK_NEG);
        send(cli_sockfd, buff, LEN, 0);
        close(cli_sockfd);
        return NULL;   
    }
        
    
    if (msg.type == CONTROL) {
        handle_control_message(msg.from_node_num, cli_sockfd);
    }
    else if (msg.type == DATA) {  
        //handle data messages
        data_source_num = msg.from_node_num;
        bzero(buff,LEN);
        sprintf(buff, "%d", ACK_NEG);
        send(cli_sockfd, buff, LEN, 0);
        
        printf("Data message received from node %d: %s\n", 
            msg.from_node_num, msg.data);
        char info[INFO_LEN] = {0};
        sprintf(info, "Data message received from node %d: %s\n", 
            msg.from_node_num, msg.data);
        strcat(data_messages, info);
            
        broadcast_data_message(msg);
    }
    else if (msg.type == LEARN_COMPLETE) {
        bzero(buff,LEN);
        sprintf(buff, "%d", ACK_NEG);
        send(cli_sockfd, buff, LEN, 0);
        int i = 0;
        for (; i < num_adj_nodes; i++) {
            if (adj_nodes[i].node_num == msg.from_node_num) {                      
                adj_nodes[i].learn_completed = 1;
                break;
            }
        }
    }
    else if (msg.type == DATA_ACK){
        bzero(buff,LEN);
        sprintf(buff, "%d", ACK_NEG);
        send(cli_sockfd, buff, LEN, 0);
        
        printf("Data ACK received from node %d.\n", msg.from_node_num);
        char info[INFO_LEN] = {0};
        sprintf(info, "Data ACK received from node %d.\n", msg.from_node_num);
        strcat(ack_messages, info);      
        
        //forward acks
        int i, all_ack_recvd = 1, node_idx = -1;
        for (i = 0; i < num_adj_nodes; i++) {
            if (adj_nodes[i].relation == UNKNOWN)
                continue;
            if (adj_nodes[i].node_num == data_source_num) {
                node_idx = adj_nodes[i].node_idx;
                continue;
            }
            if (adj_nodes[i].node_num == msg.from_node_num)                       
                adj_nodes[i].ack_received = 1;
            if (!adj_nodes[i].ack_received)
                all_ack_recvd = 0;
        }
        
        if (all_ack_recvd) {
            all_acks_recvd_glbl = 1;
            msg.from_node_num = curr_node.node_num;          
            //clear ack_recvds
            for (i = 0; i < num_adj_nodes; i++) {
                if (adj_nodes[i].relation == UNKNOWN)
                    continue;
                adj_nodes[i].ack_received = 0;
            }
            if (node_idx != -1) {                
                send_message(node_idx, msg, 1);
            }            
            write_to_file();
            bzero(child_messages, LEN/2);
            bzero(data_messages, LEN);
            bzero(ack_messages, LEN);
        }
    }
    else if (msg.type == DATA_INIT) {
        if (n_learn_complete && this_learn_complete) {
            //initiate data broadcast
            msg.type = DATA;
            msg.from_node_num = -1;
            all_acks_recvd_glbl = 0;
            broadcast_data_message(msg);
            int i;
            
            while(!all_acks_recvd_glbl) 
                sleep(2);            
            
            bzero(buff,LEN);
            sprintf(buff, "Message successfully broadcasted across all nodes!\n");
            send(cli_sockfd, buff, LEN, 0);        
        }
        else {
            bzero(buff,LEN);
            sprintf(buff, "Nodes not ready! Try again after some time.\n");
            send(cli_sockfd, buff, LEN, 0);
        }
    }
    //close(cli_sockfd);             
}

void handle_control_message(int node_num, int cli_sockfd)
{
    char buff[LEN] = { 0 };
    int bytes_written;
    pthread_mutex_lock(&lock);

    if(!parent_known)
    {
        parent_known = 1;
        pthread_mutex_unlock(&lock);
        
        sprintf(buff, "%d", ACK_POS);
        bytes_written = send(cli_sockfd, buff, LEN, 0);
        int i = 0;
        for (; i < num_adj_nodes; i++) {
            if (adj_nodes[i].node_num == node_num) {                      
                adj_nodes[i].relation = PARENT;
                break;
            }
        }
        broadcast_control_message();    
    }
    else 
    {
        pthread_mutex_unlock(&lock);
        sprintf(buff, "%d", ACK_NEG);
        bytes_written = send(cli_sockfd, buff, LEN, 0);
    }
    if (bytes_written < 0) 
        perror("Error in socket writing: ");
}

void send_message(int adj_idx, struct m_message msg, int retry)
{   
    int sockfd, n;
    struct sockaddr_in servaddr;
    struct hostent *he;
    char hostname[MAX_HOST_NAME_LEN], buff[LEN];
    sockfd = socket(AF_INET,SOCK_STREAM, 0);
    if(sockfd < 0)
     {
        perror("Error in opening Socket: ");
        return; 
     }
    //gethostname(hostname, MAX_HOST_NAME_LEN);       
    he = gethostbyname(adj_nodes[adj_idx].host); 
    
    if(he == NULL)
     {
        return;
     } 
    
    struct timeval tv;

    tv.tv_sec = 2;  /*2 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors

    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    bcopy((char *)he->h_addr, (char *)&servaddr.sin_addr.s_addr, he->h_length);
    
    servaddr.sin_port = htons(adj_nodes[adj_idx].port); 
    if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {   
        perror("Error in connection: ");
        close(sockfd);
        return;
    }
           
    bzero(buff, LEN);
    memcpy(buff, &msg, LEN);
    n = write(sockfd, buff, LEN);
    if(n < 0)
    {
        perror("Error in writing: ");
        close(sockfd);
        
        return;
    }
    bzero(buff, LEN);
    n = read(sockfd, buff, LEN);
    if(n < 0)
    {
        perror("Error in reading: ");
        close(sockfd);
        if (retry)
            send_message(adj_idx, msg, --retry);
        return;
    }
    if (*msg.data) {
        printf("Sent successfully to node %d: %s\n", 
            adj_nodes[adj_idx].node_num, msg.data);
        
        char info[INFO_LEN] = {0};
        sprintf(info, "Sent successfully to node %d: %s\n", 
            adj_nodes[adj_idx].node_num, msg.data);
        strcat(data_messages, info);      
    }

    close(sockfd);
    process_server_reply(buff, adj_idx);
}
 

void broadcast_control_message()
{
    pthread_t threads[num_adj_nodes];
    int rc, i, recv_bc;
    struct m_message msg = { CONTROL, curr_node.node_num, {0} };
    
    for(i = 0; i < num_adj_nodes; i++) 
    {
        if (adj_nodes[i].relation != UNKNOWN)
            continue;
        send_message(adj_nodes[i].node_idx, msg, 1);
    }
    
    printf("Learning complete!\n");
    this_learn_complete = 1;
    msg.type = LEARN_COMPLETE;
    msg.from_node_num = curr_node.node_num;
    for(i = 0; i < num_adj_nodes; i++) 
    {
        if (adj_nodes[i].relation == UNKNOWN)
            continue;
        send_message(adj_nodes[i].node_idx, msg, 1);
    }
}

void read_data_and_broadcast()
{
    char line[MAX_LINE_LEN], *temp_line_ptr, **lines = NULL;
    FILE *fp;
    
    fp = fopen("data.txt", "r");
    if (!fp)
        exit(EXIT_FAILURE);

    fgets(line, MAX_LINE_LEN, fp);
    if (curr_node.node_num != atoi(line))
        return;
        
    fgets(line, MAX_LINE_LEN, fp);
    struct m_message msg = { DATA, -1, {0} };
    strcpy(msg.data, line);
    close(fp);
    broadcast_data_message(msg);
}

void send_message_to_self(int retry)
{
    int sockfd, n;
    struct sockaddr_in servaddr;
    struct hostent *he;
    char hostname[MAX_HOST_NAME_LEN], buff[LEN];
    sockfd = socket(AF_INET,SOCK_STREAM, 0);
    if(sockfd < 0)
    {
        perror("Error in opening Socket: ");
        return;
    }
    //gethostname(hostname, MAX_HOST_NAME_LEN);       
    he = gethostbyname(curr_node.host); 
    
    if(he == NULL)
    {
        return;
    } 
         
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    bcopy((char *)he->h_addr, (char *)&servaddr.sin_addr.s_addr, he->h_length);
    
    servaddr.sin_port = htons(curr_node.port); 
    if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {   
        perror("Error in connection: ");
        close(sockfd);
        return;
    }
    
    bzero(buff, LEN);
    n = write(sockfd, buff, LEN); 
    n = read(sockfd, buff, LEN);
    if(n < 0)
    {
        perror("Error in reading: ");
        close(sockfd);
        if (retry)
            send_message_to_self(--retry);
        return;
    }
    close(sockfd);  
}

void broadcast_data_message(struct m_message msg)
{
    while (!n_learn_complete || !this_learn_complete)
        sleep(1);
    pthread_t threads[num_adj_nodes];
    int rc, i, num_of_forwards = 0;
    //previous node
    int from_node = msg.from_node_num;    
    msg.from_node_num = curr_node.node_num;
    for(i = 0; i < num_adj_nodes; i++) 
    {
        if (adj_nodes[i].relation == UNKNOWN
            || adj_nodes[i].node_num == from_node)
            continue;
        send_message(adj_nodes[i].node_idx, msg, 1);
        num_of_forwards++;
    }
    
    if (!num_of_forwards) {
        int node_idx = -1;
        for(i = 0; i < num_adj_nodes; i++) {
            if(adj_nodes[i].node_num == from_node) {
                node_idx = i;
                break;
            }
        }
        struct m_message ack_msg = { DATA_ACK, curr_node.node_num, {0}};
        send_message(node_idx, ack_msg, 1);
        write_to_file();
        bzero(child_messages, LEN/2);
        bzero(data_messages, LEN);
        bzero(ack_messages, LEN);      
    }
}

void process_server_reply(char *reply, int adj_idx)
{
    if(atoi(reply) == ACK_POS)
    {    
        adj_nodes[adj_idx].relation = CHILD;
        printf("My child = %d\n", adj_nodes[adj_idx].node_num);
        
        char info[INFO_LEN] = {0};
        sprintf(info, "My child = %d\n", adj_nodes[adj_idx].node_num);
        strcat(child_messages, info);
    }
}

void *wait_for_learn_completion(void *arg)
{
    int i, num_of_nodes, flag;
    
    while(1) {
        num_of_nodes = 0;
        flag = 1;
        for(i = 0; i < num_adj_nodes; i++) {
            if (adj_nodes[i].relation == UNKNOWN)
                continue;
            num_of_nodes++;
            if (!adj_nodes[i].learn_completed)  flag = 0;
        }
        
        if (num_of_nodes && flag)
            break;
        
        sleep(2);
    }
    printf("Neighbors Learn Complete!\n");
    n_learn_complete = 1;
}

void write_to_file()
{
    FILE *fp;
    char filename[20];
    sprintf(filename, "output%d.txt", curr_node.node_num);
    
    //fp = fopen(filename, "wx");
    //if (fp)
    //    fclose(fp);
    fp = fopen(filename, "a");
    if (!fp)
        return;
    fprintf(fp, "%s%s%s", child_messages, data_messages,
            ack_messages);
      
    fflush(fp);    
    fclose(fp);
}
