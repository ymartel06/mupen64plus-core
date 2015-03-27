#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "main/main.h"
#include "main/version.h"
#include "api/callbacks.h"
#include "bitstream.h"
#include "main/rom.h"
#include "network.h"

SOCKET server_socket;
SOCKET client_socket;
network_message next_incoming_message;

int simulated_tick_time;

//both
BIT_STREAM *send_stream;
BIT_STREAM *receive_stream;
uint16_t time_delay;
uint8_t frame_delay;
uint32_t start;
uint32_t local_inputs[INPUTS_FRAME];

//for the server
int client_is_ready = -1;
uint32_t clock_diff;


//for the client
int server_current_frame = 0;
int server_game_start = 0;


void init_network()
{
    send_stream = BitStream_new();
    receive_stream = BitStream_new();
    remote_input = 0;
    simulated_tick_time = 1000 / ROM_PARAMS.vilimit;

#ifdef WIN32
    WSADATA wsa;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (err < 0)
    {
        DebugMessage(M64MSG_ERROR, "Network internal error: WSAStartup failed.");
        exit(EXIT_FAILURE);
    }
#endif
}

void cleanup_network()
{
#ifdef WIN32
    WSACleanup();
#endif
    BitStream_free(send_stream);
    BitStream_free(receive_stream);

    if (server_socket > 0)
    {
        end_connection(server_socket);
    }

    if (client_socket > 0)
    {
        end_connection(client_socket);
    }
}

int launch_server(int port)
{
    int success = 0;
    init_network();
    current_network_mode = IS_SERVER;

    int server_port = get_server_port(port);

    server_socket = init_connection_server(server_port);
    if (server_socket != SOCKET_ERROR)
    {
        int client_is_connected = 0;
        DebugMessage(M64MSG_INFO, "Network: Server is waiting for his spectator on port %d.", server_port);

        /* Main loop */
        while (client_is_connected == 0) {
            socklen_t socksize = sizeof(struct sockaddr_in);
            struct sockaddr_in their_addr;
            char str[INET6_ADDRSTRLEN];

            memset(&their_addr, 0, sizeof(struct sockaddr));
            client_socket = accept(server_socket, (struct sockaddr*)&their_addr, &socksize);
            if (client_socket != INVALID_SOCKET) {
                client_is_connected = 1;
                int socket_status = 1;

                get_ip_str((struct sockaddr*)&their_addr, str, INET6_ADDRSTRLEN);

                DebugMessage(M64MSG_INFO, "Network: Got a connection from %s on port %d\n",
                    str, ntohs(their_addr.sin_port));

                send_welcome(client_socket);

                while (client_is_ready == -1)
                {
                    sleepcp(simulated_tick_time); //wait to simulate a frame rate
                    read_incoming_message(client_socket);
                }

                if (client_is_ready == 0)
                {
                    //wait for another client				
                    end_connection(client_socket);
                    client_is_connected = 0;
                    client_socket = INVALID_SOCKET;
                }
                else
                {
                    send_game_start(client_socket);

                    sync_game_start();

                    success = 1;
                }
            }
        }
    }

    return success;
}

int get_server_port(int port)
{
    if (port < 1)
    {
        return PORT;
    }
    else
    {
        return port;
    }
}

int launch_client(const char *address, int port)
{
    init_network();
    current_network_mode = IS_CLIENT;

    int server_port = get_server_port(port);

    next_incoming_message = WELCOME;

    client_socket = init_connection_client(address, server_port);
    if (client_socket != SOCKET_ERROR)
    {
        while (server_game_start == 0)
        {
            sleepcp(simulated_tick_time); //wait to simulate a frame rate
            read_incoming_message(client_socket);
        }

        sync_game_start();

    }

    return server_game_start;
}

int init_connection_client(const char *address, int port)
{
    SOCKET sock;
    struct sockaddr_in client_service;

    int iResult;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        DebugMessage(M64MSG_ERROR, "Network internal error: socket creation failed.");
        return SOCKET_ERROR;
    }

    // The sockaddr_in structure specifies the address family,
    // IP address, and port of the server to be connected to.
    client_service.sin_family = AF_INET;
    inet_pton(AF_INET, address, &(client_service.sin_addr));
    client_service.sin_port = htons(port);

    // Connect to server.
    iResult = connect(sock, (SOCKADDR *)& client_service, sizeof(client_service));
    if (iResult == SOCKET_ERROR) {
        DebugMessage(M64MSG_ERROR, "Network error: Impossible to connect.");
        iResult = closesocket(sock);
        if (iResult == SOCKET_ERROR)
            DebugMessage(M64MSG_ERROR, "Network error: closesocket failed.");

        return iResult;
    }

    //not blocking socket
    set_blocking(sock, 0);

    DebugMessage(M64MSG_INFO, "Network: Connected to the server.");

    return sock;
}

int init_connection_server(int port)
{
    struct sockaddr_in serv; /* socket info about our server */
    SOCKET sock = SOCKET_ERROR;
    struct addrinfo hints, *res = NULL;
    int reuseaddr = 1;

    // Get the address info 
    memset(&hints, 0, sizeof(hints));

    serv.sin_family = AF_INET;                /* set the type of connection to TCP/IP */
    serv.sin_addr.s_addr = htonl(INADDR_ANY); /* set our address to any interface */
    serv.sin_port = htons(port);           /* set the server port number */

    sock = socket(AF_INET, SOCK_STREAM, 0);

    // Enable the socket to reuse the address 
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuseaddr, sizeof(int)) == SOCKET_ERROR) {
        DebugMessage(M64MSG_ERROR, "Network internal error: socket can't reuse the address.");
        sock = SOCKET_ERROR;
    }

    // bind serv information to the socket 
    if (bind(sock, (struct sockaddr *)&serv, sizeof(struct sockaddr)) == SOCKET_ERROR) {
        DebugMessage(M64MSG_ERROR, "Network internal error: bind failed.");
        return SOCKET_ERROR;
    }

    /* start listening, allowing a queue of up to 1 pending connection */
    if (listen(sock, 10) == SOCKET_ERROR) {
        DebugMessage(M64MSG_ERROR, "Network internal error: listen failed.");
        return SOCKET_ERROR;
    }

    //not blocking socket
    set_blocking(sock, 0);

    return sock;
}

void sync_game_start()
{
    uint32_t now = clock();
    while (now < start)
    {
        sleepcp(simulated_tick_time);
        now = clock();
    }
}

void end_connection(int sock)
{
    closesocket(sock);
}

int read_socket(SOCKET sock, BIT_STREAM *bstream)
{
    return recv(sock, bstream->data, DEFAULT_BUFSIZE, 0);
}

int write_socket(SOCKET sock, BIT_STREAM *bstream)
{
    int res = send(sock, bstream->data, bstream->length, 0);
    if (res < 0)
    {
        DebugMessage(M64MSG_ERROR, "Network internal error: Send failed.");
        exit(errno);
    }
    return res;
}

int set_blocking(int fd, int blocking)
{
    if (fd < 0) return 0;
#ifdef WIN32
    unsigned long mode = blocking ? 0 : 1;
    return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? 1 : 0;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
#endif
}

char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen)
{
    switch (sa->sa_family) {
    case AF_INET:
        inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr),
            s, maxlen);
        break;

    case AF_INET6:
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)sa)->sin6_addr),
            s, maxlen);
        break;
    }
    return s;
}

void send_remote_input(uint32_t input)
{
    BitStream_reset(send_stream);
    BitStream_write_uint16(send_stream, NET_INPUT);
    BitStream_write_uint32(send_stream, main_get_current_frame());
    BitStream_write_uint32(send_stream, input);
    write_socket(client_socket, send_stream);
}

void read_network_input(BIT_STREAM *bstream)
{
    server_current_frame = BitStream_read_uint32(bstream);
    remote_input = BitStream_read_uint32(bstream);
}

int send_welcome(SOCKET sock)
{
    next_incoming_message = WELCOME_BACK;
    BitStream_reset(send_stream);
    // send welcome and the current server time
    // we will use it to estimate the ping but also to have difference between the client clock and the server clock
    BitStream_write_uint16(send_stream, WELCOME);
    BitStream_write_uint32(send_stream, clock());
    return write_socket(sock, send_stream);
}

void read_welcome(BIT_STREAM *bstream, SOCKET sock)
{
    uint32_t remote_clock = BitStream_read_uint32(bstream);
    send_welcome_back(sock, remote_clock);
}

int send_welcome_back(SOCKET sock, uint32_t remote_clock)
{
    next_incoming_message = GAME_START;
    BitStream_reset(send_stream);
    BitStream_write_uint16(send_stream, WELCOME_BACK);
    BitStream_write_uint32(send_stream, remote_clock);	//remote clock for the ping
    BitStream_write_uint32(send_stream, clock());		//current clock to estimate the difference between clocks
    BitStream_write_uint32(send_stream, MUPEN_CORE_VERSION); //check if the core version is the same
    BitStream_write_char_array(send_stream, ROM_SETTINGS.MD5, 32);
    return write_socket(sock, send_stream);
}

void read_welcome_back(BIT_STREAM *bstream, SOCKET sock)
{
    next_incoming_message = NET_INPUT;
    uint32_t local_clock = BitStream_read_uint32(bstream);
    uint32_t remote_clock = BitStream_read_uint32(bstream);
    time_delay = one_way_ping(clock(), local_clock);
    frame_delay = time_delay / simulated_tick_time;

    DebugMessage(M64MSG_INFO, "Network: Client delay %d ms, %d frame(s)", time_delay, frame_delay);

    clock_diff = remote_clock - local_clock - time_delay;
    uint32_t remote_core_version = BitStream_read_uint32(bstream);
    char *remote_rom_md5 = BitStream_read_char_array(bstream, 32);

    if (remote_core_version != MUPEN_CORE_VERSION)
    {
        DebugMessage(M64MSG_VERBOSE, "Network: Remote Core version is not the same (%d)", remote_core_version);
        send_disconnection(sock, WRONG_CORE_VERSION);
        client_is_ready = 0;
        return;
    }

    if (memcmp(remote_rom_md5, ROM_SETTINGS.MD5, 32) != 0)
    {
        DebugMessage(M64MSG_VERBOSE, "Network: ROM MD5 check failed");
        send_disconnection(sock, WRONG_ROM);
        client_is_ready = 0;
        return;
    }

    free(remote_rom_md5);

    client_is_ready = 1;
}

int send_disconnection(SOCKET sock, disconnection_reason reason)
{
    BitStream_reset(send_stream);
    BitStream_write_uint16(send_stream, DISCONNECT);
    BitStream_write_uint16(send_stream, reason);
    return write_socket(sock, send_stream);
}

void read_disconnection(BIT_STREAM *bstream)
{
    uint16_t reason = BitStream_read_uint16(bstream);
    switch (reason)
    {
    case WRONG_CORE_VERSION:
        DebugMessage(M64MSG_ERROR, "Network: You must use the same version of mupen64 core.");
        break;
    case WRONG_ROM:
        DebugMessage(M64MSG_ERROR, "Network: You must have the same rom as the server.");
        break;
    }
    exit(1);
}

int send_game_start(SOCKET sock)
{
    next_incoming_message = NET_INPUT;
    BitStream_reset(send_stream);
    BitStream_write_uint16(send_stream, GAME_START);
    start = clock() + (3000 * CLOCKS_PER_SEC / 1000);
    uint32_t client_start = clock() + (3000 * CLOCKS_PER_SEC / 1000) + clock_diff; //boot in 3s 
    BitStream_write_uint32(send_stream, client_start);
    BitStream_write_uint16(send_stream, time_delay);
    return write_socket(sock, send_stream);
}

void read_network_game_start(BIT_STREAM *bstream)
{
    start = BitStream_read_uint32(bstream);
    time_delay = BitStream_read_uint16(bstream);
    frame_delay = time_delay / simulated_tick_time;
    DebugMessage(M64MSG_INFO, "Network: Client delay %d ms, %d frame(s)", time_delay, frame_delay);

    next_incoming_message = NET_INPUT;
    server_game_start = 1;
}

void read_incoming_message(SOCKET sock)
{
    BitStream_reset(receive_stream);
    int res = read_socket(sock, receive_stream);
    if (res > 0)
    {
        receive_stream->datasize = receive_stream->length = res;
        uint16_t message_type = BitStream_read_uint16(receive_stream);
        if (message_type == next_incoming_message)
        {
            switch (message_type)
            {
            case WELCOME:
                read_welcome(receive_stream, sock);
                break;
            case WELCOME_BACK:
                read_welcome_back(receive_stream, sock);
                break;
            case NET_INPUT:
                read_network_input(receive_stream);
                break;
            case GAME_START:
                read_network_game_start(receive_stream);
                break;
            }
        }
        else
        {
            if (message_type == DISCONNECT)
            {
                read_disconnection(receive_stream);
            }
            else
            {
                //not the good message, close everything
                DebugMessage(M64MSG_ERROR, "Network: Not the expected message has been received. Actual:%d Expected:%d.", message_type, next_incoming_message);
                exit(errno);
            }

        }
    }
}

void read_client_socket()
{
    read_incoming_message(client_socket);
}

int one_way_ping(uint32_t end_time, uint32_t begin_time)
{
    uint32_t res = end_time - begin_time;
    //to have result in ms, we have to multiply by 1000 but we want a one way ping, so the half.
    return (res * (CLOCKS_PER_SEC / 2) / CLOCKS_PER_SEC);
}


void sleepcp(int milliseconds) // cross-platform sleep function
{
#ifdef WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

void set_local_input(uint32_t input)
{
    int index = ((main_get_current_frame() + frame_delay)  % INPUTS_FRAME);
    local_inputs[index] = input;
}

uint32_t get_local_input()
{
    return local_inputs[(main_get_current_frame() % INPUTS_FRAME)];
}