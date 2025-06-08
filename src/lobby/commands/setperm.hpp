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

#ifndef LOBBY_COMMAND_SETPERM_HPP
#define LOBBY_COMMAND_SETPERM_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

class SetPermCommand : public STKCommand
{
    ServerPermissionLevel m_min_perm = PERM_ADMINISTRATOR;
public:
    SetPermCommand() : STKCommand(false)
    {
        m_name = "setperm";
        m_args = {{nnwcli::CT_INTEGER, "level", "Permission level (rank)"},
                  {nnwcli::CT_STRING, "player", "Name of player"}};
        m_description = "Sets permission of a player to a specified rank level. "
            "Can only set permissions to a value lower your current rank. "
            "Minumum rank required: administrator.";
    }

    // Implemented in setperm.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};
class SetPermAliasCommand : public STKCommand
{
    ServerPermissionLevel m_min_perm = PERM_ADMINISTRATOR;

    std::string m_rankname;
    ServerPermissionLevel m_level;
public:
    SetPermAliasCommand(
            const std::string& cmdname,
            const std::string& rankname,
            const ServerPermissionLevel level) : STKCommand(false)
    {
        m_name = cmdname;
        m_rankname = rankname;
        m_level = level;
        m_args = {{nnwcli::CT_STRING, "player", "Name of player"}};
        m_description = "Set the rank of the specified player to " + rankname +
            (". Can only set permissions to a value lower your current rank. "
            "Minumum rank required: administrator.");
    }

    // Implemented in setperm.cpp
    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_SETPERM_HPP
