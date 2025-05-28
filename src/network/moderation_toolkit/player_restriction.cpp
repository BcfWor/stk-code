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

#include "player_restriction.hpp"
#include <vector>

const char* 
    getRestrictionName(PlayerRestriction prf)
{
    switch(prf)
    {
        case PRF_NOTEAM:
            return "noteam";
        case PRF_NOPCHAT:
            return "nopchat";
        case PRF_NOCHAT:
            return "nochat";
        case PRF_NOGAME:
            return "nogame";
        case PRF_NOSPEC:
            return "nospec";
        case PRF_HANDICAP:
            return "handicap";
        case PRF_TRACK:
            return "track";
        case PRF_ITEMS:
            return "items";
        case PRF_OK:
            return "ok";
    }
}
PlayerRestriction getRestrictionValue(
        const std::string& restriction)
{
    if (restriction == "noteam")
        return PRF_NOTEAM;
    if (restriction == "nopchat")
        return PRF_NOPCHAT;
    if (restriction == "nochat")
        return PRF_NOCHAT;
    if (restriction == "nogame")
        return PRF_NOGAME;
    if (restriction == "nospec")
        return PRF_NOSPEC;
    if (restriction == "handicap")
        return PRF_HANDICAP;
    if (restriction == "track")
        return PRF_TRACK;
    if (restriction == "items")
        return PRF_ITEMS;
    if (restriction == "ok")
        return PRF_OK;
    return PRF_OK;
}
const std::string formatRestrictions(PlayerRestriction prf)
{
    std::vector<std::string> res_v;
    for (unsigned char i = 0; i < 8; ++i)
    {
        uint32_t c = prf & (1 << i);
        if (c != 0)
            res_v.push_back(getRestrictionName((PlayerRestriction)c));
    }

    std::string result;
    for (unsigned char i = 0; i < res_v.size(); ++i)
    {
        result += res_v[i];
        if (i != res_v.size() - 1)
            result += ", ";
    }
    return result;
}
