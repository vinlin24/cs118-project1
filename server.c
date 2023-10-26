/**
 * @file
 * @brief UCLA F23 COM SCI 118 Project 1: Web Server Implementation.
 *
 * @authors Snigdha Kansal
 *          Vincent Lin
 */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * Buffer size for an HTTP request.
 */
#define REQUEST_BUFFER_SIZE 1024
/**
 * Buffer size for an HTTP response.
 */
#define RESPONSE_BUFFER_SIZE 65536

/**
 * Default port to run this web server on.
 */
#define DEFAULT_SERVER_PORT 8081
/**
 * Hostname of the remote video server. This is provided in the handout and
 * should not be changed.
 */
#define DEFAULT_REMOTE_HOST "131.179.176.34"
/**
 * Port of the remote video server. This is provided in the handout and should
 * not be changed as the server's firewall is set to only allow traffic through
 * this port. Repeated attempts outside of this port may cause an IP ban.
 */
#define DEFAULT_REMOTE_PORT 5001

/**
 * Readable alias for passing in 0 as an argument representing bit flags.
 */
#define NO_FLAGS 0
/**
 * Readable alias for the automatic protocol argument to socket().
 */
#define AUTO_PROTOCOL 0
/**
 * Readable alias for using strcmp to check C-string equality.
 */
#define STRING_EQUALS(s1, s2) (strcmp((s1), (s2)) == 0)

/**
 * Represents a server application.
 */
struct server_app
{
    /**
     * Local port of HTTP server.
     */
    uint16_t server_port;
    /**
     * Remote host of remote proxy.
     */
    const char *remote_host;
    /**
     * Remote port of remote proxy.
     */
    uint16_t remote_port;
};

/**
 * Readable alias for an integer representing a socket file descriptor.
 */
typedef int sockfd_t;

/**
 * Parse command line arguments to initialize the server application.
 */
static void parse_args(int argc, char *argv[], struct server_app *app);

/**
 * Return a pointer to the start of the file path's file extension, if exists.
 * This file extension is assumed to start at the last occurrence of '.'. If
 * there is no '.', the NULL pointer is returned.
 */
static inline const char *extract_file_extension(const char *path);

/**
 * Convert %20 and %25 from a URL-encoded filename to the space and % characters
 * respectively, in-place.
 */
static void decode_url_file_name(char *file_name);

/**
 * Handle an incoming HTTP request from a client application.
 */
static void handle_request(struct server_app *app, sockfd_t client_socket);

/**
 * Send a file from the local filesystem via an HTTP response to the client.
 */
static void serve_local_file(sockfd_t client_socket, const char *path);

/**
 * Forward an HTTP request from the client to the remote video server, also
 * forwarding its HTTP response back to the client.
 */
static void proxy_remote_file(struct server_app *app,
                              sockfd_t client_socket,
                              const char *path);

/**
 * Send an HTTP 502 Bad Gateway response to a socket.
 */
static void send_bad_gateway(sockfd_t sockfd);

/**
 * Main driver function.
 */
int main(int argc, char *argv[])
{
    struct server_app app;
    parse_args(argc, argv, &app);

    sockfd_t server_socket = socket(AF_INET, SOCK_STREAM, AUTO_PROTOCOL);
    if (server_socket == -1)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(app.server_port);

    // The following allows the program to immediately bind to the port in case
    // previous run exits recently.
    int optval = 1;
    setsockopt(server_socket,
               SOL_SOCKET,
               SO_REUSEADDR,
               &optval,
               sizeof(optval));

    if (bind(server_socket,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1)
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

    while (true)
    {
        socklen_t client_len;
        struct sockaddr_in client_addr;
        sockfd_t client_socket = accept(server_socket,
                                        (struct sockaddr *)&client_addr,
                                        &client_len);

        if (client_socket == -1)
        {
            perror("accept failed");
            continue;
        }

        printf("Accepted connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        handle_request(&app, client_socket);
        close(client_socket);
    }

    close(server_socket);
    return 0;
}

static void parse_args(int argc, char *argv[], struct server_app *app)
{
    app->server_port = DEFAULT_SERVER_PORT;
    app->remote_host = NULL;
    app->remote_port = DEFAULT_REMOTE_PORT;

    const char usage[] = "Usage: server "
                         "[-b local_port] "
                         "[-r remote_host] "
                         "[-p remote_port]";
    int opt;
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
        // Unrecognized parameter or "-?".
        default:
            fprintf(stderr, "%s\n", usage);
            exit(-1);
            break;
        }
    }

    if (app->remote_host == NULL)
    {
        app->remote_host = strdup(DEFAULT_REMOTE_HOST);
    }
}

static inline const char *extract_file_extension(const char *path)
{
    return strrchr(path, '.');
}

static void decode_url_file_name(char *file_name)
{
    size_t length = strlen(file_name);
    for (size_t i = 0; i + 2 < length; i++)
    {
        if (file_name[i] == '%' && file_name[i + 1] == '2')
        {
            if (file_name[i + 2] == '0')
            {
                file_name[i] = ' ';
                memmove(&file_name[i + 1],
                        &file_name[i + 3],
                        strlen(&file_name[i + 3]) + 1);
            }
            else if (file_name[i + 2] == '5')
            {
                file_name[i] = '%';
                memmove(&file_name[i + 1],
                        &file_name[i + 3],
                        strlen(&file_name[i + 3]) + 1);
            }
        }
    }
}

static void handle_request(struct server_app *app, sockfd_t client_socket)
{
    // Read the request from HTTP client.
    //
    // NOTE: This code is not ideal in the real world because it assumes that
    // the request header is small enough and can be read once as a whole.
    // However, the current version suffices for our testing.

    char buffer[REQUEST_BUFFER_SIZE];
    ssize_t bytes_read = recv(client_socket,
                              buffer,
                              sizeof(buffer) - 1,
                              NO_FLAGS);
    // Connection closed or error.
    if (bytes_read <= 0)
        return;

    // Copy buffer to a new string.
    buffer[bytes_read] = '\0';
    char *request = malloc(strlen(buffer) + 1);
    strcpy(request, buffer);

    // Parse the header and extract essential fields, e.g. file name. If the
    // requested path is "/" (root), default to index.html.

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

    // If the requested path is "/" (root), defaults to index.html.
    if (strlen(file_name) == 0)
        strcpy(file_name, "index.html");
    else
        decode_url_file_name(file_name);

    const char *extension = extract_file_extension(file_name);
    if (extension && STRING_EQUALS(extension, ".ts"))
        proxy_remote_file(app, client_socket, request);
    else
        serve_local_file(client_socket, file_name);
}

static void serve_local_file(sockfd_t client_socket, const char *path)
{
    printf("Serving %s from local filesystem.\n", path);

    // Serving of local files:
    //  * Open the requested file.
    //  * Build proper response headers (see details in the spec), and send
    //    them.
    //  * Also send file content
    //
    // When the requested file does not exist:
    //  * Generate a correct response.

    // Tries to open requested file at path.

    FILE *file = fopen(path, "r");

    // If file does not exist, responds with a message saying "Requested file
    // does not exist.".

    if (file == NULL)
    {
        char response[] = "HTTP/1.0 404 Not Found\r\n"
                          "Content-Type: text/plain; charset=UTF-8\r\n"
                          "Content-Length: 30\r\n"
                          "\r\n"
                          "Requested file does not exist."
                          "\r\n\r\n";
        send(client_socket, response, strlen(response), NO_FLAGS);
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

    // Converts file_size from size_t to string to append to HTTP response
    // message.

    char response2[sizeof(size_t)];
    sprintf(response2, "%ld", file_size);

    const char *extension = extract_file_extension(path);

    // Following checks respond accordingly based on extension of file which
    // include cases of: no extension, .html extension, .txt extension, and .jpg
    // extension.

    if (!extension)
    {
        // Sets content-type to application/octet-stream. Concatenates different
        // parts of the response.

        char response1[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: application/octet-stream; charset=UTF-8\r\n"
            "Content-Length: ";

        send(client_socket, response1, strlen(response1), NO_FLAGS);
        send(client_socket, response2, strlen(response2), NO_FLAGS);
        send(client_socket, "\r\n\r\n", 4, NO_FLAGS);
        send(client_socket, content, file_size, NO_FLAGS);
    }
    else if (STRING_EQUALS(extension, ".html"))
    {
        // Sets content-type to text/html. Concatenates different parts of the
        // response.

        char response1[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Content-Length: ";

        char response[strlen(response1) + strlen(response2) + file_size + 10];

        strcpy(response, response1);
        strcat(response, response2);
        strcat(response, "\r\n\r\n");
        strcat(response, content);

        send(client_socket, response, strlen(response), NO_FLAGS);
    }
    else if (STRING_EQUALS(extension, ".txt"))
    {
        // Sets content-type to text/plain. Concatenates different parts of the
        // response.

        char response1[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n"
            "Content-Length: ";

        char response[strlen(response1) + strlen(response2) + file_size + 10];

        strcpy(response, response1);
        strcat(response, response2);
        strcat(response, "\r\n\r\n");
        strcat(response, content);

        send(client_socket, response, strlen(response), NO_FLAGS);
    }
    else if (STRING_EQUALS(extension, ".jpg"))
    {
        // Sets content-type to image/jpeg. Concatenates different parts of the
        // response.

        char response1[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: image/jpeg; charset=UTF-8\r\n"
            "Content-Length: ";

        send(client_socket, response1, strlen(response1), NO_FLAGS);
        send(client_socket, response2, strlen(response2), NO_FLAGS);
        send(client_socket, "\r\n\r\n", 4, NO_FLAGS);
        send(client_socket, content, file_size, NO_FLAGS);
    }
    else
    {
        // Sets content-type to application/octet-stream. Concatenates different
        // parts of the response.

        char response1[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: application/octet-stream; charset=UTF-8\r\n"
            "Content-Length: ";

        send(client_socket, response1, strlen(response1), NO_FLAGS);
        send(client_socket, response2, strlen(response2), NO_FLAGS);
        send(client_socket, "\r\n\r\n", 4, NO_FLAGS);
        send(client_socket, content, file_size, NO_FLAGS);
    }
}

static void proxy_remote_file(struct server_app *app,
                              sockfd_t client_socket,
                              const char *request)
{
    printf("Proxying to remote video server.\n");

    // Proxying a request:
    //  * Connect to remote server (app->remote_host/app->remote_port).
    //  * Forward the original request to the remote server.
    //  * Pass the response from remote server back.
    //
    // Bonus:
    //  * When connection to the remote server fail, properly generate HTTP 502
    //    "Bad Gateway" response.

    /**
     * A new socket, where we're now acting as the client for the video server.
     */
    sockfd_t server_socket = socket(AF_INET, SOCK_STREAM, AUTO_PROTOCOL);
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
    if (send(server_socket, request, strlen(request), NO_FLAGS) == -1)
    {
        fprintf(stderr, "send() to video server failed\n");
        send_bad_gateway(client_socket);
        goto cleanup;
    }

    char response[RESPONSE_BUFFER_SIZE];
    while (true)
    {
        ssize_t bytes_received = recv(server_socket,
                                      response,
                                      sizeof(response),
                                      NO_FLAGS);
        if (bytes_received == -1)
        {
            fprintf(stderr, "recv() from video server failed\n");
            send_bad_gateway(client_socket);
            goto cleanup;
        }
        if (bytes_received == 0)
            break;

        // Forward response to original client.
        if (send(client_socket, response, bytes_received, NO_FLAGS) == -1)
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
    if (send(sockfd, response, sizeof(response), NO_FLAGS) == -1)
        perror("send in send_bad_gateway failed");
}
