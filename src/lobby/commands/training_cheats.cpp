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

#include "item.hpp"
#include "nitro.hpp"
#include "karts/abstract_kart.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "modes/world.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool ItemCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked || !ServerConfig::m_cheats)
    {
        ctx->write("Cheats are not allowed on this server.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();

    std::string itemname;

    *parser >> itemname;

    parser->parse_finish(); // Do not allow more arguments

    if (!stk_ctx->testPlayerOnly()) return false;
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
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
    int quantity;
    type = PowerupManager::getPowerupFromName(itemname);
    quantity = ServerConfig::getCheatQuantity(type);
        
    if (!quantity)
    {
        ctx->nprintf("Unknown item: %s.", 512, itemname.c_str());
        ctx->flush();
        return false;
    }

    STKPeer* const peer = stk_ctx->get_peer();

    const std::set<unsigned int>& k_ids
        = peer->getAvailableKartIDs();
    if (peer->isWaitingForGame() || k_ids.empty())
    {
        std::string msg = "You are not in the game.";
        lobby->sendStringToPeer(msg, peer);
        return false;
    }
    else if (k_ids.size() > 1)
    {
        Log::warn("ServerLobby", "item: Player %s has multiple kart IDs.", 
                StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()).c_str());
    }
        unsigned int a = *k_ids.begin();
        target = w->getKart(a);

        // set the powerup
        target->setPowerup(PowerupManager::POWERUP_NOTHING, 0);
        target->setPowerup(type, quantity);
        std::string msgtarget = "Your powerup has been changed.";
        lobby->sendStringToPeer(msgtarget, peer);
        if (peer->hasPlayerProfiles())
        {
            // report to the log
            Log::info("ServerLobby", "ITEM %s(ID=%d) %d for %s",
                itemname.c_str(), type, quantity, 
                stk_ctx->getProfileName().c_str());
        }

    return true;
}
bool NitroCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (ServerConfig::m_ranked || !ServerConfig::m_cheats)
    {
        ctx->write("Cheats are not allowed on this server.");
        ctx->flush();
        return false;
    }

    auto parser = ctx->get_parser();
    parser->parse_finish();

    if (!stk_ctx->testPlayerOnly()) return false;
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    STKPeer* const peer = stk_ctx->get_peer();
    ServerLobby* const lobby = stk_ctx->get_lobby();
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

    float quantity;

    quantity = ServerConfig::m_cheat_nitro;
    
    if (!quantity)
    {
        ctx->write("This command is unavailable.");
        ctx->flush();
        return false;
    }

    const std::set<unsigned int>& k_ids
        = peer->getAvailableKartIDs();
    if (peer->isWaitingForGame() || k_ids.empty())
    {
        ctx->write("You are not in the game.");
        ctx->flush();
        return false;
    }
    else if (k_ids.size() > 1)
    {
        Log::warn("ServerLobby", "item: Player %s has multiple kart IDs.", 
                StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()).c_str());
    }
    unsigned int a = *k_ids.begin();
    target = w->getKart(a);

    // set the powerup
    target->setEnergy(.0f);
    target->setEnergy(quantity);
    ctx->write("Your nitro has been changed.");
    ctx->flush();
    if (peer->hasPlayerProfiles())
    {
        // report to the log
        Log::info("ServerLobby", "NITRO %f for %s",
            quantity, 
            StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName()).c_str());
    }
    return true;
}
