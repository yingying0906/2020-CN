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

    if (argc != 5)
    {
        fprintf(stderr, "用法: %s <receiver IP> <agent IP> <receiver port> <agent port>\n", argv[0]);
        fprintf(stderr, "例如: ./receiver local local 8887 8888 folder\n");
        exit(1);
    }
    else
    {
        setIP(ip[0], "local"); //server
        setIP(ip[1], argv[2]); //agent

        sscanf(argv[3], "%d", &port[0]); //receiver
        sscanf(argv[4], "%d", &port[1]); //agent
    }

    /*Create UDP socket*/
    int recvsocket;
    recvsocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*recv struct(own)*/
    struct sockaddr_in recvr;
    recvr.sin_family = AF_INET;
    recvr.sin_port = htons(port[0]);
    recvr.sin_addr.s_addr = inet_addr(ip[0]);
    memset(recvr.sin_zero, '\0', sizeof(recvr.sin_zero));

    /*bind socket*/
    bind(recvsocket, (struct sockaddr *)&recvr, sizeof(recvr));

    /*Configure settings in agent struct*/
    struct sockaddr_in agent;
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[1]);
    agent.sin_addr.s_addr = inet_addr(ip[1]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

    /*size variable*/
    struct sockaddr_in tmp_addr;
    socklen_t recvr_size, agent_size, tmp_size;
    recvr_size = sizeof(recvr);
    agent_size = sizeof(agent);
    tmp_size = sizeof(tmp_addr);

    /*variable*/
    segment send_seg, recv_seg;
    segment seg_buff[32];
    int index_ack = 0;
    int index_buf = 0;

    /*receive frame size*/
    Mat imgClient;
    int width;
    int height;
    int imgSize;
    int frame_offset = 0;
    int recv_size;

    uchar *frame_buf;

    while (1)
    {
        /*recieve packet*/
        int recv_size;
        if ((recv_size = recvfrom(recvsocket, &recv_seg, sizeof(recv_seg), 0, (struct sockaddr *)&tmp_addr, &tmp_size)) > 0)
        {
            /*first packet*/
            if (recv_seg.head.seqNumber == 0)
            {
                /*get width height and imgsize*/
                sscanf(recv_seg.data, "%d %d %d", &imgSize, &width, &height);
                imgClient = Mat::zeros(height, width, CV_8UC3);
                if (!imgClient.isContinuous())
                {
                    imgClient = imgClient.clone();
                }

                /*frame buf malloc*/
                frame_buf = (uchar *)malloc(imgSize);

                continue;
            }
            /*receive fin*/
            if (recv_seg.head.fin == 1)
            {
                printf("recv	fin\n");
                //make pkt
                send_seg.head.length = 0;
                send_seg.head.seqNumber = 0;
                send_seg.head.ackNumber = 0;
                send_seg.head.fin = 1;
                send_seg.head.syn = 0;
                send_seg.head.ack = 1;

                //send to
                sendto(recvsocket, &send_seg, sizeof(send_seg), 0, (struct sockaddr *)&agent, agent_size);

                //message
                printf("send	finack\n");
                break;
            }
            else
            {
                /*correct seq number*/
                if (recv_seg.head.seqNumber == index_ack + 1)
                {
                    /*message*/
                    printf("recv	data	#%d\n", recv_seg.head.seqNumber);

                    /*copy to buffer*/
                    memcpy(&seg_buff[index_buf], &recv_seg, sizeof(recv_seg));

                    /*update ack and buffer idx*/
                    index_ack = recv_seg.head.seqNumber;
                    index_buf++;
                }
                else
                {
                    /*drop packet*/
                    printf("drop	data	#%d\n", recv_seg.head.seqNumber);
                }

                /*Send ack*/
                send_seg.head.length = 0;
                send_seg.head.seqNumber = recv_seg.head.seqNumber;
                send_seg.head.ackNumber = index_ack;
                send_seg.head.fin = 0;
                send_seg.head.syn = 0;
                send_seg.head.ack = 1;
                sendto(recvsocket, &send_seg, sizeof(send_seg), 0, (struct sockaddr *)&agent, agent_size);
                printf("send	ack     #%d\n", index_ack);

                /*flush*/
                if (index_buf == 32)
                {
                    /*write buf to frame*/
                    for (int j = 0; j < 32; j++)
                    {
                        memcpy(frame_buf + frame_offset, seg_buff[j].data, seg_buff[j].head.length);
                        frame_offset += seg_buff[j].head.length;

                        /*if frame is full, show it*/
                        if (frame_offset >= imgSize)
                        {
                            memcpy(imgClient.data, frame_buf, imgSize);
                            imshow("window", imgClient);
                            waitKey(33.3333);

                            /*reset*/
                            frame_offset = 0;
                            memset(frame_buf, 0, sizeof(frame_buf));
                        }
                    }

                    /*reset buf idx -> 0*/
                    index_buf = 0;

                    /*message*/
                    printf("flush\n");
                    memset(seg_buff, 0, sizeof(seg_buff));
                }
            }
        }
    }

    /*flush remain buffer*/
    for (int j = 0; j < index_buf; j++)
    {
        memcpy(frame_buf + frame_offset, seg_buff[j].data, seg_buff[j].head.length);
        frame_offset += seg_buff[j].head.length;

        /*if frame is full, show it*/
        if (frame_offset >= imgSize)
        {
            memcpy(imgClient.data, frame_buf, imgSize);
            imshow("window", imgClient);
            waitKey(33.3333);
            
            /*reset*/
            frame_offset = 0;
            memset(frame_buf, 0, sizeof(frame_buf));
        }
    }
    printf("flush\n");

    return 0;
}