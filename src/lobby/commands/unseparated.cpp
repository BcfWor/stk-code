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

#include "rps.hpp"
#include "jumbleword.hpp"
#include "lobby/rps_challenge.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <string>

bool RPSCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (!stk_ctx->testPlayerOnly()) return false;

    auto parser = ctx->get_parser();

    std::string variant;
    *parser >> variant;

    parser->parse_finish(); // Do not allow more arguments

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    STKPeer* const peer = stk_ctx->get_peer();

    uint32_t peer_id = peer->getHostId();
    std::string player_name;

    player_name = stk_ctx->getProfileName();

    std::string arg_lower = player_name;
    std::transform(arg_lower.begin(), arg_lower.end(), arg_lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    if (arg_lower == "accept" || arg_lower == "a")
    {
        for (auto& challenge : lobby->m_rps_challenges)
        {
            if (challenge.challenged_id == peer_id && !challenge.accepted)
            {
                challenge.accepted = true;
                challenge.timeout = StkTime::getMonoTimeMs() + 20000; // 20 seconds to choose

                std::shared_ptr<STKPeer> challenger_peer = NULL;
                for (auto& p : STKHost::get()->getPeers())
                {
                    if (p->getHostId() == challenge.challenger_id)
                    {
                        challenger_peer = p;
                        break;
                    }
                }

                if (challenger_peer)
                {
                    std::string msg = player_name + " accepted your Rock Paper Scissors challenge! You have 20 seconds to choose /rps rock (r), /rps paper (p), or /rps scissors (s).";
                    lobby->sendStringToPeer(msg, challenger_peer);
                }

                ctx->nprintf("You accepted the Rock Paper Scissors challenge from %s! You have 10 seconds to choose /rps rock (r), /rps paper (p), or /rps scissors (s).", 2048, challenge.challenger_name.c_str());
                ctx->flush();
                return true;
            }
        }

        ctx->write("You don't have any pending Rock Paper Scissors challenges.");
        ctx->flush();
        return false;
    }

    // Handle full names and abbreviations
    RPSChoice choice = RockPaperScissors::rpsFromString(arg_lower);
    std::string choice_str = RockPaperScissors::rpsToString(choice);

    if (choice != RPS_NONE)
    {
        for (auto& challenge : lobby->m_rps_challenges)
        {
            if (!challenge.accepted)
                continue;

            RPSChoice* source_choice;
            RPSChoice* target_choice;

            if (challenge.challenger_id == peer_id &&
                    challenge.challenger_choice == RPS_NONE)
            {
                source_choice = &challenge.challenger_choice;
                target_choice = &challenge.challenged_choice;
            }
            else if (challenge.challenged_id == peer_id &&
                    challenge.challenged_choice == RPS_NONE)
            {
                source_choice = &challenge.challenged_choice;
                target_choice = &challenge.challenger_choice;
            }
            else
            {
                ctx->write("You already chose your option, this can only be chosen once.");
                ctx->flush();
                return false;
            }
            *source_choice = choice;
            ctx->nprintf("You chose %s! Waiting for your opponent...", 1024,
                    choice_str.c_str());
            ctx->flush();

            if (*target_choice != RPS_NONE)
            {
                lobby->determineRPSWinner(challenge);
            }
            return true;
        }
        ctx->write("You don't have any active Rock Paper Scissors games.");
        ctx->flush();
        return false;
    }

    // Handle /rps [playername] - challenge a player
    std::string target_name = variant;
    std::shared_ptr<STKPeer> target_peer = NULL;
    uint32_t target_id = 0;

    // Case-insensitive player name search
    target_peer = STKHost::get()->findPeerByName(
            StringUtils::utf8ToWide(target_name), true/*ignoreCase*/, true/*onlyPrefix*/);
    if (target_peer)
    {
        target_id = target_peer->getHostId();
        target_name = StringUtils::wideToUtf8(target_peer->getPlayerProfiles()[0]->getName());
    }
    else
    {
        ctx->nprintf("Player %s not found.", 512, target_name.c_str());
        ctx->flush();
        return false;
    }

    if (target_id == peer_id)
    {
        ctx->write("You can't challenge yourself!");
        ctx->flush();
        return false;
    }

    for (auto& challenge : lobby->m_rps_challenges)
    {
        if ((challenge.challenger_id == peer_id && challenge.challenged_id == target_id) ||
            (challenge.challenger_id == target_id && challenge.challenged_id == peer_id))
        {
            ctx->nprintf("There's already a Rock Paper Scissors challenge between you and %s.", 1024,
                    target_name.c_str());
            ctx->flush();
            return false;
        }
    }

    RPSChallenge challenge;
    challenge.challenger_id = peer_id;
    challenge.challenged_id = target_id;
    challenge.challenger_name = player_name;
    challenge.challenged_name = target_name;
    challenge.timeout = StkTime::getMonoTimeMs() + 40000; // 40 seconds to accept
    challenge.accepted = false;

    lobby->m_rps_challenges.push_back(challenge);

    ctx->write("You challenged " + target_name + " to Rock Paper Scissors!");
    ctx->flush();

    lobby->sendStringToPeer(player_name + " challenged you to Rock Paper Scissors! "
            "Type /rps accept to play.", target_peer);
    return true;
}
bool JumblewordCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    if (!stk_ctx->testPlayerOnly()) return false;

    auto parser = ctx->get_parser();

    std::string answer;
    parser->parse_string(answer, false);

    parser->parse_finish(); // Do not allow more arguments

    if (!stk_ctx->testPlayerOnly()) return false;

    CMD_REQUIRE_PERM(stk_ctx, m_required_perm);

    ServerLobby* const lobby = stk_ctx->get_lobby();
    STKPeer* const peer = stk_ctx->get_peer();

    uint32_t peer_id = peer->getHostId();
    std::string player_name;
    for (auto& player : peer->getPlayerProfiles())
    {
        player_name = StringUtils::wideToUtf8(player->getName());
        break;
    }
    std::lock_guard<std::mutex> lock(lobby->m_jumble_mutex);
    if (answer.empty())
    {
        auto it_jumbled = lobby->m_jumble_player_jumbled.find(peer_id);
        if (it_jumbled != lobby->m_jumble_player_jumbled.end())
        {
            ctx->write("Current word to unscramble: ");
            ctx->write(it_jumbled->second);
            ctx->flush();
        }
        else
        {
            lobby->startJumbleForPlayer(peer_id);
        }
        return true;
    }
    answer = StringUtils::toLowerCase(answer);
    auto it_word = lobby->m_jumble_player_words.find(peer_id);
    if (it_word != lobby->m_jumble_player_words.end())
    {
        if (answer == it_word->second)
        {
            lobby->endJumbleForPlayer(peer_id, true);
        }
        else
        {
            ctx->nprintf("Sorry, '%s' is not correct. Try again!", 1024,
                    answer.c_str());
            ctx->flush();
        }
    }
    else
    {
        lobby->startJumbleForPlayer(peer_id);
    }

    return true;
}
