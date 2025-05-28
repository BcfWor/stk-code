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

#ifndef PLAYER_RESTRICTION_HPP
#define PLAYER_RESTRICTION_HPP

#include <cstdint>
#include <string>

// Moderation toolkit
enum PlayerRestriction: uint32_t
{
    PRF_OK = 0, //!< Default, no restrictions are applied to the player
    PRF_NOSPEC = 1, //!< Player is unable to spectate
    PRF_NOGAME = 2, //!< Player is unable to play the game
    PRF_NOCHAT = 4, //!< Player is unable to send chat messages
    PRF_NOPCHAT = 8, //!< Player is unable to send private chat messages
    PRF_NOTEAM = 16, //!< Player profiles of the peer cannot change teams
    PRF_HANDICAP = 32, //!< Player is unable to toggle the handicap
    //PRF_KART = 64, //!< Player is unable to select the kart by themselved
    PRF_TRACK = 128, //!< Player is unable to vote for the track
    PRF_ITEMS = 256, //!< Player is unable to pick up items in game
};  // PlayerRestriction

const char* 
    getRestrictionName(PlayerRestriction prf);
PlayerRestriction getRestrictionValue(
        const std::string& restriction);
const std::string formatRestrictions(PlayerRestriction prf);

#endif // PLAYER_RESTRICTION_HPP
