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

#include "hackitem.hpp"
#include "karts/abstract_kart.hpp"
#include "lobby/commands/hacknitro.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool HackitemCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked)
    {
        ctx->write("This command does not apply for ranked servers.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    std::string itemname;
    unsigned char quantity;
    std::string playername;

    *parser >> itemname;
    *parser >> quantity;
    parser->parse_string(playername, false);

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    // only during the game
    if (!lobby->isRacing())
    {
        ctx->write("The game is not running.");
        ctx->flush();
        return false;
    }

    World* w = World::getWorld();
    if (!w)
    {
        ctx->write("World is not available right now.");
        ctx->flush();
        return false;
    }
    AbstractKart* target;
    PowerupManager::PowerupType type;

    STKPeer* target_peer;
    if (playername.empty())
    {
        target_peer = stk_ctx->get_peer();
        if (!target_peer)
        {
            ctx->write("Console should always specify player name.");
            ctx->flush();
            return false;
        }
    }
    else
        target_peer = STKHost::get()->findPeerByName(
                    StringUtils::utf8ToWide(playername),
                    true, true
                    ).get();

    if (!target_peer)
    {
        ctx->write("Player is not online.");
        ctx->flush();
        return false;
    }
    const std::set<unsigned int>& k_ids
        = target_peer->getAvailableKartIDs();
    if (target_peer->isWaitingForGame() || k_ids.empty())
    {
        ctx->write("Player is not in the game or has no available karts.");
        ctx->flush();
        return false;
    }
    else if (k_ids.size() > 1)
    {
        Log::warn("HackitemCommand", "hackitem: Player %s has multiple kart IDs.", 
                StringUtils::wideToUtf8(target_peer->getPlayerProfiles()[0]->getName()).c_str());
    }
    unsigned int a = *k_ids.begin();
    target = w->getKart(a);
    type = PowerupManager::getPowerupFromName(itemname);

    if (type == PowerupManager::POWERUP_NOTHING)
        quantity = 0;

    // set the powerup
    target->setPowerup(PowerupManager::POWERUP_NOTHING, 0);
    target->setPowerup(type, quantity);
    std::string msgtarget = "Your powerup has been changed.";
    lobby->sendStringToPeer(msgtarget, target_peer);
    if (target_peer->hasPlayerProfiles())
    {
        // report to the log
        Log::warn("ServerLobby", "HACKITEM %s(ID=%d) %d for %s by %s",
            itemname.c_str(), type, quantity, 
            StringUtils::wideToUtf8(
            target_peer->getPlayerProfiles()[0]->getName()).c_str(),
            stk_ctx->getProfileName().c_str());
        if (stk_ctx->get_peer() != target_peer)
        {
            ctx->nprintf(
                "Changed powerup for player %s.",
                512,
                StringUtils::wideToUtf8(
                    target_peer->getPlayerProfiles()[0]->getName()).c_str());
            ctx->flush();
        }
    }

    return true;
}
bool HacknitroCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked)
    {
        ctx->write("This command does not apply for ranked servers.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    float quantity;
    std::string playername;

    *parser >> quantity;
    parser->parse_string(playername, false);

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    // only during the game
    if (!lobby->isRacing())
    {
        ctx->write("The game is not running.");
        ctx->flush();
        return false;
    }

    World* w = World::getWorld();
    if (!w)
    {
        ctx->write("World is not available right now.");
        ctx->flush();
        return false;
    }
    AbstractKart* target;
 
    STKPeer* target_peer;
    if (playername.empty())
    {
        target_peer = stk_ctx->get_peer();
        if (!target_peer)
        {
            ctx->write("Console should always specify player name.");
            ctx->flush();
            return false;
        }
    }
    else
        target_peer = STKHost::get()->findPeerByName(
                    StringUtils::utf8ToWide(playername),
                    true, true
                    ).get();

    if (!target_peer)
    {
        ctx->write("Player is not online.");
        ctx->flush();
        return false;
    }
    const std::set<unsigned int>& k_ids
        = target_peer->getAvailableKartIDs();
    if (target_peer->isWaitingForGame() || k_ids.empty())
    {
        ctx->write("Player is not in the game or has no available karts.");
        ctx->flush();
        return false;
    }
    else if (k_ids.size() > 1)
    {
        Log::warn("ServerLobby", "hacknitro: Player %s has multiple kart IDs.", 
                stk_ctx->getProfileName().c_str());
    }
    unsigned int a = *k_ids.begin();
    target = w->getKart(a);

    // set the powerup
    target->setEnergy(0.0);
    target->setEnergy(quantity);
    std::string msgtarget = "Your nitro has been changed.";
    lobby->sendStringToPeer(msgtarget, target_peer);
    if (target_peer->hasPlayerProfiles())
    {
        // report to the log
        Log::warn("ServerLobby", "HACKNITRO %f for %s by %s",
            quantity, 
            StringUtils::wideToUtf8(target_peer->getPlayerProfiles()[0]->getName()).c_str(),
            stk_ctx->getProfileName().c_str());
        if (stk_ctx->get_peer() != target_peer)
        {
            ctx->nprintf(
                "Changed nitro for player %s.", 512,
                StringUtils::wideToUtf8(
                    target_peer->getPlayerProfiles()[0]->getName()).c_str());
            ctx->flush();
        }
    }
    return true;
}
