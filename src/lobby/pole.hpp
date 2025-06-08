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


#ifndef LOBBY_POLE_HPP
#define LOBBY_POLE_HPP

#include "network/stk_peer.hpp"
#include "network/network_player_profile.hpp"
#include <map>

class Pole
{
public:
    typedef std::map<STKPeer*,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterMap;
    typedef std::map<STKPeer* const,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterConstMap;
    typedef std::pair<STKPeer* const,
                      std::weak_ptr<NetworkPlayerProfile>>
        PoleVoterConstEntry;
    typedef std::pair<std::weak_ptr<NetworkPlayerProfile>,
                      unsigned int>
        PoleVoterResultEntry;
    typedef std::pair<const std::weak_ptr<NetworkPlayerProfile>,
                      unsigned int>
        PoleVoterConstResultEntry;

};

#endif // LOBBY_POLE_HPP
