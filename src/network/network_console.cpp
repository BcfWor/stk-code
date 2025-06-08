//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2015 Joerg Henrichs
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/vs.hpp"
#include "main_loop.hpp"

#include <iostream>
#include <string>

#ifndef WIN32
#  include <stdint.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

namespace NetworkConsole
{

    std::shared_ptr<STKCommandContext> network_console_context;

#ifndef WIN32
std::string g_cmd_buffer;
#endif
// ----------------------------------------------------------------------------
void showHelp()
{
    std::cout << "Available command:" << std::endl;
    std::cout << "help, Print this." << std::endl;
    std::cout << "quit, Shut down the server." << std::endl;
    std::cout << "kickall, Kick all players out of STKHost." << std::endl;
    std::cout << "kick #, kick # peer of STKHost." << std::endl;
    std::cout << "kickban #, kick and ban # peer of STKHost." << std::endl;
    std::cout << "unban #, unban # peer of STKHost." << std::endl;
    std::cout << "listpeers, List all peers with host ID and IP." << std::endl;
    std::cout << "listban, List IP ban list of server." << std::endl;
    std::cout << "speedstats, Show upload and download speed." << std::endl;
    std::cout << "setplayer name, Set permission to player." << std::endl;
    std::cout << "setmoderator name, Set permission to moderator." 
        << std::endl;
    std::cout << "setadministrator name, Set permission to administrator." 
        << std::endl;
}   // showHelp

// ----------------------------------------------------------------------------
#ifndef WIN32
bool pollCommand()
{
    struct timeval timeout;
    fd_set rfds;
    int fd;
    char c;

    // stdin file descriptor is 0
    fd = 0;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    if (select(fd + 1, &rfds, NULL, NULL, &timeout) <= 0)
        return false;
    if (read(fd, &c, 1) != 1)
        return false;

    if (c == '\n')
        return true;
    g_cmd_buffer += c;
    return false;
}   // pollCommand
#endif

// ----------------------------------------------------------------------------
void mainLoop(STKHost* host)
{

    network_console_context = std::make_shared<STKCommandContext>();
    network_console_context->set_peer(nullptr);

    VS::setThreadName("NetworkConsole");

#ifndef WIN32
    g_cmd_buffer.clear();
#endif

    std::string str = "";
    while (!host->requestedShutdown())
    {
#ifndef WIN32
        if (!pollCommand())
            continue;
        if (g_cmd_buffer.empty())
            continue;

        ServerLobbyCommands::get()->handleNetworkConsoleCommand(g_cmd_buffer);
        g_cmd_buffer.clear();
        continue;

        std::stringstream ss(g_cmd_buffer);
        if (g_cmd_buffer.empty())
            continue;
        g_cmd_buffer.clear();
#else
        getline(std::cin, str);

        continue;
        std::stringstream ss(str);
#endif
    }   // while !stop
    main_loop->requestAbort();
}   // mainLoop

}
