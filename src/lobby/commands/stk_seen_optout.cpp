//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2013-2015 SuperTuxKart-Team
//  Copyright (C) 2025 the linaSTK authors (https://codeberg.org/linaSTK)
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

#include "stk_seen_optout.hpp"
#include "io/xml_node.hpp"
#include "io/file_manager.hpp"
#include "lobby/server_lobby_commands.hpp"
#include "lobby/stk_command.hpp"
#include "lobby/stk_command_context.hpp"
#include "network/protocols/server_lobby.hpp"
#include "network/stk_host.hpp"
#include "network/stk_peer.hpp"
#include "network/server_config.hpp"
#include "network/protocols/server_lobby.hpp"
#include "utils/string_utils.hpp"
#include <parser/argline_parser.hpp>
#include <curl/curl.h>
#include <string>

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool StkSeenOptOutCommand::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);

    auto parser = ctx->get_parser();
    parser->parse_finish();

    ServerLobby* const lobby = stk_ctx->get_lobby();
    if (!lobby) return false;

    STKPeer* player_peer = stk_ctx->get_peer();
    if (!player_peer || !player_peer->hasPlayerProfiles())
    {
        ctx->write("Error: Could not get player information.\n");
        ctx->flush();
        return false;
    }
    std::string player_name = StringUtils::wideToUtf8(player_peer->getPlayerProfiles()[0]->getName());

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

    std::string post_data = "username=" + player_name;
    std::string ishigami_addr = ServerConfig::m_ishigami_address;
    std::string full_url = ishigami_addr + "/stk-seen-opt-toggle";
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

    std::string reason;
    xml->get("info", &reason);

    if (!m_success)
    {
	ctx->nprintf("Failed to opt in/out of STK seen: %s", 512, reason);
	ctx->flush();
	return false;
    }

    ctx->write(reason);
    ctx->flush();

    return true;
}
