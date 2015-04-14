#ifndef M64P_NETWORK_NETWORK_H
#define M64P_NETWORK_NETWORK_H


#if defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> 
#include <netdb.h> 
#include <fcntl.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket(s) close(s)
typedef int SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr SOCKADDR;
typedef struct in_addr IN_ADDR;

#endif

#include "api/m64p_types.h"
#include "bitstream.h"

#define PORT            6464
#define MAX_CLIENTS     1
#define INPUTS_FRAME    60

typedef struct {
    uint32_t player_input[INPUTS_FRAME];
    network_player_input_mode player_input_mode;
    uint16_t player_local_channel;
} network_player;

network_player network_players[4];
int local_player_number;
int remote_player_number;

network_mode current_network_mode;

//init methods
void init_network();
void init_local_player(int local_player_nb);
int init_connection_client(const char *address, int port);
int init_connection_server(int port);
void cleanup_network();
int launch_client(const char *address, int port, int local_player_nb);
int launch_server(int port, int local_player_nb);
int get_server_port(int port);
void end_connection(int sock);
int read_socket(SOCKET sock, BIT_STREAM *bstream);
int write_socket(SOCKET sock, BIT_STREAM *bstream);
int set_blocking(int fd, int blocking);
int send_game_start(SOCKET sock);
int send_welcome(SOCKET sock);
int send_welcome_back(SOCKET sock, uint32_t remote_clock);
int send_disconnection(SOCKET sock, disconnection_reason reason);
void read_incoming_message(SOCKET sock);
void read_network_input(BIT_STREAM *bstream);
void read_network_game_start(BIT_STREAM *bstream);
void read_welcome(BIT_STREAM *bstream, SOCKET sock);
void read_welcome_back(BIT_STREAM *bstream, SOCKET sock);
void read_disconnection(BIT_STREAM *bstream);
void reset_remote_input();
void read_client_socket();
void sync_game_start();

//helpers
void sleepcp(int milliseconds);
char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen);
int one_way_ping(uint32_t end_time, uint32_t begin_time);

//frame sync
void frame_sync_read_socket();

//inputs
void set_local_input(uint32_t input, uint16_t channel);
uint32_t get_local_input(uint16_t channel);
void set_remote_input(uint32_t input, uint16_t channel);
uint32_t get_remote_input(uint16_t channel);
void send_remote_input(uint32_t input, uint16_t channel);
#endif /* guard */
