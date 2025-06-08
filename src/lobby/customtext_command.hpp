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

#ifndef LOBBY_CUSTOMTEXT_COMMAND_HPP
#define LOBBY_CUSTOMTEXT_COMMAND_HPP

#include "argument.hpp"
#include "lobby/stk_command.hpp"
#include "nlohmann/json_fwd.hpp"
#include "utils/cpp2011.hpp"
#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

class CustomtextCommandBase : public STKCommand
{
public:
    struct ChapterNode
    {
        bool m_paged = false; // use paging for this one
        std::vector<std::string> m_text_pages;
        std::map<std::string, std::unique_ptr<ChapterNode>>
            m_chapters;
    };
private:
    ChapterNode m_chapter_root = {};

    bool m_paged = false; // at least one of the chapter nodes in the tree has paging
    // do something about it if required
    unsigned int m_line_maxlen = 60;
    unsigned int m_lines_per_page = 7;
    bool m_wordwrap = true; // when false, it interrupts the word into the next page
    // is set as the last argument
    static nnwcli::ArgumentDefinition m_paged_optarg;

    unsigned int processJsonRecursive(const nlohmann::json& node, std::map<std::string, std::unique_ptr<ChapterNode>>& target, unsigned int depth_limit);
public:
    explicit CustomtextCommandBase() = delete;
    explicit CustomtextCommandBase(CustomtextCommandBase&) = delete;
    explicit CustomtextCommandBase(CustomtextCommandBase&&) = delete;

    CustomtextCommandBase(
            const std::string name,
            const std::string desc) : STKCommand(false)
    {
        m_name = name;
        m_description = desc;
        m_args = {};
    }

    const ChapterNode& getChapterNode();
    void setChapterContent(const nlohmann::json& node_root);
    void setText(const std::string text);
    void setOptargs();
    void setOptargs(const nlohmann::json& chapter_label_array);

    virtual bool execute(nnwcli::CommandExecutorContext* context, void* data) OVERRIDE;
};

#endif // LOBBY_CUSTOMTEXT_COMMAND_HPP
