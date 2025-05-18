//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2018 SuperTuxKart-Team
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

#ifndef SERVER_PERMISSION_LEVEL_HPP
#define SERVER_PERMISSION_LEVEL_HPP

#include <limits>

/* Moderation toolkit */
enum ServerPermissionLevel : int
{
    PERM_NONE = -100,        // no chat, nothing
    PERM_SPECTATOR = -20,    // chat is allowed, can spectate the game
    PERM_PRISONER = -10,     // can play the game, but unable to change teams
    PERM_PLAYER = 0,         // can participate in the game and change teams
    PERM_MODERATOR = 80,     // staff, allowed to change other players perm status
                             // (only less to the own)
                             // and use general moderation commands such as 
                             // mute, kick, ban.
    PERM_REFEREE = 90,       // only active during tournament
    PERM_ADMINISTRATOR = 100,// staff, can change current server's mode and toggle
                             // between owner-less on or off, can disable command 
                             // voting
    PERM_OWNER = std::numeric_limits<int>::max(),
                             // Special peer, has all permissions,
                             // including giving the administrator permission level.
                             // Specified in the configuration file.
};

#endif // SERVER_PERMISSION_LEVEL_HPP
