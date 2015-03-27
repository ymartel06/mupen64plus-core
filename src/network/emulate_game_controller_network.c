#include "emulate_game_controller_network.h"

#include "plugin/plugin.h"
#include "main/main.h"
#include "network/network.h"
#include "api/m64p_plugin.h"
#include "si/game_controller.h"

//note: this function will be refactor at the real implementation for multiplayer
int egcn_is_connected(void* opaque, enum pak_type* pak)
{
    int channel = *(int*)opaque;
    if (channel < 2) //p1 and p2
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

//note: this function will be refactor at the real implementation for multiplayer
uint32_t egcn_get_input(void* opaque)
{
    BUTTONS keys = { 0 };
    uint32_t in = 0;
    uint32_t local_input = 0;
    int channel = *(int*)opaque;

    if (channel == 0)
    {
        if (input.getKeys)
            input.getKeys(channel, &keys);

        local_input = keys.Value;
        send_remote_input(local_input);
        set_local_input(local_input);
    }

    switch (current_network_mode)
    {
    case IS_SERVER:
        if (channel == 0)
        {            
            in = get_local_input();
        }
        else
        {
            read_client_socket();
            in = remote_input;
            remote_input = 0;
        }
        break;
    case IS_CLIENT:
        if (channel == 0)
        {
            read_client_socket();
            in = remote_input;
            remote_input = 0;
        }
        else
        {
            in = get_local_input();
        }
        break;

    }

    return in;

}
