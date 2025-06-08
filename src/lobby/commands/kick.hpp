//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013-2015 SuperTuxKart-Team
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

#ifndef LOBBY_COMMAND_KICK_HPP
#define LOBBY_COMMAND_KICK_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

class KickCommand : public STKCommand
{
    // Minimum permission level in order to skip voting,
    // if the command is even votable in the first place
    ServerPermissionLevel m_min_veto = PERM_MODERATOR;
public:
    // The argument for the parent STKCommand constructor
    // is if the command can be voted at all
    KickCommand() : STKCommand(true)
    {
        m_name = "kick";
        m_args = {{nnwcli::CT_STRING, "player", "Name of the player"}};
        m_optargs = {{nnwcli::CT_FULL, "reason", "Reason for the kick (has no effect)"}};
        m_description = "Forcefully disconnect the player from the server.";
    }

    // Commands can be implemented in the source file, and it doesn't have to be the separate source
    // file if the command is not that big
    //
    // Implemented in sample_command.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_KICK_HPP
