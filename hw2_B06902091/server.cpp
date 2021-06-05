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
#include "opencv2/opencv.hpp"
#include <signal.h>

#define BUFF_SIZE 1024

using namespace std;
using namespace cv;

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    //wrong arg
    if (argc != 2)
    {
        cout << "usage: ./server [port]" << endl;
        return 0;
    }

    //make own directory
    struct stat st = {0};
    if (stat("server_f", &st) == -1)
    {
        mkdir("server_f", 0777);
    }
    chdir("server_f");

    //obtain port
    char *port_str = argv[1];
    int port = atoi(port_str);

    //create socket
    int server_socket, new_socket;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0)
    {
        cout << "socket() error" << endl;
        return 0;
    }

    //connection info
    int addrLen = sizeof(struct sockaddr_in);
    struct sockaddr_in server_addr, new_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; //
    server_addr.sin_port = htons(port);

    //bind
    int ret = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0)
    {
        cout << "bind() error";
        return 0;
    }

    //listen
    listen(server_socket, 3);

    //select: fd_set
    fd_set active_fd_set;
    FD_ZERO(&active_fd_set);
    FD_SET(server_socket, &active_fd_set);

    cout << "Success" << endl;

    while (1)
    {
        //select
        fd_set read_fd_set;
        read_fd_set = active_fd_set;

        int select_ret = select(FD_SETSIZE, &read_fd_set, NULL, NULL, NULL);
        if (select_ret < 0)
        {
            cout << "select() error" << endl;
            return 0;
        }
        else if (select_ret == 0)
        {
            cout << "select() time out";
            continue;
        }

        //multiplexing
        for (int i = 0; i < FD_SETSIZE; i++)
        {
            if (FD_ISSET(i, &read_fd_set))
            {
                if (i == server_socket)
                {
                    new_socket = accept(server_socket, (struct sockaddr *)&new_addr, (socklen_t *)&addrLen);
                    if (new_socket < 0)
                    {
                        cout << "accept() error";
                        return 0;
                    }
                    FD_SET(new_socket, &active_fd_set);
                }
                else
                {
                    //variable
                    int sent;
                    int recved;

                    char buffer_sent[BUFF_SIZE] = {};
                    char buffer_recved[BUFF_SIZE] = {};

                    //receive msg from client [0]
                    bzero(buffer_recved, BUFF_SIZE);
                    recved = recv(i, buffer_recved, BUFF_SIZE, 0);

                    if (recved == 0)
                    {
                        cout << "Client disconnected" << endl;
                        close(i);
                        FD_CLR(i, &active_fd_set);
                        continue;
                    }
                    else if (recved == -1)
                    {
                        cout << "recv() error";
                        close(i);
                        FD_CLR(i, &active_fd_set);
                        continue;
                    }

                    //get cmd
                    char command[BUFF_SIZE] = {};
                    char filename[BUFF_SIZE] = {};
                    sscanf(buffer_recved, "%s %s", command, filename);
                    cout <<"client ["<< i << "] : "<< command << " " << filename<< endl;

                    //command case
                    if (strcmp(command, "ls") == 0)
                    {
                        //handshaking
                        bzero(buffer_sent, BUFF_SIZE);

                        DIR *d;
                        struct dirent *dir;
                        d = opendir(".");
                        if (d)
                        {
                            while (dir = readdir(d))
                            {
                                strcat(buffer_sent, dir->d_name);
                                strcat(buffer_sent, " ");
                            }
                            closedir(d);
                            strcat(buffer_sent, "\n");
                            sent = send(i, buffer_sent, BUFF_SIZE, 0);
                        }
                    }
                    else if (strcmp(command, "put") == 0)
                    {
                        //send response to client [1]
                        sprintf(buffer_sent, "OK");
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);

                        //create or open file
                        FILE *fp = fopen(filename, "wb");

                        //get file size from client [2]
                        int file_size;
                        bzero(buffer_recved, BUFF_SIZE);
                        recved = recv(i, buffer_recved, BUFF_SIZE, 0);
                        sscanf(buffer_recved, "%d", &file_size);

                        //send response to client and send [3]
                        sent = send(i, buffer_recved, BUFF_SIZE, 0);

                        //get file
                        int buf_size;
                        char buf[BUFF_SIZE] = {};

                        while ((buf_size = recv(i, buf, BUFF_SIZE, 0)) > 0)
                        {
                            fwrite(buf, sizeof(char), buf_size, fp);
                            fflush(fp);
                            bzero(buf, BUFF_SIZE);
                            file_size -= buf_size;
                            if (file_size == 0)
                            {
                                break;
                            }
                        }
                        fclose(fp);

                        sprintf(buffer_sent, "DONE");
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);
                    }
                    else if (strcmp(command, "get") == 0)
                    {
                        int file_flag = 1;

                        //check file exist
                        if (access(filename, F_OK) != 0)
                        {
                            file_flag = 0;
                        }

                        //check file can open
                        FILE *fp = fopen(filename, "rb");
                        if (!fp)
                        {
                            file_flag = 0;
                        }

                        //check file_flag
                        bzero(buffer_sent, BUFF_SIZE);

                        if (file_flag == 0)
                        {
                            sprintf(buffer_sent, "The ‘%s’ doesn’t exist.", filename);
                            sent = send(i, buffer_sent, BUFF_SIZE, 0);
                            continue;
                        }

                        //send response to client [1]
                        bzero(buffer_sent, BUFF_SIZE);
                        sprintf(buffer_sent, "OK");
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);

                        //send file size to client [2]
                        int file_size;
                        fseek(fp, 0L, SEEK_END);
                        file_size = ftell(fp);
                        fseek(fp, 0L, SEEK_SET);
                        bzero(buffer_sent, BUFF_SIZE);
                        sprintf(buffer_sent, "%d", file_size);
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);

                        //get response from client and send [3]
                        bzero(buffer_recved, BUFF_SIZE);
                        recved = recv(i, buffer_recved, BUFF_SIZE, 0);

                        //send file
                        int buf_size;
                        char buf[BUFF_SIZE];
                        while ((buf_size = fread(buf, sizeof(char), BUFF_SIZE, fp)) > 0)
                        {
                            send(i, buf, buf_size, 0);
                            bzero(buf, BUFF_SIZE);
                        }
                        fclose(fp);
                    }
                    else if (strcmp(command, "play") == 0)
                    {

                        //check file exist
                        if (access(filename, F_OK) != 0)
                        {
                            sprintf(buffer_sent, "The ‘%s’ doesn’t exist.", filename);
                            sent = send(i, buffer_sent, BUFF_SIZE, 0);
                            continue;
                        }

                        //check .mpg
                        int mpg_flag = 1;
                        char mpg_str[10] = ".mpg";
                        int file_len = strlen(filename);
                        int mpg_len = strlen(mpg_str);

                        if(file_len < mpg_len) {
                            mpg_flag = 0;
                        }
                        else{
                            if(strncmp(filename+file_len-mpg_len,mpg_str, mpg_len ) == 0){
                                mpg_flag = 1;
                            }
                            else{
                                mpg_flag = 0;
                            }
                        }

                        if(mpg_flag == 0){
                            sprintf(buffer_sent, "The ‘%s’ is not a mpg file.", filename);
                            sent = send(i, buffer_sent, BUFF_SIZE, 0);
                            continue;
                        }

                        //send response to client [1]
                        bzero(buffer_sent, BUFF_SIZE);
                        sprintf(buffer_sent, "OK");
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);

                        //send video size to client [2]
                        Mat imgServer;
                        VideoCapture cap(filename);

                        int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
                        int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
                        cout << "video size: " << width << " x " << height << endl;

                        bzero(buffer_sent, BUFF_SIZE);
                        sprintf(buffer_sent, "%d %d\n", width, height);
                        sent = send(i, buffer_sent, BUFF_SIZE, 0);

                        imgServer = Mat::zeros(width, height, CV_8UC3);

                        if (!imgServer.isContinuous())
                        {
                            imgServer = imgServer.clone();
                        }

                        while (1)
                        {
                            //send frame size [3]
                            cap >> imgServer;
                            if (imgServer.empty())
                            {
                                bzero(buffer_sent, BUFF_SIZE);
                                sprintf(buffer_sent, "0");
                                sent = send(i, buffer_sent, BUFF_SIZE, 0);
                                break;
                            }
                            int imgSize = imgServer.total() * imgServer.elemSize();
                            bzero(buffer_sent, BUFF_SIZE);
                            sprintf(buffer_sent, "%d", imgSize);
                            sent = send(i, buffer_sent, BUFF_SIZE, 0);

                            //get response to server [4]
                            bzero(buffer_recved, BUFF_SIZE);
                            recved = recv(i, buffer_recved, BUFF_SIZE, 0);

                            //copy a frame
                            uchar buffer[imgSize];
                            memcpy(buffer, imgServer.data, imgSize);

                            //send to client [5]
                            int nbyte = 0;
                            while (nbyte < imgSize)
                            {
                                sent = send(i, buffer + nbyte, imgSize - nbyte, 0);
                                nbyte += sent;
                            }


                            //clear buffer
                            bzero(buffer, BUFF_SIZE);

                            //client stop?
                            bzero(buffer_recved, BUFF_SIZE);
                            recved = recv(i, buffer_recved, BUFF_SIZE, 0);
                            if (strcmp(buffer_recved, "STOP") == 0)
                            {
                                cout << "Client stop the streaming" << endl;
                                break;
                            }
                        }
                        cap.release();
                        continue;
                    }
                }
            }
        }
    }
    close(server_socket);
    return 0;
}