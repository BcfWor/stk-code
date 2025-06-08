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

#include "stk_seen.hpp"
#include "io/xml_node.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/server_config.hpp"
#include "online/xml_request.hpp"
#include "online/request_manager.hpp"
#include <parser/argline_parser.hpp>
#include <string>

// ========================================================================

class STKSeenRequest : public Online::XMLRequest {
    private:
    std::string addr = ServerConfig::m_ishigami_address;
    public:

    STKSeenRequest(const std::string& username) : XMLRequest(Online::RequestManager::HTTP_MAX_PRIORITY) {
        setURL(addr + "/stk-seen");
        addParameter("username", username);
    };
    virtual void afterOperation() {
        Online::XMLRequest::afterOperation();
        const XMLNode* result = getXMLData();

        if (!isSuccess()) {
            Log::error("Ishigami", "Failed to get the STK Seen data.");
        };
    }
};

bool StkSeenCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    std::string playername;

    *parser >> playername;
    parser->parse_finish();

    ctx->write("Checking player data...");
    ctx->flush();

    // Note that if the context does not survive up to this point,
    // this will result in an undefined behavior, since
    // the command can receive a temporary pointer
    std::thread([&]() {
        auto request = std::make_shared<STKSeenRequest>(playername);
        Online::RequestManager::get()->addRequest(request);

        while (!request->isDone())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const XMLNode* xml = request->getXMLData();
        if (request->isSuccess())
        {
            if (xml)
            {
                std::string username, country, server, server_country, date;
                xml->get("username", &username);
                xml->get("country", &country);
                xml->get("server", &server);
                xml->get("server-country", &server_country);
                xml->get("date", &date);

                ctx->nprintf("Player %s (%s) was last seen on server %s (%s) at %s", 512,
                    username.c_str(), country.c_str(), server.c_str(),
                    server_country.c_str(), date.c_str());
                ctx->flush();
            }
        }
        else
        {
            std::string api_reason;
            std::string reason;

            if (xml)
                xml->get("info", &api_reason);
            
            if (api_reason == "player_not_seen")
            {
                reason = StringUtils::insertValues("Player %s has not been seen on any server recently.", playername);
            }
            else if (api_reason == "name_too_short")
            {
                reason = "Username must be at least 3 characters";
            }
            else if (api_reason == "sql_error")
            {
                reason = "SQL query failed. Please contact the administrator";
            }
            else
            {
                reason = "Unspecified error";
            }

            ctx->write("Failed to get player data: ");
            ctx->write(reason);
            ctx->flush();
        }
    }).detach();

    return true;
}
