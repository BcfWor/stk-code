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
#include "io/file_manager.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/server_config.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <curl/curl.h>
#include <string>

// ========================================================================

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool StkSeenCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    std::string playername;

    *parser >> playername;
    parser->parse_finish();

    if (playername.length() < 3)
    {
	ctx->write("Player name must be at least 3 characters long");
	ctx->flush();
	return false;
    }

    ctx->write("Checking player data...");
    ctx->flush();

    CURL *curl;
    CURLcode res;
    std::string response;
    curl = curl_easy_init();

    if (!curl)
    {
        ctx->write("Error: Failed to initialize HTTP request.\n");
        ctx->flush();
        return false;
    }

    std::string post_data = "username=" + playername;
    std::string ishigami_addr = ServerConfig::m_ishigami_address;
    std::string full_url = ishigami_addr + "/stk-seen";
    std::string response_string;
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
	ctx->nprintf("Error: Request failed: %s\n", 512, curl_easy_strerror(res));
	curl_easy_cleanup(curl);
	ctx->flush();
	return false;
    }

    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    XMLNode *xml;
    xml = file_manager->createXMLTreeFromString(response_string);

    if (!xml)
    {
	ctx->write("Error: Unexpected response from server.");
	ctx->flush();
	return false;
    }

    std::string rec_success;
    bool m_success;
    m_success = false;

    xml->get("success", &rec_success);
    m_success = (rec_success == "yes");

    if (!m_success)
    {
	std::string api_reason;
        std::string reason;

        xml->get("info", &api_reason);
            
        if (api_reason == "player_not_seen")
        {
            reason = StringUtils::insertValues("Player %s has not been seen on any server recently.", playername);
        }
        else if (api_reason == "sql_error")
        {
            reason = "SQL query failed. Please contact the administrator";
        }
	else if (api_reason == "optout")
	{
	    reason = "This player has opted out of this feature";
	}
        else
        {
            reason = "Unspecified error";
        }

	ctx->write("Failed to get player data: ");
        ctx->write(reason);
        ctx->flush();
	return false;
    }

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
    return true;
}
