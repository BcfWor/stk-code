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

#include "server_lobby_commands.hpp"

// COMMANDS
#include "argument_types.hpp"
#include "lobby/commands/ban.hpp"
#include "lobby/commands/broadcast.hpp"
#include "lobby/commands/chaosparty.hpp"
#include "lobby/commands/hackitem.hpp"
#include "lobby/commands/hacknitro.hpp"
#include "lobby/commands/infinite.hpp"
#include "lobby/commands/item.hpp"
#include "lobby/commands/kickall.hpp"
#include "lobby/commands/listban.hpp"
#include "lobby/commands/listpeers.hpp"
#include "lobby/commands/nitro.hpp"
#include "lobby/commands/restrict.hpp"
#include "lobby/commands/sample_command.hpp"
#include "lobby/commands/setdifficulty.hpp"
#include "lobby/commands/setgoaltarget.hpp"
#include "lobby/commands/sethandicap.hpp"
#include "lobby/commands/setkart.hpp"
#include "lobby/commands/setmode.hpp"
#include "lobby/commands/setperm.hpp"
#include "lobby/commands/soccerroulette.hpp"
#include "lobby/commands/spectate.hpp"
#include "lobby/commands/speedstats.hpp"
#include "lobby/commands/stk_seen.hpp"
#include "lobby/commands/unban.hpp"
#include "lobby/commands/vote.hpp"
#include "lobby/commands/help.hpp"
#include "lobby/commands/helpof.hpp"
#include "lobby/commands/quit.hpp"
#include "lobby/commands/addtime.hpp"
#include "lobby/commands/slots.hpp"
#include "lobby/commands/teamchat.hpp"
#include "lobby/commands/public.hpp"
#include "lobby/commands/private_message.hpp"
#include "lobby/commands/listserveraddon.hpp"
#include "lobby/commands/playeraddonscore.hpp"
#include "lobby/commands/playerhasaddon.hpp"
#include "lobby/commands/serverhasaddon.hpp"
#include "lobby/commands/score.hpp"
//#include "lobby/commands/"
#include "lobby/commands/ranklist.hpp"
#include "lobby/commands/rankof.hpp"
#include "lobby/commands/rank.hpp"
#include "lobby/commands/feature.hpp"
#include "lobby/commands/report.hpp"
// kart restriction commands
#include "lobby/commands/heavyparty.hpp"
#include "lobby/commands/mediumparty.hpp"
#include "lobby/commands/lightparty.hpp"
// special powerup modifier commands
#include "lobby/commands/bowlparty.hpp"
#include "lobby/commands/cakeparty.hpp"
#include "lobby/commands/bowltrainingparty.hpp"
#include "lobby/commands/nitroless.hpp"
#include "lobby/commands/itemless.hpp"
// other
#include "lobby/commands/scanservers.hpp"
#include "lobby/commands/mute.hpp"
#include "lobby/commands/unmute.hpp"
#include "lobby/commands/showcommands.hpp"
#include "lobby/commands/datetime.hpp"
#include "lobby/commands/karts.hpp"
#include "lobby/commands/tracks.hpp"
#include "lobby/commands/results.hpp"
#include "lobby/commands/replay.hpp"
#include "lobby/commands/autoteams.hpp"
#include "lobby/commands/resetball.hpp"
#include "lobby/commands/rps.hpp"
#include "lobby/commands/jumbleword.hpp"
#include "lobby/commands/randomkarts.hpp"
#include "lobby/commands/endgame.hpp"
#include "lobby/commands/goalhistory.hpp"
// special features
#include "lobby/commands/pole.hpp"
// moderation
#include "lobby/commands/kick.hpp"
#include "lobby/commands/veto.hpp"
#include "lobby/commands/customtext_reload.hpp"
#include "lobby/commands/setowner.hpp"
#include "lobby/commands/setteam.hpp"
#include "lobby/commands/settrack.hpp"
#include "lobby/commands/start.hpp"

#include "lobby/stk_command_context.hpp"
#include "network/moderation_toolkit/server_permission_level.hpp"
#include "network/protocols/lobby_protocol.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/network_console.hpp"
#include "network/server_config.hpp"
#include "network/stk_peer.hpp"
#include "network/stk_host.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

#include <memory>
#include <sstream>

static const char* LOGNAME = "ServerLobbyCommands";

ServerLobbyCommands* ServerLobbyCommands::g_instance = nullptr;

bool ServerLobbyCommands::VoteEntry::operator<(const VoteEntry& other) const
{
    // Sort by command name
    const std::string& this_cmd_name = m_voted_command->get_name();
    const std::string& other_cmd_name = other.m_voted_command->get_name();
    if (this_cmd_name == other_cmd_name)
    {
        if (m_voted_argline == other.m_voted_argline)
            return false;
        return m_voted_argline < other.m_voted_argline;
    }
    return this_cmd_name < other_cmd_name;
}
bool ServerLobbyCommands::VoteEntry::operator==(const VoteEntry& other) const
{
    return m_voted_command->get_name() == other.m_voted_command->get_name() && m_voted_argline == other.m_voted_argline;
}

ServerLobbyCommands::ServerLobbyCommands()
    : m_executor(), m_customtext_manager(&m_executor)
{
    registerCommands();
    Log::info(LOGNAME, "This server has %d commands in total.", m_executor.get_command_count());
}

ServerLobbyCommands::~ServerLobbyCommands()
{
}

void ServerLobbyCommands::create()
{
    g_instance = new ServerLobbyCommands();
    Log::verbose(LOGNAME, "Created instance");
}
void ServerLobbyCommands::destroy()
{
    delete g_instance;
    Log::verbose(LOGNAME, "Deleted instance");
}
std::shared_ptr<STKCommandContext>& ServerLobbyCommands::getNetworkConsoleContext()
{
    return NetworkConsole::network_console_context;
}

void ServerLobbyCommands::registerCommands()
{
    // Builtin commands
    m_executor.register_command(std::make_shared<HelpCommand>());
    m_executor.register_command(std::make_shared<HelpOfCommand>());
    // Basic command set
    m_executor.register_command(std::make_shared<VoteCommand>());
    m_executor.register_command(std::make_shared<SpectateCommand>());
    m_executor.add_alias("spec", "spectate");
    m_executor.add_alias("sp", "spectate");
    m_executor.add_alias("s", "spectate");
    m_executor.register_command(std::make_shared<QuitCommand>());
    // Sample
    // m_executor.register_command(std::make_shared<SampleCommand>());
    // Reworked official STK commands
    m_executor.register_command(std::make_shared<ListServerAddonCommand>());
    m_executor.register_command(std::make_shared<PlayerAddonScoreCommand>());
    m_executor.register_command(std::make_shared<PlayerHasAddonCommand>());
    m_executor.register_command(std::make_shared<ServerHasAddonCommand>());
    // Added STK commands
    m_executor.register_command(std::make_shared<AddTimeCommand>());
    m_executor.add_alias("addt", "addtime");
    m_executor.add_alias("moredelay", "addtime");
    m_executor.register_command(std::make_shared<KickCommand>());
    m_executor.register_command(std::make_shared<KickallCommand>());
    m_executor.register_command(std::make_shared<SlotsCommand>());
    m_executor.add_alias("sl", "slots");
    m_executor.add_alias("queue", "slots");
    m_executor.register_command(std::make_shared<TeamChatCommand>());
    m_executor.register_command(std::make_shared<PublicCommand>());
    m_executor.register_command(std::make_shared<PrivateMessageCommand>());
    m_executor.add_alias("to", "msg");
    m_executor.add_alias("pm", "msg");
    m_executor.add_alias("dm", "msg");
    m_executor.add_alias("private", "msg");
    m_executor.register_command(std::make_shared<ScoreCommand>());
    m_executor.register_command(std::make_shared<RankListCommand>());
    m_executor.add_alias("rank10", "ranklist");
    m_executor.add_alias("top", "ranklist");
    m_executor.register_command(std::make_shared<RankOfCommand>());
    m_executor.register_command(std::make_shared<RankCommand>());
    m_executor.register_command(std::make_shared<FeatureCommand>());
    m_executor.add_alias("inform", "feature");
    m_executor.add_alias("ifm", "feature");
    m_executor.add_alias("bug", "feature");
    m_executor.add_alias("suggest", "feature");
    m_executor.register_command(std::make_shared<ReportCommand>());
    m_executor.add_alias("tell", "report");
    m_executor.register_command(std::make_shared<HeavyPartyCommand>());
    m_executor.add_alias("hp", "heavyparty");
    m_executor.register_command(std::make_shared<MediumPartyCommand>());
    m_executor.add_alias("mp", "mediumparty");
    m_executor.register_command(std::make_shared<LightPartyCommand>());
    m_executor.add_alias("lp", "lightparty");
    m_executor.register_command(std::make_shared<BowlPartyCommand>());
    m_executor.add_alias("bp", "bowlparty");
    m_executor.register_command(std::make_shared<CakePartyCommand>());
    m_executor.add_alias("cp", "cakeparty");
    m_executor.register_command(std::make_shared<BowlTrainingPartyCommand>());
    m_executor.add_alias("btp", "bowltrainingparty");
    m_executor.register_command(std::make_shared<ItemlessCommand>());
    m_executor.add_alias("il", "itemless");
    m_executor.register_command(std::make_shared<NitrolessCommand>());
    m_executor.add_alias("nl", "nitroless");
    m_executor.register_command(std::make_shared<ScanServersCommand>());
    m_executor.add_alias("online", "scanservers");
    m_executor.add_alias("o", "scanservers");
    m_executor.register_command(std::make_shared<MuteCommand>());
    m_executor.add_alias("ignore", "mute");
    m_executor.register_command(std::make_shared<UnmuteCommand>());
    m_executor.add_alias("unignore", "unmute");
    m_executor.register_command(std::make_shared<ShowCommandsCommand>());
    m_executor.add_alias("commands", "showcommands");
    m_executor.add_alias("cmds", "showcommands");
    m_executor.add_alias("cmd", "showcommands");
    m_executor.register_command(std::make_shared<PoleCommand>());
    if (ServerConfig::m_ishigami_enabled)
    {
        m_executor.register_command(std::make_shared<StkSeenCommand>());
        m_executor.add_alias("seen", "stk-seen");
    }
    m_executor.register_command(std::make_shared<DatetimeCommand>());
    m_executor.add_alias("date", "datetime");
    m_executor.add_alias("time", "datetime");
    m_executor.register_command(std::make_shared<VetoCommand>());
    m_executor.register_command(std::make_shared<CustomTextReloadCommand>());
    m_executor.add_alias("ctext-r", "customtext-reload");
    m_executor.register_command(std::make_shared<KartsCommand>());
    m_executor.register_command(std::make_shared<TracksCommand>());
    m_executor.register_command(std::make_shared<ResultsCommand>());
    m_executor.add_alias("rs", "results");
    m_executor.register_command(std::make_shared<ReplayCommand>());
    m_executor.register_command(std::make_shared<RandomkartsCommand>());
    m_executor.add_alias("rks", "randomkarts");
    m_executor.register_command(std::make_shared<ResetBallCommand>());
    m_executor.add_alias("resetpuck", "resetball");
    m_executor.add_alias("rb", "resetball");
    m_executor.add_alias("rp", "resetball");
    m_executor.register_command(std::make_shared<GoalHistoryCommand>());
    m_executor.add_alias("goal", "goalhistory");
    m_executor.add_alias("history", "goalhistory");
    m_executor.register_command(std::make_shared<StartCommand>());
    m_executor.add_alias("begin", "start");
    m_executor.add_alias("play", "start");
    m_executor.register_command(std::make_shared<EndGameCommand>());
    m_executor.add_alias("end", "endgame");
    m_executor.add_alias("lobby", "endgame");
    m_executor.register_command(std::make_shared<BanCommand>());
    m_executor.register_command(std::make_shared<UnbanCommand>());
    m_executor.register_command(std::make_shared<ListBanCommand>());
    m_executor.add_alias("bans", "listban");
    m_executor.register_command(std::make_shared<RestrictCommand>());
    m_executor.add_alias("punish", "restict");
    // moderation toolkit's restrict aliases
    m_executor.register_command(std::make_shared<RestrictAliasCommand>(
                "nospec", PRF_NOSPEC));
    m_executor.register_command(std::make_shared<RestrictAliasCommand>(
                "nogame", PRF_NOGAME));
    m_executor.register_command(std::make_shared<RestrictAliasCommand>(
                "nochat", PRF_NOCHAT));
    m_executor.register_command(std::make_shared<RestrictAliasCommand>(
                "nopchat", PRF_NOPCHAT));
    m_executor.register_command(std::make_shared<RestrictAliasCommand>(
                "noteam", PRF_NOTEAM));
    m_executor.register_command(std::make_shared<SetTeamCommand>());
    m_executor.register_command(std::make_shared<SetKartCommand>());
    m_executor.register_command(std::make_shared<SetTrackCommand>());
    m_executor.add_alias("setfield", "settrack");
    m_executor.add_alias("setarena", "settrack");
    m_executor.register_command(std::make_shared<SetHandicapCommand>());
    m_executor.add_alias("sethc", "sethandicap");
    m_executor.register_command(std::make_shared<SetOwnerCommand>());
    m_executor.add_alias("sethost", "setowner");
    m_executor.register_command(std::make_shared<SetModeCommand>());
    m_executor.register_command(std::make_shared<SetDifficultyCommand>());
    m_executor.add_alias("setdiff", "setdifficulty");
    m_executor.register_command(std::make_shared<SetGoalTargetCommand>());
    m_executor.add_alias("setgt", "setgoaltarget");
    m_executor.register_command(std::make_shared<HackitemCommand>());
    m_executor.add_alias("hki", "hackitem");
    m_executor.register_command(std::make_shared<HacknitroCommand>());
    m_executor.add_alias("hkn", "hacknitro");
    m_executor.register_command(std::make_shared<ItemCommand>());
    m_executor.add_alias("i", "item");
    m_executor.register_command(std::make_shared<NitroCommand>());
    m_executor.add_alias("n", "nitro");
    m_executor.register_command(std::make_shared<InfiniteCommand>());
    m_executor.register_command(std::make_shared<SoccerRouletteCommand>());
    m_executor.add_alias("sr", "soccerroulette");
    m_executor.register_command(std::make_shared<ChaosPartyCommand>());
    m_executor.register_command(std::make_shared<BroadcastCommand>());
    m_executor.add_alias("bc", "broadcast");
    m_executor.register_command(std::make_shared<ListPeersCommand>());
    m_executor.register_command(std::make_shared<SpeedStatsCommand>());
    // Permission rank manipulation commands
    m_executor.register_command(std::make_shared<SetPermCommand>());
    m_executor.register_command(std::make_shared<SetPermAliasCommand>(
            "setplayer", "player", PERM_PLAYER));
    m_executor.register_command(std::make_shared<SetPermAliasCommand>(
            "setreferee", "referee", PERM_REFEREE));
    m_executor.register_command(std::make_shared<SetPermAliasCommand>(
            "setmoderator", "moderator", PERM_MODERATOR));
    m_executor.register_command(std::make_shared<SetPermAliasCommand>(
            "setadministrator", "administrator", PERM_ADMINISTRATOR));

    // TODO: this needs to be refactored in the second stage: soccer_elo_ranking and soccer_autoteams
    m_executor.register_command(std::make_shared<AutoteamsCommand>());
    m_executor.add_alias("mix", "autoteams");
    m_executor.add_alias("am", "autoteams");
    m_executor.register_command(std::make_shared<AutoteamsVariantVoteCommand>(true)); // a
    m_executor.register_command(std::make_shared<AutoteamsVariantVoteCommand>(false)); // b
    m_executor.register_command(std::make_shared<RPSCommand>());
    m_executor.register_command(std::make_shared<JumblewordCommand>());
    m_executor.add_alias("jw", "jumbleword");
}

void ServerLobbyCommands::handleServerCommand(ServerLobby* const lobby, std::shared_ptr<STKPeer>& peer, std::string& line)
{
    DispatchData dispatch_data;
    std::shared_ptr<STKCommandContext>& context = peer->getCommandContext();
    context->set_lobby(lobby);
    context->set_lobby_commands(this);

    unsigned int argv0_number;
    std::stringstream argv0_ss;

    argv0_ss << line;
    argv0_ss >> argv0_number;

    dispatch_data.m_peer_wkptr = peer;

    // TODO: change lobby to a separate singleton of a pole
    // and add a temporary callback to the numeric commands.
    if (lobby->isPoleEnabled() && line.size() == 1 && !argv0_ss.fail())
    {
        // command is a number, /1 /2 /3 ... and pole is enabled
        lobby->submitPoleVote(peer, argv0_number);
        return;
    }
    try {
        m_executor.dispatch_line(line, context, &dispatch_data);
    } catch (const std::exception& e) {
        Log::error(LOGNAME, "Could not execute the command \"%s\" by %s: %s",
                line.c_str(), context->getProfileName().c_str(), e.what());

        context->write("An unknown error has occurred when trying to execute the command. Inform server staff.");
        context->flush();
        return;
    }

    // The command have emitted the vote test
    if (dispatch_data.m_is_vote)
    {
        // the command itself can't always provide the SHARED pointer of the m_voted_command
        // due to the limitations of the nnwcli library
        auto stk_command = std::dynamic_pointer_cast<STKCommand>(
            dispatch_data.m_voted_command ? 
            dispatch_data.m_voted_command :
            // in case the command votes for itself, it can't provide shared pointer,
            // find it again
            m_executor.get_command(dispatch_data.m_voted_alias));

        // it has already been verified that the voted alias exists.
        assert(stk_command);

        // if it wasn't a self-vote, test it
        if (!ServerConfig::m_command_voting)
        {
            // voting is not supported, give up
            context->sendNoPermission();
            return;
        }
        else if (!stk_command || !stk_command->isVotable())
        {
            // Command is marked as unvotable
            context->write("This command cannot be voted.");
            context->flush();
            return;
        }
        else if(!dispatch_data.m_can_vote && !testVote(&dispatch_data, context.get()))
        {
            // Wrong syntax
            return;
        }
        else if (!dispatch_data.m_can_vote)
        {
            // previous test returned false, deny the permission
            context->sendNoPermission();
            return;
        }
        // command can be voted in, do it
        VoteEntry entry = {
            .m_voted_command = stk_command,
            .m_voted_args = dispatch_data.m_voted_args,
            .m_voted_argline = dispatch_data.m_voted_argline,
        };
        // generalize the argument line, e.g. turn yes/y/on/1 to "on"
        // for bool arg and so on
        entry.bakeArgline();
        submitVote(lobby, context->getProfileName(), context.get(), entry);
    }
}
void ServerLobbyCommands::handleNetworkConsoleCommand(std::string& line)
{
    auto sl = LobbyProtocol::get<ServerLobby>();
    DispatchData dispatch_data;
    NetworkConsole::network_console_context->set_lobby(sl.get());
    NetworkConsole::network_console_context->set_lobby_commands(this);

    try
    {
        m_executor.dispatch_line(
            line, NetworkConsole::network_console_context, &dispatch_data);
    }
    catch (const std::exception& e)
    {
        Log::error(LOGNAME, "Could not execute command: %s", e.what());
    }

    assert(!dispatch_data.m_is_vote);
}

// do not use for self-vote
bool ServerLobbyCommands::testVote(
        ServerLobbyCommands::DispatchData* const data, STKCommandContext* const ctx)
{
    // test whether or not the command can be voted
    assert(data->m_voted_command);
    try {
        data->m_voted_command->execute(ctx, data);
    } catch (const std::exception& e) {
        ctx->write("Unable to vote because the command cannot be executed. Check the usage and try to vote again.\nUsage: /");
        std::stringstream ss;
        data->m_voted_command->format_usage_into(ss, data->m_voted_alias);
        *ctx << ss;
        ctx->flush();
        return false;
    }
    return true;
}
void ServerLobbyCommands::VoteEntry::bakeArgline()
{
    static const char* const whitespace = " ";

    std::stringstream res;

    assert(m_voted_command.get());

    auto cur_signature_it = m_voted_command->get_arg_iter();
    bool opt = false;
    bool nfirst = false;

    std::string full;
    for (;;)
    {
        if (!opt && cur_signature_it.first == cur_signature_it.second)
        {
            if (opt) break;
            cur_signature_it = m_voted_command->get_optarg_iter();
            opt = true;
        }
        if (opt && (cur_signature_it.first == cur_signature_it.second))
            break;

        switch(cur_signature_it.first->m_type)
        {
            case nnwcli::CT_STRING:
            case nnwcli::CT_STRING_CUSTOM:
            {
                std::string arg;
                *m_voted_args >> arg;
                const bool has_whitespace = arg.find(' ') != arg.npos;
                const bool has_double_quote = arg.find('"') != arg.npos;
                const bool has_single_quote = arg.find('\'') != arg.npos;
                if (has_single_quote && has_double_quote)
                    // ideally they need to be escaped, but that's not an option here.
                    arg = "\"\"" + arg + "\"\"";
                else if ((has_whitespace && !has_double_quote) || has_single_quote)
                    arg = "\"" + arg + "\"";
                else if ((has_whitespace && !has_single_quote) || has_double_quote)
                    arg = "'" + arg + "'";
                if (!arg.empty() && nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_BOOL:
            {
                bool arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                if (arg)
                    res << "on";
                else
                    res << "off";
                break;
            }

            // the rest of them can be passed as is
            case nnwcli::CT_FLOAT:
            {
                float arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_DOUBLE:
            {
                double arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_INTEGER:
            {
                int arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_BIGINT:
            {
                long arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_SHORTINT:
            {
                short arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_TINYINT:
            {
                char arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_UINTEGER:
            {
                unsigned int arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_UBIGINT:
            {
                unsigned long arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_USHORTINT:
            {
                unsigned short arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_UTINYINT:
            {
                unsigned char arg;
                *m_voted_args >> arg;
                if (nfirst)
                    res << whitespace;
                res << arg;
                break;
            }
            case nnwcli::CT_FULL:
            {
                m_voted_args->parse_full(full);
                if (nfirst && !full.empty())
                    res << whitespace;
                res << full;
                break;
            }
        }
        if (m_voted_args->exhausted())
            break;

        nfirst = true;
        cur_signature_it.first++;
    }
    m_voted_argline = res.str();
    // When finished, bring the parser back to its original state.
    m_voted_args->reset_pos();
    m_voted_args->reset_argument_pos();
}

void ServerLobbyCommands::submitVote(ServerLobby* const lobby, const std::string username, STKCommandContext* const ctx, VoteEntry& entry)
{
    // TODO?
    // create a new vote entry or add to an already existing one
    auto entry_iterator = m_command_votes.find(entry);
    if (entry_iterator == m_command_votes.cend())
    {
        // entry does not exist, paste it right into the spot where it is supposed to be
        entry_iterator = m_command_votes.insert(entry_iterator, {entry, std::set<std::string>{username}});
    }
    else
    {
        // already exists, adding extra voter into the list, if possible
        auto username_iterator = entry_iterator->second.find(username);
        if (username_iterator != entry_iterator->second.cend())
        {
            // this command has already been voted by the same player
            ctx->write("You already voted for that command.");
            ctx->flush();
            return;
        }
        // voter added onto the existing vote
        entry_iterator->second.insert(username_iterator, username);
    }

    const unsigned int min_required = STKHost::get()->getPeerCount() / 2 + 1;

    std::string msg;
    if (entry.m_voted_argline.empty())
    {
        msg = StringUtils::insertValues("%s voted for command \"/%s\" (%d out of %d votes)", username.c_str(),
                entry.m_voted_command->get_name().c_str(),
                entry_iterator->second.size(),
                min_required);
    }
    else
    {
        msg = StringUtils::insertValues("%s voted for command \"/%s %s\" (%d out of %d votes)", username.c_str(),
                entry.m_voted_command->get_name().c_str(),
                entry.m_voted_argline.c_str(),
                entry_iterator->second.size(),
                min_required);
    }

    lobby->sendStringToAllPeers(msg);

    applyVoteIfPresent(lobby);
}
void ServerLobbyCommands::onPeerJoin(ServerLobby* const lobby, const std::shared_ptr<STKPeer> peer)
{
    // vote hooks? but for now do nothing
}
void ServerLobbyCommands::onPeerLeave(ServerLobby* const lobby, STKPeer* const peer)
{
    if (peer->hasPlayerProfiles())
    {
        const std::string username = StringUtils::wideToUtf8(peer->getPlayerProfiles()[0]->getName());
        // vote hooks: delete the voter
        resetVotesFor(lobby, username);
    }
    // apply voted commands if there is any
    applyVoteIfPresent(lobby);

    // when last player leaves, the votings are always empty
}
void ServerLobbyCommands::resetVotesFor(ServerLobby* const lobby, const std::string username)
{
    for (auto entry_iterator = m_command_votes.begin();
         entry_iterator != m_command_votes.end();)
    {
        entry_iterator->second.erase(username);
        if (entry_iterator->second.empty())
        {
            entry_iterator = m_command_votes.erase(entry_iterator);
        }
        else
            entry_iterator++;
    }
}
void ServerLobbyCommands::resetCommandVotesFor(
        const std::string command_name, const std::string argline)
{
    for (auto vote = m_command_votes.begin();
            vote != m_command_votes.end();)
    {
        if (vote->first.m_voted_command->get_name() != command_name ||
                (!argline.empty() && vote->first.m_voted_argline != argline))
        {
            vote++;
            continue;
        }
        // delete the vote entry
        vote = m_command_votes.erase(vote);
    }
}
void ServerLobbyCommands::clearAllVotes()
{
    m_command_votes.clear();
}
void ServerLobbyCommands::applyVoteIfPresent(ServerLobby* lobby)
{
    // if the amount of votes is equal to the amount of hosts on the server, apply the vote
    const unsigned int min_required = STKHost::get()->getPeerCount() / 2 + 1;

    if (m_command_votes.empty())
        // no change
        return;

    // find the vote entry of the amount not less than half of the num_peers
    for (auto vote = m_command_votes.begin();
            vote != m_command_votes.end();)
    {
        if (vote->second.size() < min_required)
        {
            // keep that vote, it did not get filled in
            vote++;
            continue;
        }
        
        // send the command to the vote and erase the entry
        dispatchVotedCommand(lobby, vote->first.m_voted_command, vote->first.m_voted_args,
                vote->first.m_voted_argline);
        // Erase the command
        vote = m_command_votes.erase(vote);
        // reset voting
        m_command_votes.clear();
        break;
    }
}
void ServerLobbyCommands::dispatchVotedCommand(ServerLobby* const lobby, std::shared_ptr<nnwcli::Command> cmd, std::shared_ptr<nnwcli::AbstractParser> args, const std::string argline)
{
    // most code copied from the dispatch_line, but uses preset command and its parser
    std::shared_ptr<STKCommandContext> const ctx = NetworkConsole::network_console_context;

    ctx->set_executor(&m_executor);
    ctx->set_lobby_commands(this);
    ctx->set_lobby(lobby);
    ctx->set_command(cmd->get_name(), cmd);
    ctx->set_parser(args);

    // run the command as network console

    try {
        DispatchData dispatch_data;
        cmd->execute(ctx.get(), &dispatch_data);
    } catch (std::exception& e) {
        Log::error(LOGNAME, "Could not execute voted-in command \"%s\" as network console with arguments \"%s\". %s",
                cmd->get_name().c_str(), argline.c_str(), e.what());
        std::string msg = "Could not execute the voted-in command for some reason. Contact the server administrator.";
        lobby->sendStringToAllPeers(msg);
    }
}
