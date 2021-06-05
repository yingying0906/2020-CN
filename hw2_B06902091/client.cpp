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

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    //wrong arg
    if (argc != 2)
    {
        cout << "usage: ./client [ip:port]" << endl;
        return 0;
    }

    //make own directory
    struct stat st = {0};
    if (stat("client_f", &st) == -1)
    {
        mkdir("client_f", 0777);
    }
    chdir("client_f");

    //obtain port
    int port;
    char ip_str[50];
    char argv_str[50];
    strcpy(argv_str, argv[1]);
    char *token = strtok(argv_str, ":");
    sscanf(token, "%s", ip_str);
    token = strtok(NULL, ":");
    sscanf(token, "%d", &port);

    cout << "connecting to ip = " << ip_str << endl;
    cout << "connecting to port = " << port << endl;

    //create socket
    int client_socket;
    client_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (client_socket == -1)
    {
        cout << "socket() error" << endl;
        return 0;
    }

    //connection struct
    int addrLen = sizeof(struct sockaddr_in);
    struct sockaddr_in client_addr;
    client_addr.sin_family = PF_INET;
    client_addr.sin_addr.s_addr = inet_addr(ip_str);
    client_addr.sin_port = htons(port);

    //connect to server
    int ret = connect(client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if (ret < 0)
    {
        cout << "connect() error" << endl;
        return 0;
    }

    while (1)
    {
        //variable
        int recved;
        int sent;

        char buffer_recved[BUFF_SIZE] = {};
        char buffer_sent[BUFF_SIZE] = {};
        char client_input[BUFF_SIZE] = {};
        char client_output[BUFF_SIZE] = {};
        char command[BUFF_SIZE] = {};
        char filename[BUFF_SIZE] = {};

        //scan command from client
        cout << "Enter command: " << endl;
        scanf("%[^\n]%*c", client_input);
        sscanf(client_input, "%s %s", command, filename);

        //command case
        if (strcmp(command, "ls") == 0)
        {
            //send request
            sent = send(client_socket, client_input, BUFF_SIZE, 0);

            //recv list
            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            //output
            char str[BUFF_SIZE];
            const char s[2] = " ";
            strcpy(str, buffer_recved);
            char *tokenls;
            tokenls = strtok(str, s);
            while (tokenls != NULL)
            {
                cout << tokenls << endl;
                tokenls = strtok(NULL, s);
            }
        }
        else if (strcmp(command, "put") == 0)
        {
            //check format
            if (strlen(filename) == 0)
            {
                cout << "Command format error." << endl;
                continue;
            }

            //check file exist
            if (access(filename, F_OK) != 0)
            {
                sprintf(client_output, "The ‘%s’ doesn’t exist.", filename);
                cout << client_output << endl;
                continue;
            }

            //check file can open
            FILE *fp = fopen(filename, "rb");
            if (!fp)
            {
                continue;
            }

            //send request to server [0]
            sent = send(client_socket, client_input, BUFF_SIZE, 0);

            //get response from server [1]
            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            if(recved == 0){
                cout << "server error" << endl;
                return 0;
            }

            if (strcmp(buffer_recved, "OK") != 0)
            {
                cout << "server not ready" << endl;
                continue;
            }

            //send file size to server [2]
            int file_size;
            fseek(fp, 0L, SEEK_END);
            file_size = ftell(fp);
            fseek(fp, 0L, SEEK_SET);
            bzero(buffer_sent, BUFF_SIZE);
            sprintf(buffer_sent, "%d", file_size);
            send(client_socket, buffer_sent, BUFF_SIZE, 0);

            //get response from server and send [3]
            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            //send file
            int buf_size;
            char buf[BUFF_SIZE];

            while ((buf_size = fread(buf, sizeof(char), BUFF_SIZE, fp)) > 0)
            {
                send(client_socket, buf, buf_size, 0);
                bzero(buf, BUFF_SIZE);
            }
            fclose(fp);

            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);
        }
        else if (strcmp(command, "get") == 0)
        {
            //check format
            if (strlen(filename) == 0)
            {
                cout << "Command format error." << endl;
                continue;
            }

            //send request to server [0]
            sent = send(client_socket, client_input, BUFF_SIZE, 0);

            //get response from server [1]
            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            //check server response
            if (recved == 0)
            {
                cout << "server error" << endl;
                return 0;
            }

            if (strcmp(buffer_recved, "OK") != 0)
            {
                sprintf(client_output, "The ‘%s’ doesn’t exist.", filename);
                cout << client_output << endl;
                continue;
            }

            //create or open file
            FILE *fp = fopen(filename, "wb");

            //get file size from server [2]
            int file_size;
            int buf_size;
            char buf[BUFF_SIZE] = {};
            recved = recv(client_socket, buf, BUFF_SIZE, 0);
            sscanf(buf, "%d", &file_size);

            //send response to server and send [3]
            sent = send(client_socket, buf, BUFF_SIZE, 0); //response to server

            //recv file
            bzero(buf, BUFF_SIZE);
            while ((recved = recv(client_socket, buf, BUFF_SIZE, 0)) > 0)
            {
                fwrite(buf, sizeof(char), recved, fp);
                fflush(fp);
                bzero(buf, BUFF_SIZE);
                file_size -= recved;
                if (file_size == 0)
                {
                    break;
                }
            }
            fclose(fp);
        }
        else if (strcmp(command, "play") == 0)
        {
            //check format
            if (strlen(filename) == 0)
            {
                cout << "Command format error." << endl;
                continue;
            }

            //send request to server [0]
            sent = send(client_socket, client_input, BUFF_SIZE, 0);

            //get response from server [1]
            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            //check server response
            if (recved == 0)
            {
                cout << "server error" << endl;
                return 0;
            }

            if (strcmp(buffer_recved, "OK") != 0)
            {
                cout << buffer_recved << endl;
                continue;
            }

            //get video width from server [2]
            Mat imgClient;
            int width;
            int height;
            int imgSize;

            bzero(buffer_recved, BUFF_SIZE);
            recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);

            sscanf(buffer_recved, "%d %d", &width, &height);
            cout << "video size: " << width << " x " << height << endl;

            imgClient = Mat::zeros(height, width, CV_8UC3);

            while (1)
            {
                //get frame size [3]
                bzero(buffer_recved, BUFF_SIZE);
                recved = recv(client_socket, buffer_recved, BUFF_SIZE, 0);
                sscanf(buffer_recved, "%d", &imgSize);

                if (imgSize == 0)
                {
                    destroyAllWindows();
                    break;
                }
                
                //response to server [4]
                bzero(buffer_sent, BUFF_SIZE);
                sprintf(buffer_sent, "%d", imgSize);
                sent = send(client_socket, buffer_sent, BUFF_SIZE, 0);

                //get frame [5]
                uchar buffer[imgSize];

                int nbyte = 0;

                while (nbyte < imgSize)
                {
                    recved = recv(client_socket, buffer + nbyte, imgSize - nbyte, 0);
                    nbyte += recved;
                }

                //show frame
                uchar *iptr = imgClient.data;
                memcpy(iptr, buffer, imgSize);
                imshow("Video", imgClient);

                //clear buffer
                bzero(buffer, BUFF_SIZE);

                //client stop?
                char c = (char)waitKey(33.3333);
                if (c == 27)
                {
                    bzero(buffer_sent, BUFF_SIZE);
                    sprintf(buffer_sent, "STOP");
                    sent = send(client_socket, buffer_sent, BUFF_SIZE, 0);
                    destroyAllWindows();
                    break;
                }
                else
                {
                    bzero(buffer_sent, BUFF_SIZE);
                    sprintf(buffer_sent, "CON");
                    sent = send(client_socket, buffer_sent, BUFF_SIZE, 0);
                }
            }
        }
        else
        {
            cout << "Command not found." << endl;
            continue;
        }
    }
}