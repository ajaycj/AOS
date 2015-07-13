#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>

#define LEN 1024

enum Message_type {
    CONTROL,
    DATA,
    DATA_ACK,
    LEARN_COMPLETE,
    DATA_INIT
};

struct m_message {
    enum Message_type type;
    int from_node_num;
    char data[LEN - sizeof(int) - sizeof(enum Message_type)];
};

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Invalid Arguments!\n");
        exit(EXIT_FAILURE);
    }
    
    int sockfd, n;
    struct sockaddr_in servaddr;
    struct hostent *he;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    char *hostname = argv[1], *data = argv[3], buff[LEN];
    int port = atoi(argv[2]);
    
    if(sockfd < 0)
    {
        perror("Error in opening Socket: ");
        exit(EXIT_FAILURE);; 
    }
       
    he = gethostbyname(hostname); 
    
    if(he == NULL)
     {
        exit(EXIT_FAILURE);;
     } 
    
    
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    bcopy((char *)he->h_addr, (char *)&servaddr.sin_addr.s_addr, he->h_length);
    
    servaddr.sin_port = htons(port); 
    if(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {   
        perror("Error in connection: ");
        close(sockfd);
        exit(EXIT_FAILURE);;
    }
    
    struct m_message msg = { DATA_INIT, -1, {0} };
    strcpy(msg.data, data);
    memcpy(buff, &msg, LEN);
    n = write(sockfd, buff, LEN);
    if(n < 0)
    {
        perror("Error in writing: ");
        close(sockfd);
        
        exit(EXIT_FAILURE);;
    }
    bzero(buff, LEN);
    n = read(sockfd, buff, LEN);
    if(n < 0)
    {
        perror("Error in reading: ");
        close(sockfd);
        exit(EXIT_FAILURE);;
    }
    printf("Message from server:\n%s\n", buff);

    close(sockfd);

    return 0;
}

