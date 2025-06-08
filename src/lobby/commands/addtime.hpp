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

#include "context.hpp"
#include "lobby/stk_command.hpp"
#include "utils/cpp2011.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"

#ifndef LOBBY_COMMAND_ADDTIME_HPP
#define LOBBY_COMMAND_ADDTIME_HPP

class AddTimeCommand : public STKCommand
{
    ServerPermissionLevel m_min_veto = PERM_REFEREE;
    // At least this level of permission to skip seconds verification (1-3600)
    ServerPermissionLevel m_perm_limitless = PERM_ADMINISTRATOR;
public:
    AddTimeCommand() : STKCommand(true) // can vote for this command
    {
        m_name = "addtime";
        m_args = {{nnwcli::CT_USHORTINT, "seconds", "seconds, between 1 and 3600"}};
        m_description = "Temporarily increase the starting timeout in the lobby.";
    }
    ~AddTimeCommand() {}

    //------------------------------------
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};
#endif // LOBBY_COMMAND_ADDTIME_HPP
