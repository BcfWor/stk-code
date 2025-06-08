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

#include "customtext_manager.hpp"
#include "command_executor.hpp"
#include "network/server_config.hpp"
#include "utils/file_utils.hpp"
#include "utils/log.hpp"
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

static const char* LOGNAME = "CommandCustomtextManager";

CommandCustomtextManager::CommandCustomtextManager(
        nnwcli::CommandExecutor* const executor
        ) : m_executor(executor)
{
    // load commands from the file
    loadCommands(ServerConfig::m_customtext_commands);
}

CommandCustomtextManager::~CommandCustomtextManager()
{
    unregisterCommands();
}

/** Example JSON data: 
 *  {
 *      "command_name": {"desc": "Shows something", "aliases": ["cmdname", "cmd-name"], "text": "Sample text"},
 *      "command2": {
 *          "desc": "Show long text",
 *          "aliases": ["ltext"]
 *          "text": "Hello world,\nThis is a multiline text\nIt will be paged or it will be not.\nIt only depends whether or not pages are enabled."
 *      }
 *  }*/

void CommandCustomtextManager::unregisterCommands()
{
    for (auto cmd : m_cmds)
    {
        m_executor->unregister_command(cmd->get_name());
    }

    if (m_cmds.size())
    {
        Log::info(LOGNAME, "Unregistered %d customtext commands.", m_cmds.size());
        m_cmds.clear();
    }
}
bool CommandCustomtextManager::loadCommands(const std::string path)
{
    auto rpath = FileUtils::getPortableReadingPath(path);
    std::ifstream input_file(rpath);
    if (!input_file.is_open())
    {
        Log::info(LOGNAME, "File \"%s\" does not exist, won't load customtext commands. "
                "If you need customtext commands, please create that file in the server's working directory. "
                "See example in customtext_example.json"
                , rpath.c_str());
        return false;
    }
    
    nlohmann::json data;
    try
    {
        data = nlohmann::json::parse(input_file);
    }
    catch (const nlohmann::json::exception& e)
    {
        Log::error(LOGNAME, "Syntax error in the customtext file: %s", e.what());
        return false;
    }

    unregisterCommands();

    for (auto entry = data.cbegin(); entry != data.cend(); entry++)
    {
        // command name is a key, while its parameters is in the value
        const std::string& cmd_name = entry.key();
        const nlohmann::json& cmd_data = entry.value();
        auto desc_it = cmd_data.find("desc");
        auto aliases_it = cmd_data.find("aliases");
        auto chapter_labels_it = cmd_data.find("chapter_labels");
        auto text_it = cmd_data.find("text");
        auto chapters_it = cmd_data.find("chapters");
        try
        {
            m_executor->get_command(cmd_name);
            
            // error expected, cannot continue
            Log::error(LOGNAME, "Command or alias %s already exists, cannot register.",
                    cmd_name.c_str());
            continue;
        }
        catch(const nnwcli::command_not_found& e) {}
        
        auto cmd = std::make_shared<CustomtextCommandBase>(
                cmd_name,
                desc_it == cmd_data.cend() ?
                    std::string("Gives information about this topic")
                    : desc_it->get<std::string>());

        if (chapter_labels_it != cmd_data.cend())
            cmd->setOptargs(chapter_labels_it.value());
        else
            cmd->setOptargs();
        if (text_it != cmd_data.cend())
            cmd->setText(text_it.value());

        if (chapters_it != cmd_data.cend())
            cmd->setChapterContent(chapters_it.value());
        else
            Log::verbose("CustomtextManager", "Command %s has no chapters.", cmd_name.c_str());

        m_cmds.insert(cmd);

        // register the command and register the aliases
        m_executor->register_command(cmd);

        if (aliases_it != cmd_data.cend())
        {
            const std::vector<std::string>& aliases = *aliases_it;
            for (const std::string& alias : aliases)
            {
                m_executor->add_alias(alias, cmd_name);
            }
        }
    }

    Log::info(LOGNAME, "%d customtext commands registered.", m_cmds.size());
    return true;
}
