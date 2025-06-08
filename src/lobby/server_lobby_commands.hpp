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

#ifndef SERVER_LOBBY_COMMANDS_HPP
#define SERVER_LOBBY_COMMANDS_HPP

#include "command.hpp"
#include "lobby/customtext_manager.hpp"
#include "parser/abstract_parser.hpp"
#include <cassert>
#include "command_executor.hpp"
#include <string>
#include <map>
#include <set>

class STKPeer;
class ServerLobby;
class STKCommand;
class STKCommandContext;

class ServerLobbyCommands
{
    nnwcli::CommandExecutor m_executor;
    CommandCustomtextManager m_customtext_manager;

    static ServerLobbyCommands* g_instance;
public:

    struct DispatchData
    {
        // Sometimes commands need to access the shared pointer to the peer.
        std::weak_ptr<STKPeer> m_peer_wkptr;
        // the next dispatch call is a vote-test
        bool m_is_vote = false;
        // when m_is_vote is true, STK Command needs to determine whether or not this command can be voted in
        // by default it is marked as unvotable
        bool m_can_vote = false;
        bool m_vote_positive = true; // when false, vote is considered negative
        std::shared_ptr<nnwcli::Command> m_voted_command; // the command that is voted in, can be a self-voted command
        std::shared_ptr<nnwcli::AbstractParser> m_voted_args; // when self-voting, parser's position should be reset
        std::string m_voted_argline; // when empty, the recent command is marked as voted,
        std::string m_voted_alias;   // this string is for the replacement
    };

    struct VoteEntry
    {
        std::shared_ptr<nnwcli::Command> m_voted_command;
        std::shared_ptr<nnwcli::AbstractParser> m_voted_args;
        std::string m_voted_argline; // only for visuals and index

        // for std::map
        bool operator< (const VoteEntry& other) const;
        bool operator== (const VoteEntry& other) const;

        // internals
        // bakeArgline creates a deterministic, fixed
        // argument line by reverse-engineering the
        // output of the abstract parser by following the
        // command signature.
        // This alleviates an issue of the same command being voted
        // while the argument line is a synonym, creating duplicates
        // in the vote entries. Such as: /pole on and /pole yes
        // refers to the same command and the same argument value
        void bakeArgline();
    };
    std::map<VoteEntry /*command real name (not an alias)*/,
             std::set<std::string> /*voter names*/> m_command_votes;
    // contains a reverse list of votings per player
    // std::map<std::string, std::set<VoteEntry>> m_command_votes_rindex;

private:

    // Test if the command in the DispatchData can be voted. Cannot be used for self-votes.
    bool testVote(DispatchData* data, STKCommandContext* ctx);
public:
    ServerLobbyCommands();
    ~ServerLobbyCommands();

    static void create();
    static void destroy();
    [[nodiscard]] static ServerLobbyCommands* get() { return g_instance; }
    static std::shared_ptr<STKCommandContext>& getNetworkConsoleContext();
    static CommandCustomtextManager* getCustomtextManager()
    {
        assert(g_instance);
        return &g_instance->m_customtext_manager;
    };
    //------------------------------------------------------------
    void registerCommands();
    void handleServerCommand(ServerLobby* lobby, std::shared_ptr<STKPeer>& peer, std::string& line);
    void handleNetworkConsoleCommand(std::string& line);
    //------------------------------------------------------------
    // should the voting system be separate from this code? At some point. But not right now.
    void submitVote(ServerLobby* lobby, const std::string username, STKCommandContext* ctx, VoteEntry& entry); // or submit the vote. Runs by the separate command.
    void resetVotesFor(ServerLobby* lobby, const std::string username);
    void resetCommandVotesFor(const std::string command_name, const std::string argline = "");
    void clearAllVotes();
    void applyVoteIfPresent(ServerLobby* lobby);
    // run the command with a network console context, primarily used for voted commands
    void dispatchVotedCommand(ServerLobby* lobby, std::shared_ptr<nnwcli::Command> cmd,
            std::shared_ptr<nnwcli::AbstractParser> args,
            const std::string argline);
    //------------------------------------------------------------
    // Server Lobby Hooks
    void onPeerJoin(ServerLobby* lobby, std::shared_ptr<STKPeer> peer);
    void onPeerLeave(ServerLobby* lobby, STKPeer* peer);
    //------------------------------------------------------------
};

#endif // SERVER_LOBBY_COMMANDS_HPP
