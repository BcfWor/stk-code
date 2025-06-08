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

#ifndef LOBBY_COMMAND_SPECIAL_MODIFIERS_COMMON_HPP
#define LOBBY_COMMAND_SPECIAL_MODIFIERS_COMMON_HPP

#include "context.hpp"
#include "items/powerup.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
// Here's the code for commands that set the special powerup modifiers.
// Such commands are /bowlparty, /cakeparty, /plungerparty, /zipperparty etc.
// As they share common code, it can be used across different commands.

// tsm_displayname should be titlecase
// special_modifier_description should describe what it does.
// votable parameter corresponds to the value of the configuration entry. For
// example ServerConfig::m_allow_bowlparty for bowlparty, or other parameters like
// !ServerConfig::m_tiers_roulette. Commands should pass that.
bool specialPowerupCommandFor(
        nnwcli::CommandExecutorContext* ctx,
        void* data,
        bool votable,
        ServerPermissionLevel min_veto,
        Powerup::SpecialModifier tsm,
        bool state,
        const std::string tsm_displayname,
        const std::string special_modifier_description);

#endif // LOBBY_COMMAND_SPECIAL_MODIFIERS_COMMON_HPP
