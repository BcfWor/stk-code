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

#include "lobby/player_queue.hpp"
#include "network/database/abstract_database.hpp"
#include "network/moderation_toolkit/player_restriction.hpp"
#include "network/remote_kart_info.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "veto.hpp"
#include "ban.hpp"
#include "unban.hpp"
#include "listban.hpp"
#include "restrict.hpp"
#include "setteam.hpp"
#include "setkart.hpp"
#include "sethandicap.hpp"
#include "setowner.hpp"
#include "karts/kart_properties.hpp"
#include "karts/kart_properties_manager.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp"
#include <parser/argline_parser.hpp>
#include <string>
#include <unistd.h>

bool VetoCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    bool state;

    STKPeer* const peer = stk_ctx->get_peer();
    if (!peer)
    {
        ctx->write("Console has permanent veto which cannot be disabled.");
        ctx->flush();
        return false;
    }

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    if (!parser->parse_bool(state))
    {
        ctx->write("Current veto status: ");
        if (peer->getVeto() >= m_required_perm)
            ctx->write("ON");
        else
            ctx->write("OFF");
        ctx->write(". Use /veto on or /veto off to change.");
        ctx->flush();
        return true;
    }
    parser->parse_finish(); // Do not allow more arguments

    if (state)
    {
        peer->setVeto(peer->getPermissionLevel());
        ctx->write("Forcing votable commands is now enabled.");
    }
    else
    {
        peer->setVeto(0);
        ctx->write("Votable commands are no longer forced.");
    }
    ctx->flush();

    return true;
}

bool BanCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();

    std::string playername, reason;
    int days = -1;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    *parser >> playername;
    parser->parse_string(reason, false);
    parser->parse_integer(days, false);

    parser->parse_finish();

    // get target
    int32_t trg_permlvl = db->loadPermissionLevelForUsername(
        StringUtils::utf8ToWide(playername));
    int32_t sender_permlvl = stk_ctx->getPermissionLevel();
    Log::verbose("BanCommand", "sender_permlvl = %d, trg_permlvl = %d",
            sender_permlvl, trg_permlvl);

    if (trg_permlvl >= sender_permlvl)
    {
        ctx->write("You cannot ban someone who has at least your level of permissions.");
        ctx->flush();
        return false;
    }

    int res;
    if ((res = lobby->banPlayer(playername, reason, days)) == 0)
    {
        ctx->nprintf("Banned player %s.", 512, playername.c_str());
        ctx->flush();
        return true;
    }
    else if (res == 1)
    {
        ctx->write("Player's online id is not known in the database.");
        ctx->flush();
        return false;
    }
    else if (res == 2)
    {
        ctx->write("Failed to ban the player, check the network console.");
        ctx->flush();
        return false;
    }
    return false;
}
bool UnbanCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();

    std::string playername;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    *parser >> playername;
    parser->parse_finish();

    int res;
    if ((res = db->unbanPlayer(playername)) == 0)
    {
        ctx->nprintf("Unbanned player %s.", 512, playername.c_str());
        ctx->flush();
        return false;
    }
    else if (res == 1)
    {
        ctx->write("Player's online id is not known in the database.");
        ctx->flush();
        return false;
    }
    else if (res == 2)
    {
        ctx->write("Failed to unban the player, check the network console.");
        ctx->flush();
        return false;
    }
    return false;
}
bool ListBanCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    std::stringstream out;
    if (db)
        db->listBanTable(out);

    std::string res = out.str();
    if (res.empty())
        ctx->write("Nothing to show.");
    else
        ctx->write(res);
    ctx->flush();
    return true;
}

static
bool restrict_command(
        nnwcli::CommandExecutorContext* const ctx,
        ServerLobbyCommands::DispatchData* const dispatch_data,
        std::string& playername,
        AbstractDatabase* const db,
        STKCommandContext* const stk_ctx,
        const bool state,
        std::string& restriction_name,
        const PlayerRestriction restriction)
{
    irr::core::stringw playername_w = StringUtils::utf8ToWide(playername);

    auto target = STKHost::get()->findPeerByName(
            playername_w, true, true);
    int target_permlvl = db->loadPermissionLevelForUsername(playername_w);
    auto target_rv_k = db->loadRestrictionsForUsername(playername_w);

	std::string _k = std::get<1>(target_rv_k);

    if (stk_ctx->get_peer() && stk_ctx->getPermissionLevel() < target_permlvl)
    {
        ctx->write("You can only apply restrictions to a player that has lower permission level than yours.");
        ctx->flush();
        return false;
    }
    if (target && target->hasPlayerProfiles() &&
            target->getPlayerProfiles()[0]->getOnlineId() != 0)
    {
        auto& targetPlayer = target->getPlayerProfiles()[0];
        if (!state && restriction_name == "all")
            target->clearRestrictions();
        else if (state)
            target->addRestriction(restriction);
        else
            target->removeRestriction(restriction);
        db->writeRestrictionsForUsername(
                targetPlayer->getName(),
                target->getRestrictions(), _k);

        const PeerEligibility old_el = target->getEligibility();
        target->testEligibility();
        LobbyPlayerQueue::get()->onPeerEligibilityChange(dispatch_data->m_peer_wkptr.lock(), old_el);

        ctx->nprintf(
                "Set %s to %s for player %s.",
                512,
                getRestrictionName(restriction),
                state ? "on" : "off",
                StringUtils::wideToUtf8(targetPlayer->getName()).c_str());
        ctx->flush();
        return true;
    }

    return false;
}

bool RestrictCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    // Triggers eligibility change
    STK_CTX(stk_ctx, ctx);
    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data) return false;

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();
    
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    bool state;
    std::string playername, restriction_name;

    *parser >> state;
    *parser >> restriction_name;
    *parser >> playername;

    parser->parse_finish();

    PlayerRestriction restriction = getRestrictionValue(restriction_name);

    if ((restriction == PRF_OK) && state)
    {
        ctx->nprintf("Invalid name for restriction: %s", 512, restriction_name.c_str());
        ctx->flush();
        return false;
    }

    return restrict_command(
            ctx, dispatch_data, playername, db, stk_ctx, state, restriction_name, restriction);
}

bool RestrictAliasCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    // Triggers eligibility change
    STK_CTX(stk_ctx, ctx);
    ServerLobbyCommands::DispatchData* const dispatch_data =
        reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
    if (!data) return false;

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();
    
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    bool state;
    std::string playername;

    *parser >> state;
    *parser >> playername;

    parser->parse_finish();

    return restrict_command(
            ctx, dispatch_data, playername, db, stk_ctx, state, m_name, m_restriction);
}
bool SetTeamCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    // Triggers eligibility change
    STK_CTX(stk_ctx, ctx);
    if (!data) return false;

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    
    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    std::string teamname, playername;

    *parser >> teamname;
    *parser >> playername;

    parser->parse_finish();

    if (teamname.empty())
    {
        ctx->write("Specify team name.");
        ctx->flush();
        return false;
    }
    KartTeam team = KART_TEAM_NONE;
    
    if (teamname[0] == 'b')
    {
        team = KART_TEAM_BLUE;
    }
    else if (teamname[0] == 'r')
    {
        team = KART_TEAM_RED;
    }
    std::shared_ptr<NetworkPlayerProfile> t_player = nullptr;
    auto t_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(playername), true, true, &t_player);
    if (!t_player || !t_peer || !t_peer->hasPlayerProfiles())
    {
        ctx->write("Invalid target player: ");
        ctx->write(playername);
        ctx->flush();
        return false;
    }
    lobby->forceChangeTeam(t_player.get(), team);
    lobby->updatePlayerList();

    ctx->write("Player team has been updated.");
    ctx->flush();
    return true;
}
bool SetKartCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    // Triggers eligibility change
    STK_CTX(stk_ctx, ctx);
    if (!data) return false;

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    AbstractDatabase* const db = lobby->getDatabase();
    STKPeer* const peer = stk_ctx->get_peer();
    
    if (ServerConfig::m_command_kart_mode)
    {
        CMD_REQUIRE_PERM(stk_ctx, PERM_PLAYER);
    }
    else if (ServerConfig::m_command_kart_mode && peer &&
                !peer->isEligibleForGame())
    {
        ctx->write("You need to be able to play in order to use this command.");
        ctx->flush();
        return false;
    }
    else if (!ServerConfig::m_command_kart_mode)
    {
        CMD_REQUIRE_PERM(stk_ctx, m_required_perm);
    }

    std::string kartname, playername;
    bool permanent = false;;

    *parser >> kartname;
    if (parser->parse_string(playername, false)) // can set own kart
    {
        parser->parse_bool(permanent, false);
    }

    const bool canSpecifyExtra = stk_ctx->getPermissionLevel() >= m_required_perm;
    parser->parse_finish();

    const KartProperties* kart =
        kart_properties_manager->getKart(kartname);
    if ((!kart || kart->isAddon()) && kartname != "off")
    {
        ctx->nprintf("Kart does not exist or is an addon kart: %s.", 512,
                kartname.c_str());
        ctx->flush();
        return false;
    }
    else
        kart = nullptr;

    std::shared_ptr<NetworkPlayerProfile> t_player = nullptr;
    STKPeer* t_peer = nullptr;
    if (canSpecifyExtra && !playername.empty())
        t_peer = STKHost::get()->findPeerByName(
                StringUtils::utf8ToWide(playername), true, true, &t_player).get();
    else if (!stk_ctx->get_peer())
    {
        ctx->write("Console always need to specify the player name.");
        format_usage_into(stk_ctx->get_response_buffer(), ctx->get_alias());
        ctx->flush();
    }
    else
    {
        t_peer = stk_ctx->get_peer();
        t_player = t_peer->getPlayerProfiles()[0];
    }
    if (!t_player || !t_peer || !t_peer->hasPlayerProfiles())
    {
        ctx->write("Invalid target player: ");
        ctx->write(playername);
        ctx->flush();
        return false;
    }

    if (kartname == "off")
    {
        std::string targetmsg = "You can choose any kart now.";
        lobby->sendStringToPeer(targetmsg, t_peer);
        t_player->unforceKart();
        if (permanent)
            db->writeRestrictionsForOID(t_player->getOnlineId(), "");
        if (t_peer != peer)
        {
            ctx->nprintf("No longer forcing a kart for %s.", 512,
                    playername.c_str());
            ctx->flush();
        }
    }
    else
    {
        std::string targetmsg = "Your kart is " + kartname + " now.";
        lobby->sendStringToPeer(targetmsg, t_peer);
        t_player->forceKart(kartname);
        if (permanent)
            db->writeRestrictionsForOID(t_player->getOnlineId(), kartname);
        if (t_peer != peer)
        {
            ctx->nprintf(
                    "Made %s use kart %s.", 512,
                    playername.c_str(), kartname.c_str());
            ctx->flush();
        }
    }
    Log::info("ServerLobby", "setkart %s %s", kartname.c_str(), playername.c_str());
    return true;
}
bool SetHandicapCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string playername;
    std::string levelname;

    *parser >> playername;
    *parser >> levelname;

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    HandicapLevel level;

    if (levelname.empty())
    {
        ctx->write("Specify levelname.");
        ctx->flush();
        return false;
    }
    else if (levelname == "none" || levelname == "no" || levelname == "n")
        level = HANDICAP_NONE;
    else if (levelname == "count" || levelname == "co" || levelname == "c")
        level = HANDICAP_COUNT;
    else if (levelname == "medium" || levelname == "med" || levelname == "m")
        level = HANDICAP_MEDIUM;
    else
    {
        ctx->write("Unknown handicap level: ");
        ctx->write(levelname);
        ctx->write(". Supported levels: none/count/medium.");
        ctx->flush();
        return false;
    }

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    std::shared_ptr<NetworkPlayerProfile> t_player = nullptr;
    auto t_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(playername), true, true, &t_player);
    if (!t_player || !t_peer || !t_peer->hasPlayerProfiles())
    {
        ctx->write("Invalid target player: ");
        ctx->flush();
        return false;
    }
    
    t_player->setHandicap(level);
    lobby->updatePlayerList();

    ctx->write("Player handicap has been updated.");
    ctx->flush();

    return true;
}
bool SetOwnerCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();

    std::string playername;

    *parser >> playername;

    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();

    if (ServerConfig::m_owner_less)
    {
        ctx->write("This command is only usable on a server that supports crowned players.");
        ctx->flush();
        return false;
    }

    std::shared_ptr<NetworkPlayerProfile> t_player = nullptr;
    std::shared_ptr<STKPeer> t_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(playername), true, true, &t_player
            );
    if (!t_player || !t_peer || !t_peer->hasPlayerProfiles())
    {
        ctx->write("Invalid target player: ");
        ctx->write(playername);
        ctx->flush();
        return false;
    }
    else if (lobby->getServerOwner()->isSamePeer(t_peer.get()))
    {
        ctx->write("This player is already a server owner.");
        ctx->flush();
        return false;
    }

    CMD_VOTABLE(data, true);
    CMD_SELFVOTE_PERMLOWER_CROWN(stk_ctx, data, m_min_veto, parser);

    // update the owner and inform
    lobby->updateServerOwner(t_peer);

    ctx->write("Owner has been changed.");
    ctx->flush();
    return true;
}
