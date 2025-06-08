
#ifndef LOBBY_COMMAND_HELPOF_HPP
#define LOBBY_COMMAND_HELPOF_HPP

#include "lobby/stk_command.hpp"
#include "command_executor.hpp"


class HelpOfCommand : public STKCommand
{
public:
    HelpOfCommand() : STKCommand(false)
    {
        m_name = "helpof";
        m_args = {{nnwcli::CT_STRING, "command", "Command to show the info of."}};
        m_description = "Show help for a specified command.";
    }

    void write_aliases(
            std::ostream& stream,
            std::map<std::string, std::shared_ptr<Command>>::const_iterator& start,
            std::map<std::string, std::shared_ptr<Command>>::const_iterator& end,
            Command* const cmd)
    {
        bool changed = false;
        for(; start != end; start++)
        {
            if(start->second.get() != cmd)
            {
                continue;
            }
            if(changed)
                stream << ", ";

            stream << start->first;
            changed = true;
        }
        if(!changed)
            stream << "(none)";
    }

    void write_arguments(
            std::ostream& stream,
            std::size_t count,
            std::vector<nnwcli::ArgumentDefinition>::const_iterator& start,
            std::vector<nnwcli::ArgumentDefinition>::const_iterator& end)
    {
        const char* type_name;
        if(count)
        {
            while(start != end)
            {
                type_name = nnwcli::argtype_to_name(start->m_type);
                stream << " - " << start->m_name << " (" << type_name << "): " << start->m_description;

                if(++start != end)
                    stream << std::endl;
            }
        }
        else
        {
            stream << "(none)";
        }
    }

    virtual bool execute(nnwcli::CommandExecutorContext* const context, void* const data) override
    {
        std::string cmdname;
        std::stringstream ss;
        nnwcli::AbstractParser* const parser = context->get_parser().get();
        nnwcli::CommandExecutor* const executor = context->get_executor();

        *parser >> cmdname;
        // get the command first
        std::shared_ptr<Command> cmd;
        try
        {
            cmd = executor->get_command(cmdname);
        } 
        catch (const nnwcli::command_not_found& e)
        {
            ss << "Command \"" << cmdname << "\" not found." << std::endl;
            context->write(ss.str());
            context->flush();
            return false;
        }
        ss << "Description: " << cmd->get_description() << std::endl;
        auto alias_it = executor->get_alias_iter();
        ss << "Aliases: ";
        write_aliases(ss, alias_it.first, alias_it.second, cmd.get());
        ss << std::endl;

        cmd->format_usage_into(ss, cmdname);
        ss << std::endl;

        // describe every argument
        auto arg_it = cmd->get_arg_iter();
        ss << "Arguments:" << std::endl;
        write_arguments(ss, cmd->get_args_count(), arg_it.first, arg_it.second);
        ss << std::endl << "Optional arguments:" << std::endl;
        auto optarg_it = cmd->get_optarg_iter();
        write_arguments(ss, cmd->get_optargs_count(), optarg_it.first, optarg_it.second);
        *context << ss;
        context->flush();
        
        return true;
    }
};
#endif // LOBBY_COMMAND_HELPOF_HPP
