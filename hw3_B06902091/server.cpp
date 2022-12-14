#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <ctime>
#include "opencv2/opencv.hpp"
#include <signal.h>

using namespace std;
using namespace cv;

int kbhit()
{
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &fds);
}

typedef struct
{
    int length;
    int seqNumber;
    int ackNumber;
    int fin;
    int syn;
    int ack;
} header;

typedef struct
{
    header head;
    char data[1000];
} segment;

void setIP(char *dst, const char *src)
{
    if (strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0 || strcmp(src, "localhost"))
    {
        sscanf("127.0.0.1", "%s", dst);
    }
    else
    {
        sscanf(src, "%s", dst);
    }
}

int main(int argc, char **argv)
{
    char ip[2][50];
    int port[2];
    char filename[200];

    if (argc != 6)
    {
        fprintf(stderr, "用法: %s <server IP> <agent IP> <server port> <agent port> <filename>\n", argv[0]);
        fprintf(stderr, "例如: ./server local local 8887 8888 tmp.mpg\n");
        exit(1);
    }
    else
    {
        setIP(ip[0], "local"); //server
        setIP(ip[1], argv[2]); //agent

        sscanf(argv[3], "%d", &port[0]); //server
        sscanf(argv[4], "%d", &port[1]); //agent

        sscanf(argv[5], "%s", filename);
    }

    /*change directory*/
    chdir("video");

    /*Create UDP socket*/
    int serversocket;
    serversocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*server struct(own)*/
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port[0]);
    server.sin_addr.s_addr = inet_addr(ip[0]);
    memset(server.sin_zero, '\0', sizeof(server.sin_zero));

    /*bind socket*/
    bind(serversocket, (struct sockaddr *)&server, sizeof(server));

    /*Configure settings in agent struct*/
    struct sockaddr_in agent;
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[1]);
    agent.sin_addr.s_addr = inet_addr(ip[1]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

    /*size variable*/
    struct sockaddr_in tmp_addr;
    socklen_t server_size, agent_size, tmp_size;
    server_size = sizeof(server);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);

    /*check file exist*/
    if (access(filename, F_OK) != 0)
    {
        printf("The ‘%s’ doesn’t exist.", filename);
        return 0;
    }

    /*initial variable*/
    int window_size = 1;
    int threshold = 16;
    segment send_seg, recv_seg;
    fd_set set;

    int index = 0;
    int bound = 0;
    int ack_recv = 0;
    int ack_expect = 0;
    int loss_flag = 0;
    int last_frame_num = 0;

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    /*capture frame setting*/
    Mat imgServer;
    VideoCapture cap(filename);
    int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
    int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
    imgServer = Mat::zeros(width, height, CV_8UC3);
    if (!imgServer.isContinuous())
    {
        imgServer = imgServer.clone();
    }

    /*For each frame*/
    while (!kbhit())
    {
        cap >> imgServer;
        if (imgServer.empty())
        {
            break;
        }

        int imgSize = imgServer.total() * imgServer.elemSize();
        uchar frame_buf[imgSize];
        memcpy(frame_buf, imgServer.data, imgSize);

        /*byte sent*/
        int nbyte = 0;

        /*one frame to packet*/
        while (1)
        {
            /*send packet in N*/
            for (int j = 0; j < window_size; j++)
            {
                memset(&send_seg, 0, sizeof(send_seg));

                /*first packet*/
                if (index == 0)
                {
                    /*send size to agent first*/
                    sprintf(send_seg.data, "%d %d %d", imgSize, width, height);
                    send_seg.head.length = strlen(send_seg.data);
                    send_seg.head.seqNumber = 0;
                    send_seg.head.ackNumber = 0;
                    send_seg.head.fin = 0;
                    send_seg.head.syn = 0;
                    send_seg.head.ack = 0;
                    sendto(serversocket, &send_seg, sizeof(send_seg), 0, (struct sockaddr *)&agent, agent_size);
                    index = 1;
                }
                else
                {
                    /*copy frame to packet*/
                    if (imgSize - nbyte >= 1000)
                    {
                        memcpy(send_seg.data, frame_buf + nbyte, 1000);
                        send_seg.head.length = 1000;
                        nbyte += 1000;
                    }
                    else
                    {
                        memcpy(send_seg.data, frame_buf + nbyte, imgSize - nbyte);
                        send_seg.head.length = imgSize - nbyte;
                        nbyte += imgSize - nbyte;
                    }

                    /*send packet*/
                    send_seg.head.seqNumber = index;
                    send_seg.head.ackNumber = 0;
                    send_seg.head.fin = 0;
                    send_seg.head.syn = 0;
                    send_seg.head.ack = 0;
                    sendto(serversocket, &send_seg, sizeof(send_seg), 0, (struct sockaddr *)&agent, agent_size);

                    /*message*/
                    if (index <= ack_expect)
                    {
                        printf("resend	data	#%d,	winSize = %d\n", index, window_size);
                    }
                    else
                    {
                        printf("send	data	#%d,	winSize = %d\n", index, window_size);
                    }

                    /*update idx*/
                    index++;

                    /*one frame is sent*/
                    if (nbyte >= imgSize)
                    {
                        break;
                    }
                }
            }
            /*expect ack*/
            bound = index - 1;

            /*Check Ack*/
            while (1)
            {
                FD_ZERO(&set);
                FD_SET(serversocket, &set);
                int ret = select(serversocket + 1, &set, NULL, NULL, &timeout);
                if (ret == 0)
                {
                    /*timeout*/
                    threshold = max((window_size / 2), 1);
                    window_size = 1;
                    printf("time    out,            threshold = %d\n", threshold);

                    /*retransmit*/
                    loss_flag = 1;
                    break;
                }
                else if (ret > 0)
                {
                    /*normal ack*/
                    int recv_size = recvfrom(serversocket, &recv_seg, sizeof(recv_seg), 0, (struct sockaddr *)&tmp_addr, &tmp_size);
                    printf("recv	ack	#%d\n", recv_seg.head.ackNumber);
                    if (recv_seg.head.ackNumber > ack_recv)
                    {
                        ack_recv = recv_seg.head.ackNumber;
                    }
                }
                else
                {
                    exit(1);
                }
                if (ack_recv == bound)
                {
                    loss_flag = 0;
                    break;
                }
            }

            /*loss and update window size*/
            if (loss_flag == 0)
            {
                if (window_size < threshold)
                {
                    window_size = window_size * 2;
                }
                else
                {
                    window_size++;
                }
            }

            /*update ack and index and nbyte*/
            ack_expect = bound;
            index = ack_recv + 1;
            nbyte = (ack_recv - last_frame_num) * 1000; /******/
            if (nbyte >= imgSize)
            {
                nbyte = 0;
                break;
            }
        }

        /*update last_frame_ack_number*/
        last_frame_num = index - 1;
    }

    /*send fin*/
    send_seg.head.length = 0;
    send_seg.head.seqNumber = -1;
    send_seg.head.ackNumber = 0;
    send_seg.head.fin = 1;
    send_seg.head.syn = 0;
    send_seg.head.ack = 0;
    sendto(serversocket, &send_seg, sizeof(send_seg), 0, (struct sockaddr *)&agent, agent_size);
    printf("send	fin\n");

    /*recv fin ack*/
    while (1)
    {
        if ((recvfrom(serversocket, &recv_seg, sizeof(recv_seg), 0, (struct sockaddr *)&tmp_addr, &tmp_size)) > 0)
        {
            if (recv_seg.head.fin == 1 && recv_seg.head.ack == 1)
            {
                printf("recv	finack\n");
                return 0;
            }
        }
        else{
            continue;
        }
    }

    return 0;
}