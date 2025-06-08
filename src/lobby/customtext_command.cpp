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

#include "customtext_command.hpp"
#include <algorithm>
#include <context.hpp>
#include "argument_types.hpp"
#include "lobby/stk_command.hpp"
#include "utils/log.hpp"
#include "stk_command_context.hpp"
#include <stdexcept>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <cassert>
#include <cctype>

nnwcli::ArgumentDefinition CustomtextCommandBase::m_paged_optarg = {nnwcli::CT_UINTEGER, "page", "Page number, from 1"};
bool CustomtextCommandBase::execute(nnwcli::CommandExecutorContext* const ctx, void* const data)
{
    STK_CTX(stk_ctx, ctx);
    
    auto parser = ctx->get_parser();

    unsigned int page = 1;
    ChapterNode* node = &m_chapter_root;  // the target node to get.

    for (auto& arg : m_optargs)
    {
        std::string chapter_name;
        // try to get the page number at first
        try
        {
            // that's the page number
            parser->parse_unsigned_integer(page, false);
            if (!page)
                page = 1;

            break;
        }
        catch (const std::invalid_argument& e)
        {
            if (arg.m_type == nnwcli::CT_UINTEGER)
                throw e; // there can't be another chapter,
                         // the type error is valid
        } // otherwise chapter is specified

        if (!parser->parse_string(chapter_name, false))
            break;

        // try to traverse
        auto next = node->m_chapters.find(chapter_name);
        if (next == node->m_chapters.cend())
        {
            ctx->write("Unknown chapter: ");
            ctx->write(chapter_name);
            ctx->flush();
            return false;
        }
        node = next->second.get();
    }

    parser->parse_finish();

    const size_t max_page = node->m_text_pages.size();
    const bool use_paging = max_page > 1;

    if (use_paging)
    {
        if (page > max_page)
            page = max_page;
        ctx->nprintf("--- Page %u of %zu ---", 128,
                page, max_page);
    }

    if (!max_page)
    {
        ctx->write("There's nothing to show here.");
        if (node->m_chapters.size())
        {
            ctx->write(" Did you forget to specify the chapter?\n");
            format_usage_into(stk_ctx->get_response_buffer(), ctx->get_alias());
        }
        ctx->flush();
        return false;
    }
    std::string& page_content = node->m_text_pages[page - 1];
    ctx->write(page_content);

    if (use_paging)
    {
        ctx->write("---");
    }

    ctx->flush();
    return true;
}

void CustomtextCommandBase::setOptargs()
{
    if (!m_optargs.empty() && m_optargs[m_optargs.size() - 1].m_type == nnwcli::CT_UINTEGER)
        return;

    if (m_paged)
        m_optargs.push_back(m_paged_optarg);
}
void CustomtextCommandBase::setOptargs(const nlohmann::json& chapter_label_array)
{
    static const std::string chapter_desc = "Predefined chapter name.";

    m_optargs.clear();

    for (auto it = chapter_label_array.cbegin();
            it != chapter_label_array.cend();
            it++)
    {
        std::string name = it->get<std::string>();
        m_optargs.push_back({nnwcli::CT_STRING, name, chapter_desc});
    }
    setOptargs();
}

static
std::size_t find_end_prevword(
        const std::string& text,
        std::size_t index)
{
    // first whitespace
    while (text[index] != ' ')
    {
        if (!index) return 0U;
        index--;
    }
    // first non-whitespace
    while (text[index] == ' ')
    {
        if (!index) return 0U;
        index--;
    }
    return index + 1;
}

/** Iterates throughout the entire text and creates paged text.*/
static
void split_pages(
    const std::string& text,
    std::vector<std::string>& out,
    const unsigned int lines_per_page,
    const unsigned int line_maxlen,
    const bool word_wrap = true)
{
    if (text.empty())
        return;

    std::size_t p = 0;
    std::size_t l;
    // process one page
    while (p < text.size())
    {
        l = 0;
        std::ostringstream line;
        do {
            while (text[p] == ' ')
                p++;
            // create two marks: line_break and line_limit
            std::size_t line_break = text.find('\n', p);
            std::size_t line_limit = std::min(p + (std::size_t) line_maxlen, text.size());
            bool has_line_break = line_break != text.npos;
            bool has_line_limit = line_limit < text.size();

            if (word_wrap && has_line_limit)
            {
                // move line limit to the end of the previous word
                line_limit = find_end_prevword(text, line_limit);
            }

            std::size_t end;
            if (has_line_break)
                end = std::min(line_break, line_limit);
            else
                end = line_limit;

            if (end < text.size())
            {
                while (text[end - 1] == ' ' || text[end - 1] == '\n')
                    end--;
            }

            if (l)
                line << std::endl;
            line << text.substr(p, end - p);
            l++;
            p = end;
        } while (p < text.size() && l < lines_per_page);

        out.push_back(line.str());
    }
}
void CustomtextCommandBase::setText(const std::string text)
{
    split_pages(
            text,
            m_chapter_root.m_text_pages,
            m_lines_per_page,
            m_line_maxlen);
    m_paged |= m_chapter_root.m_text_pages.size() > 1;
}
unsigned int CustomtextCommandBase::processJsonRecursive(
        const nlohmann::json& chapters_node,
        std::map<std::string, std::unique_ptr<ChapterNode>>& target,
        const unsigned int depth_limit)
{
    unsigned int ret_depth_limit = depth_limit;
    // the received node is the chapters mapping
    for (auto chapter_it = chapters_node.cbegin();
            chapter_it != chapters_node.cend();
            chapter_it++)
    {
        const std::string chapter_name = chapter_it.key();

        ChapterNode* chapter = new ChapterNode;
        target.insert({chapter_name, std::unique_ptr<ChapterNode>(chapter)});
        
        auto text_it = chapter_it->find("text");
        auto chapters_it = chapter_it->find("chapters");

        // sets the text for this node
        if (text_it != chapter_it->cend())
        {
            std::string text = text_it->get<std::string>();
            split_pages(
                text,
                chapter->m_text_pages, m_lines_per_page, m_line_maxlen);

            chapter->m_paged = chapter->m_text_pages.size() > 1;
            m_paged |= chapter->m_paged;
        }

        if (chapters_it != chapter_it->cend() && depth_limit)
        {
            unsigned int res = processJsonRecursive(
                    chapters_it->get<const nlohmann::json>(),
                    chapter->m_chapters,
                    depth_limit - 1);
            if (res < ret_depth_limit)
                ret_depth_limit = res;
        }
    }

    return ret_depth_limit;
#if 0
    // the node might not have the text, otherwise it would have subchapters
    auto text_it = node.find("text");
    auto chapters_it = node.find("chapters");
    
            for (auto chapter_it = chapters_it->cbegin();
                    chapter_it != chapters_it->cend();
                    chapter_it++)
            {
                ChapterNode* sub = new ChapterNode;
                target->m_chapters.insert({chapter_it.key(), unique_ptr<ChapterNode>(sub)});
                unsigned int res =
                    processJsonRecursive(*chapter_it, sub, depth_limit);
                if (res < depth_limit)
                    depth_limit = res;
            }
#endif
}
void CustomtextCommandBase::setChapterContent(const nlohmann::json& node_root)
{
    m_chapter_root.m_chapters.clear();
    m_chapter_root.m_text_pages.clear();

    processJsonRecursive(node_root, m_chapter_root.m_chapters, 0);
}
