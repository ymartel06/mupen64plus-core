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
int first_local_player_index = 0;
int last_local_player_index = 0;

int frame_sync = 0;
uint32_t remote_current_frame = 0; // store the frame number for the last remote input received

//for the server
int client_is_ready = -1;
uint32_t clock_diff;
uint16_t remote_clock_per_second;

//for the client
int server_game_start = 0;


void init_network()
{
    send_stream = BitStream_new();
    receive_stream = BitStream_new();
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

void init_local_player(int local_player_nb)
{
    if (local_player_nb < 1)
    {
        local_player_nb = 1;

    }
    else if (local_player_nb > 3)
    {
        local_player_nb = 3;
    }

    local_player_number = local_player_nb;
}

void init_player_inputs()
{
    int total_players = local_player_number + remote_player_number;
    DebugMessage(M64MSG_INFO, "Network: Players count is %d", total_players);
    
    if (current_network_mode == IS_SERVER)
    {
        first_local_player_index = 0;
        last_local_player_index = local_player_number - 1;
        for (int i = 0; i < local_player_number; i++)
        {
            network_players[i].player_input_mode = IS_LOCAL;
            network_players[i].player_local_channel = i;
            DebugMessage(M64MSG_INFO, "Network: Player %d is local", i);
        }
        for (int i = local_player_number; i < total_players; i++)
        {
            network_players[i].player_input_mode = IS_REMOTE;
            DebugMessage(M64MSG_INFO, "Network: Player %d is remote", i);
        }
    }
    else if (current_network_mode == IS_CLIENT)
    {
        first_local_player_index = remote_player_number;
        last_local_player_index = total_players - 1;
        for (int i = 0; i < remote_player_number; i++)
        {
            network_players[i].player_input_mode = IS_REMOTE;
            DebugMessage(M64MSG_INFO, "Network: Player %d is remote", i);
        }
        for (int i = remote_player_number; i < total_players; i++)
        {
            network_players[i].player_input_mode = IS_LOCAL;
            network_players[i].player_local_channel = i - remote_player_number;
            DebugMessage(M64MSG_INFO, "Network: Player %d is local", i);
        }
    }
}

int launch_server(int port, int local_player_nb)
{
    int success = 0;
    init_network();
    current_network_mode = IS_SERVER;
    init_local_player(local_player_nb);

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

int launch_client(const char *address, int port, int local_player_nb)
{
    init_network();
    current_network_mode = IS_CLIENT;
    init_local_player(local_player_nb);
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

void send_remote_input(uint32_t input, uint16_t channel)
{
    //write the header of the packet only at the first local player
    if (first_local_player_index == channel)
    {
        BitStream_reset(send_stream);
        BitStream_write_uint16(send_stream, NET_INPUT);
        BitStream_write_uint32(send_stream, main_get_current_frame());
    }

    //content
    BitStream_write_uint16(send_stream, channel);
    BitStream_write_uint32(send_stream, input);

    //send the packet only when the last local player has put his input
    if (last_local_player_index == channel)
        write_socket(client_socket, send_stream);
}

void read_network_input(BIT_STREAM *bstream)
{
    remote_current_frame = BitStream_read_uint32(bstream);
    for (int i = 0; i < remote_player_number; i++)
    {
        uint16_t channel = BitStream_read_uint16(bstream);
        set_remote_input(BitStream_read_uint32(bstream), channel);
    }
}

int send_welcome(SOCKET sock)
{
    next_incoming_message = WELCOME_BACK;
    BitStream_reset(send_stream);
    // send welcome and the current server time
    // we will use it to estimate the ping but also to have difference between the client clock and the server clock
    BitStream_write_uint16(send_stream, WELCOME);
    BitStream_write_uint32(send_stream, clock());
    // add the number of player in local
    BitStream_write_uint16(send_stream, local_player_number);
    return write_socket(sock, send_stream);
}

void read_welcome(BIT_STREAM *bstream, SOCKET sock)
{
    uint32_t remote_clock = BitStream_read_uint32(bstream);
    remote_player_number = BitStream_read_uint16(bstream);
    send_welcome_back(sock, remote_clock);
}

int send_welcome_back(SOCKET sock, uint32_t remote_clock)
{
    next_incoming_message = GAME_START;
    BitStream_reset(send_stream);
    BitStream_write_uint16(send_stream, WELCOME_BACK);
    BitStream_write_uint16(send_stream, local_player_number);	//number of player in local
    BitStream_write_uint32(send_stream, remote_clock);	//remote clock for the ping
    BitStream_write_uint32(send_stream, clock());		//current clock to estimate the difference between clocks
    BitStream_write_uint16(send_stream, CLOCKS_PER_SEC);		//send the clocks per sec to 
    BitStream_write_uint32(send_stream, MUPEN_CORE_VERSION); //check if the core version is the same
    BitStream_write_char_array(send_stream, ROM_SETTINGS.MD5, 32);
    return write_socket(sock, send_stream);
}

void read_welcome_back(BIT_STREAM *bstream, SOCKET sock)
{
    next_incoming_message = NET_INPUT;
    remote_player_number = BitStream_read_uint16(bstream);
    uint32_t local_clock = BitStream_read_uint32(bstream);
    uint32_t remote_clock = BitStream_read_uint32(bstream);
    remote_clock_per_second = BitStream_read_uint16(bstream);
    time_delay = one_way_ping(clock(), local_clock);
    frame_delay = time_delay / simulated_tick_time;

    if (frame_delay < 1) // minimum frame delay between players is 1 frame
        frame_delay = 1;

    DebugMessage(M64MSG_INFO, "Network: Client delay %d ms, %d frame(s)", time_delay, frame_delay);

    clock_diff = remote_clock - local_clock - time_delay;
    uint32_t remote_core_version = BitStream_read_uint32(bstream);
    char *remote_rom_md5 = BitStream_read_char_array(bstream, 32);

    if (remote_core_version != MUPEN_CORE_VERSION)
    {
        send_disconnection(sock, WRONG_CORE_VERSION);
        return;
    }

    if (memcmp(remote_rom_md5, ROM_SETTINGS.MD5, 32) != 0)
    {
        send_disconnection(sock, WRONG_ROM);
        return;
    }

    if (remote_player_number < 1 || remote_player_number >= 4)
    {
        send_disconnection(sock, WRONG_LOCAL_PLAYER);
        return;
    }

    if (local_player_number + remote_player_number > 4)
    {
        send_disconnection(sock, TOO_MUCH_PLAYER);
        return;
    }

    init_player_inputs();

    free(remote_rom_md5);

    client_is_ready = 1;
}

int send_disconnection(SOCKET sock, disconnection_reason reason)
{
    client_is_ready = 0;

    switch (reason)
    {
    case WRONG_CORE_VERSION:
        DebugMessage(M64MSG_VERBOSE, "Network: Remote Core version is not the same");
        break;
    case WRONG_ROM:
        DebugMessage(M64MSG_VERBOSE, "Network: ROM MD5 check failed");
        break;
    case WRONG_LOCAL_PLAYER:
        DebugMessage(M64MSG_VERBOSE, "Network: Issue with the remote player number");
        break;
    case TOO_MUCH_PLAYER:
        DebugMessage(M64MSG_VERBOSE, "Network: Server can't handle more than 4 players");
        break;
    }

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
    case WRONG_LOCAL_PLAYER:
        DebugMessage(M64MSG_ERROR, "Network: You must have at least one local player, up to 3");
        break;
    case TOO_MUCH_PLAYER:
        DebugMessage(M64MSG_ERROR, "Network: Server can't handle more than 4 players. Try to have less local player");
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
    float clock_ratio = (float)remote_clock_per_second / CLOCKS_PER_SEC;
    uint32_t clock_estimate_diff = (float)clock_diff * clock_ratio; //we want the result in uint32_t, no need of the precision
    uint32_t client_start = clock() + (3000 * CLOCKS_PER_SEC / 1000) + clock_estimate_diff; //boot in 3s 
    BitStream_write_uint32(send_stream, client_start);
    BitStream_write_uint16(send_stream, time_delay);
    return write_socket(sock, send_stream);
}

void read_network_game_start(BIT_STREAM *bstream)
{
    init_player_inputs();

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
    if (frame_sync > 0)
    {
        frame_sync_read_socket();
    }
    else
    {
        read_incoming_message(client_socket);
    }

}

void frame_sync_read_socket()
{
    int read_socket = 1;
    while (read_socket > 0)
    {
        read_incoming_message(client_socket);
        read_socket = main_get_current_frame() - remote_current_frame - frame_delay;
        if (read_socket > 0)
        {
            sleepcp(1); //wait 1 ms before reading again
        }
    }
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

void set_local_input(uint32_t input, uint16_t channel)
{
    int index = ((main_get_current_frame() + frame_delay)  % INPUTS_FRAME);
    network_players[channel].player_input[index] = input;
}

uint32_t get_local_input(uint16_t channel)
{
    return network_players[channel].player_input[(main_get_current_frame() % INPUTS_FRAME)];
}

void set_remote_input(uint32_t input, uint16_t channel)
{
    int index = (remote_current_frame % INPUTS_FRAME);
    network_players[channel].player_input[index] = input;
}

uint32_t get_remote_input(uint16_t channel)
{
    uint32_t input = network_players[channel].player_input[(remote_current_frame % INPUTS_FRAME)];
    network_players[channel].player_input[(remote_current_frame % INPUTS_FRAME)] = 0;
    return input;
}