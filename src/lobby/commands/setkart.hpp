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

#ifndef LOBBY_COMMAND_SETKART_HPP
#define LOBBY_COMMAND_SETKART_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

class SetKartCommand : public STKCommand
{
    ServerPermissionLevel m_required_perm = PERM_MODERATOR;
public:
    SetKartCommand() : STKCommand(false)
    {
        m_name = "setkart";
        m_args = {{nnwcli::CT_STRING, "kart_name", "Name of the standard kart, see /karts"}};
        m_optargs = {{nnwcli::CT_STRING, "player", "Name of player"}, {nnwcli::CT_BOOL, "word", "Permanent? yes or no, defaults to no"}};
        m_description = "Makes the player use specified kart for the next games.";
    }

    // Implemented in moderation_toolkit.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_SETKART_HPP
