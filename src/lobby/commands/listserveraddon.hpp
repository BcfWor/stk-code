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

#ifndef LOBBY_COMMAND_LISTSERVERADDON_HPP
#define LOBBY_COMMAND_LISTSERVERADDON_HPP

#include "lobby/stk_command.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "utils/cpp2011.hpp"

class ListServerAddonCommand : public STKCommand
{
    ServerPermissionLevel m_required_perm = (ServerPermissionLevel)(PERM_NONE + 1);
public:
    ListServerAddonCommand() : STKCommand(false)
    {
        m_name = "listserveraddon";
        m_args = {};
        m_optargs = {{nnwcli::CT_STRING, "option", "-track/-arena/-kart/-soccer"},
                     {nnwcli::CT_STRING, "addon id without prefix (addon_...)",
                         "addon string to find (at least 3 characters)"}};
        m_description = "Shows installed addons on the server.";
    }

    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_COMMAND_LISTSERVERADDON_HPP
