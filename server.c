#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <sys/socket.h>

/**
 * Project 1 starter code
 * All parts needed to be changed/added are marked with TODO
 */

#define BUFFER_SIZE 1024
#define DEFAULT_SERVER_PORT 8081
#define DEFAULT_REMOTE_HOST "131.179.176.34"
#define DEFAULT_REMOTE_PORT 5001

struct server_app
{
    // Parameters of the server
    // Local port of HTTP server
    uint16_t server_port;

    // Remote host and port of remote proxy
    char *remote_host;
    uint16_t remote_port;
};

/**
 * Readable alias for an integer representing a socket file descriptor.
 */
typedef int sockfd_t;

/**
 * Readable alias for using strcmp to check C-string equality.
 */
#define STRING_EQUALS(s1, s2) ((s1) && (s2) && strcmp((s1), (s2)) == 0)

// The following function is implemented for you and doesn't need
// to be change
void parse_args(int argc, char *argv[], struct server_app *app);

// The following functions need to be updated
void handle_request(struct server_app *app, int client_socket);
void serve_local_file(int client_socket, const char *path);
void proxy_remote_file(struct server_app *app, int client_socket, const char *path);

static void send_bad_gateway(sockfd_t sockfd);

// The main function is provided and no change is needed
int main(int argc, char *argv[])
{
    struct server_app app;
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;

    parse_args(argc, argv, &app);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(app.server_port);

    // The following allows the program to immediately bind to the port in case
    // previous run exits recently
    int optval = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) == -1)
    {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", app.server_port);

    while (1)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket == -1)
        {
            perror("accept failed");
            continue;
        }

        printf("Accepted connection from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        handle_request(&app, client_socket);
        close(client_socket);
    }

    close(server_socket);
    return 0;
}

void parse_args(int argc, char *argv[], struct server_app *app)
{
    int opt;

    app->server_port = DEFAULT_SERVER_PORT;
    app->remote_host = NULL;
    app->remote_port = DEFAULT_REMOTE_PORT;

    while ((opt = getopt(argc, argv, "b:r:p:")) != -1)
    {
        switch (opt)
        {
        case 'b':
            app->server_port = atoi(optarg);
            break;
        case 'r':
            app->remote_host = strdup(optarg);
            break;
        case 'p':
            app->remote_port = atoi(optarg);
            break;
        default: /* Unrecognized parameter or "-?" */
            fprintf(stderr, "Usage: server [-b local_port] [-r remote_host] [-p remote_port]\n");
            exit(-1);
            break;
        }
    }

    if (app->remote_host == NULL)
    {
        app->remote_host = strdup(DEFAULT_REMOTE_HOST);
    }
}

void handle_request(struct server_app *app, int client_socket)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Read the request from HTTP client
    // Note: This code is not ideal in the real world because it
    // assumes that the request header is small enough and can be read
    // once as a whole.
    // However, the current version suffices for our testing.
    bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0)
    {
        return; // Connection closed or error
    }

    buffer[bytes_read] = '\0';
    // copy buffer to a new string
    char *request = malloc(strlen(buffer) + 1);
    strcpy(request, buffer);

    // TODO: Parse the header and extract essential fields, e.g. file name
    // Hint: if the requested path is "/" (root), default to index.html

    // Iterates over HTTP request to find length of file name for extraction.

    size_t index = 5;
    size_t characters_in_file_name = 0;

    while (request[index] != ' ')
    {
        characters_in_file_name++;
        index++;
    }

    char file_name[characters_in_file_name + 1];
    strncpy(file_name, request + 5, characters_in_file_name);
    file_name[characters_in_file_name] = '\0';

    // If the requested path is "/" (root), defaults to index.html

    if (strlen(file_name) == 0)
    {
        strcpy(file_name, "index.html");
    }

    else
    {
        // Converts %20 and %25 from url-encoding to space and % respectively.

        for (size_t i = 0; i < strlen(file_name) - 2; i++)
        {
            if (file_name[i] == '%')
            {
                if (file_name[i + 1] == '2')
                {
                    if (file_name[i + 2] == '0')
                    {
                        file_name[i] = ' ';
                        memmove(&file_name[i + 1], &file_name[i + 3], strlen(&file_name[i + 3]) + 1);
                    }
                    else if (file_name[i + 2] == '5')
                    {
                        file_name[i] = '%';
                        memmove(&file_name[i + 1], &file_name[i + 3], strlen(&file_name[i + 3]) + 1);
                    }
                }
            }
        }
    }

    // Extract file extension (assumed to start at the last occurrence of '.').
    const char *extension = strrchr(file_name, '.');

    if (STRING_EQUALS(extension, ".ts"))
        proxy_remote_file(app, client_socket, request);
    else
        serve_local_file(client_socket, file_name);
}

void serve_local_file(int client_socket, const char *path)
{
    printf("Serving %s from local filesystem.\n", path);

    // TODO: Properly implement serving of local files
    // The following code returns a dummy response for all requests
    // but it should give you a rough idea about what a proper response looks like
    // What you need to do
    // (when the requested file exists):
    // * Open the requested file
    // * Build proper response headers (see details in the spec), and send them
    // * Also send file content
    // (When the requested file does not exist):
    // * Generate a correct response

    // Tries to open requested file at path.

    FILE *file = fopen(path, "r");

    // If file does not exist, responds with a message saying "Requested file does not exist.".

    if (file == NULL)
    {
        char response[] = "HTTP/1.0 404 Not Found\r\n"
                          "Content-Type: text/plain; charset=UTF-8\r\n"
                          "Content-Length: 30\r\n"
                          "\r\n"
                          "Requested file does not exist."
                          "\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // Reads from file at path and stores data in content.

    char *content;
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    content = (char *)malloc(file_size);
    fread(content, 1, file_size, file);
    fclose(file);

    // Converts file_size from size_t to string to append to HTTP response message.

    char response2[sizeof(size_t)];
    sprintf(response2, "%ld", file_size);

    // Extracts file extension if it exists.

    const char *extension = strrchr(path, '.');

    // Following checks respond accordingly based on extension of file which include cases of:
    // no extension, .html extension, .txt extension, and .jpg extension

    if (!extension)
    {
        // Sets content-type to application/octet-stream.
        // Concatenates different parts of the response.

        char response1[] = "HTTP/1.0 200 OK\r\n"
                           "Content-Type: application/octet-stream; charset=UTF-8\r\n"
                           "Content-Length: ";

        send(client_socket, response1, strlen(response1), 0);
        send(client_socket, response2, strlen(response2), 0);
        send(client_socket, "\r\n\r\n", 4, 0);
        send(client_socket, content, file_size, 0);
    }
    else if (!(strcmp(extension, ".html")))
    {
        // Sets content-type to text/html.
        // Concatenates different parts of the response.

        char response1[] = "HTTP/1.0 200 OK\r\n"
                           "Content-Type: text/html; charset=UTF-8\r\n"
                           "Content-Length: ";

        char response[strlen(response1) + strlen(response2) + file_size + 10];

        strcpy(response, response1);
        strcat(response, response2);
        strcat(response, "\r\n\r\n");
        strcat(response, content);

        send(client_socket, response, strlen(response), 0);
    }
    else if (!(strcmp(extension, ".txt")))
    {
        // Sets content-type to text/plain.
        // Concatenates different parts of the response.

        char response1[] = "HTTP/1.0 200 OK\r\n"
                           "Content-Type: text/plain; charset=UTF-8\r\n"
                           "Content-Length: ";

        char response[strlen(response1) + strlen(response2) + file_size + 10];

        strcpy(response, response1);
        strcat(response, response2);
        strcat(response, "\r\n\r\n");
        strcat(response, content);

        send(client_socket, response, strlen(response), 0);
    }
    else if (!(strcmp(extension, ".jpg")))
    {
        // Sets content-type to image/jpeg.
        // Concatenates different parts of the response.

        char response1[] = "HTTP/1.0 200 OK\r\n"
                           "Content-Type: image/jpeg; charset=UTF-8\r\n"
                           "Content-Length: ";

        send(client_socket, response1, strlen(response1), 0);
        send(client_socket, response2, strlen(response2), 0);
        send(client_socket, "\r\n\r\n", 4, 0);
        send(client_socket, content, file_size, 0);
    }
    else
    {
        // Sets content-type to application/octet-stream.
        // Concatenates different parts of the response.

        char response1[] = "HTTP/1.0 200 OK\r\n"
                           "Content-Type: application/octet-stream; charset=UTF-8\r\n"
                           "Content-Length: ";

        send(client_socket, response1, strlen(response1), 0);
        send(client_socket, response2, strlen(response2), 0);
        send(client_socket, "\r\n\r\n", 4, 0);
        send(client_socket, content, file_size, 0);
    }
}

void proxy_remote_file(struct server_app *app, int client_socket, const char *request)
{
    printf("Proxying to remote video server.\n");

    // TODO: Implement proxy request and replace the following code
    // What's needed:
    // * Connect to remote server (app->remote_server/app->remote_port)
    // * Forward the original request to the remote server
    // * Pass the response from remote server back
    // Bonus:
    // * When connection to the remote server fail, properly generate
    // HTTP 502 "Bad Gateway" response

    // Initialize a new socket, where we're now acting as the client for the
    // backend video server.
    sockfd_t server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        perror("socket failed");
        goto cleanup;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = inet_addr(app->remote_host);
    server_address.sin_port = htons(app->remote_port);

    // Connect new server_socket to the video server's socket. If it fails, we
    // send HTTP 502 Bad Gateway back to the original client.
    if (connect(server_socket,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) != 0)
    {
        fprintf(stderr, "connect() to video server failed\n");
        send_bad_gateway(client_socket);
        goto cleanup;
    }

    // Forward request to video server.
    if (send(server_socket, request, strlen(request), 0) == -1)
    {
        fprintf(stderr, "send() to video server failed\n");
        send_bad_gateway(client_socket);
        goto cleanup;
    }

    // Receive response from video server.
    char response[65536];
    while (1)
    {
        ssize_t bytes_received = recv(server_socket,
                                      response,
                                      sizeof(response),
                                      0);
        if (bytes_received == -1)
        {
            fprintf(stderr, "recv() from video server failed\n");
            send_bad_gateway(client_socket);
            goto cleanup;
        }
        if (bytes_received == 0)
            break;

        // Forward response to original client.
        if (send(client_socket, response, bytes_received, 0) == -1)
        {
            perror("send failed");
            goto cleanup;
        }
    }

cleanup:
    close(server_socket);
}

static void send_bad_gateway(sockfd_t sockfd)
{
    char response[] = "HTTP/1.0 502 Bad Gateway\r\n\r\n";
    send(sockfd, response, sizeof(response), 0);
}
