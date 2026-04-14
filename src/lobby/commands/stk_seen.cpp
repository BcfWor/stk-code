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
#include <thread>
#include <memory>

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
	std::weak_ptr<STKPeer> peer_wk;
	if (data)
	{
		auto dd = reinterpret_cast<ServerLobbyCommands::DispatchData*>(data);
		peer_wk = dd->m_peer_wkptr;
	}
	std::string post_player = playername;
	ServerLobby* lobby_raw = lobby;

	std::thread([post_player, lobby_raw, peer_wk]()
	{
		std::string response_string;
		CURL* curl = curl_easy_init();
		if (!curl)
		{
			if (auto peer_locked = peer_wk.lock())
			{
				std::shared_ptr<STKPeer> peer = peer_locked;
				lobby_raw->sendStringToPeer(std::string("Error: Failed to initialize HTTP request."), peer);
			}
			return;
		}

		std::string ishigami_addr = ServerConfig::m_ishigami_address;
		std::string full_url = ishigami_addr + "/stk-seen";
		std::string post_data = "username=" + post_player;
		curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
		CURLcode res = curl_easy_perform(curl);
		long response_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		curl_easy_cleanup(curl);

		if (res != CURLE_OK || response_code != 200)
		{
			if (auto peer_locked = peer_wk.lock())
			{
				std::shared_ptr<STKPeer> peer = peer_locked;
				lobby_raw->sendStringToPeer(std::string("Error: Request failed."), peer);
			}
			return;
		}

		XMLNode* xml = file_manager->createXMLTreeFromString(response_string);
		if (!xml)
		{
			if (auto peer_locked = peer_wk.lock())
			{
				std::shared_ptr<STKPeer> peer = peer_locked;
				lobby_raw->sendStringToPeer(std::string("Error: Unexpected response from server."), peer);
			}
			return;
		}

		std::unique_ptr<XMLNode> xml_guard(xml);
		std::string rec_success;
		xml->get("success", &rec_success);
		if (rec_success != "yes")
		{
			std::string api_reason;
			xml->get("info", &api_reason);
			std::string reason;
			if (api_reason == "player_not_seen")
				reason = StringUtils::insertValues("Player %s has not been seen on any server recently.", post_player);
			else if (api_reason == "sql_error")
				reason = "SQL query failed. Please contact the administrator";
			else if (api_reason == "optout")
				reason = "This player has opted out of this feature";
			else
				reason = "Unspecified error";

			if (auto peer_locked = peer_wk.lock())
			{
				std::shared_ptr<STKPeer> peer = peer_locked;
				lobby_raw->sendStringToPeer(std::string("Failed to get player data: ") + reason, peer);
			}
			return;
		}

		irr::core::stringw username, country, server, server_country, date;
		xml->getAndDecode("username", &username);
		xml->getAndDecode("country", &country);
		xml->getAndDecode("server", &server);
		xml->getAndDecode("server-country", &server_country);
		xml->getAndDecode("date", &date);

		if (auto peer_locked = peer_wk.lock())
		{
			irr::core::stringw msg = StringUtils::insertValues(
				irr::core::stringw("Player %s (%s) was last seen on server %s (%s) at %s"),
				username, country, server, server_country, date);
			std::shared_ptr<STKPeer> peer = peer_locked;
			lobby_raw->sendStringToPeer(msg, peer);
		}
	}).detach();

	return true;
}
