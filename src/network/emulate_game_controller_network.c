#include "emulate_game_controller_network.h"

#include "plugin/plugin.h"
#include "main/main.h"
#include "network/network.h"
#include "api/m64p_plugin.h"
#include "si/game_controller.h"

int egcn_is_connected(void* opaque, enum pak_type* pak)
{
    int channel = *(int*)opaque;
    if (channel < (local_player_number + remote_player_number))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

uint32_t egcn_get_input(void* opaque)
{
    BUTTONS keys = { 0 };
    uint32_t in = 0;
    uint32_t local_input = 0;
    int channel = *(int*)opaque;

    if (network_players[channel].player_input_mode == IS_LOCAL)
    {
        //check if the controller is really connected
        CONTROL* c = &Controls[network_players[channel].player_local_channel]; //not that channel to check
        if (c->Present && input.getKeys)
            input.getKeys(network_players[channel].player_local_channel, &keys);

        local_input = keys.Value;
        send_remote_input(local_input, channel);
        set_local_input(local_input, channel);
        in = get_local_input(channel);
    }
    else if (network_players[channel].player_input_mode == IS_REMOTE)
    {
        read_client_socket(); //no need to read multiple times
        in = get_remote_input(channel);
    }

    return in;
}
