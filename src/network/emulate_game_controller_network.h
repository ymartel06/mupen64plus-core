#ifndef M64P_NETWORK_EMULATE_GAME_CONTROLLER_H
#define M64P_NETWORK_EMULATE_GAME_CONTROLLER_H

#include <stdint.h>

enum pak_type;

int egcn_is_connected(void* opaque, enum pak_type* pak);

uint32_t egcn_get_input(void* opaque);

#endif /* guard */
