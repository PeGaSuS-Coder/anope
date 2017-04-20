/*
 * Anope IRC Services
 *
 * Copyright (C) 2011-2017 Anope Team <team@anope.org>
 *
 * This file is part of Anope. Anope is free software; you can
 * redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see see <http://www.gnu.org/licenses/>.
 */

#include "module.h"
#include "modules/help.h"

class CommandHelp : public Command
{
	static CommandGroup *FindGroup(const Anope::string &name)
	{
		for (unsigned i = 0; i < Config->CommandGroups.size(); ++i)
		{
			CommandGroup &gr = Config->CommandGroups[i];
			if (gr.name == name)
				return &gr;
		}

		return NULL;
	}

 public:
	CommandHelp(Module *creator) : Command(creator, "generic/help", 0)
	{
		this->SetDesc(_("Displays this list and give information about commands"));
		this->AllowUnregistered(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		EventReturn MOD_RESULT = EventManager::Get()->Dispatch(&Event::Help::OnPreHelp, source, params);
		if (MOD_RESULT == EVENT_STOP)
			return;

		Anope::string source_command = source.GetCommand();
		const ServiceBot *bi = source.service;
		const CommandInfo::map &map = source.c ? Config->Fantasy : bi->commands;
		bool hide_privileged_commands = Config->GetBlock("options")->Get<bool>("hideprivilegedcommands"),
		     hide_registered_commands = Config->GetBlock("options")->Get<bool>("hideregisteredcommands");

		if (params.empty() || params[0].equals_ci("ALL"))
		{
			bool all = !params.empty() && params[0].equals_ci("ALL");
			typedef std::map<CommandGroup *, std::list<Anope::string> > GroupInfo;
			GroupInfo groups;

			if (all)
				source.Reply(_("All available commands for \002{0}\002:"), source.service->nick);

			for (CommandInfo::map::const_iterator it = map.begin(), it_end = map.end(); it != it_end; ++it)
			{
				const Anope::string &c_name = it->first;
				const CommandInfo &info = it->second;

				if (info.hide)
					continue;

				// Smaller command exists
				Anope::string cmd;
				spacesepstream(c_name).GetToken(cmd, 0);
				if (cmd != it->first && map.count(cmd))
					continue;

				ServiceReference<Command> c(info.name);
				if (!c)
					continue;

				if (hide_registered_commands && !c->AllowUnregistered() && !source.GetAccount())
					continue;

				if (hide_privileged_commands && !info.permission.empty() && !source.HasCommand(info.permission))
					continue;

				if (!info.group.empty() && !all)
				{
					CommandGroup *gr = FindGroup(info.group);
					if (gr != NULL)
					{
						groups[gr].push_back(c_name);
						continue;
					}
				}

				source.SetCommand(c_name);
				c->OnServHelp(source);

			}

			for (GroupInfo::iterator it = groups.begin(), it_end = groups.end(); it != it_end; ++it)
			{
				CommandGroup *gr = it->first;

				source.Reply(" ");
				source.Reply("{0}", gr->description);

				Anope::string buf;
				for (std::list<Anope::string>::iterator it2 = it->second.begin(), it2_end = it->second.end(); it2 != it2_end; ++it2)
				{
					const Anope::string &c_name = *it2;
					buf += ", " + c_name;
				}
				if (buf.length() > 2)
					source.Reply("  {0}", buf.substr(2));
			}
			if (!groups.empty())
			{
				source.Reply(" ");
				source.Reply(_("Use the \002{0} ALL\002 command to list all commands and their descriptions."), source_command);
			}
		}
		else
		{
			bool helped = false;
			for (unsigned max = params.size(); max > 0; --max)
			{
				Anope::string full_command;
				for (unsigned i = 0; i < max; ++i)
					full_command += " " + params[i];
				full_command.erase(full_command.begin());

				CommandInfo::map::const_iterator it = map.find(full_command);
				if (it == map.end())
					continue;

				const CommandInfo &info = it->second;

				ServiceReference<Command> c(info.name);
				if (!c)
					continue;

				if (hide_privileged_commands && !info.permission.empty() && !source.HasCommand(info.permission))
					continue;

				// Allow unregistered users to see help for commands that they explicitly request help for

				const Anope::string &subcommand = params.size() > max ? params[max] : "";
				source.SetCommand(it->first);

				c->SendSyntax(source);
				if (!c->OnHelp(source, subcommand))
					continue;

				helped = true;

				/* Inform the user what permission is required to use the command */
				if (!info.permission.empty())
				{
					source.Reply(" ");
					source.Reply(_("Access to this command requires the permission \002{0}\002 to be present in your opertype."), info.permission);
				}
				if (!c->AllowUnregistered() && !source.nc)
				{
					if (info.permission.empty())
						source.Reply(" ");
					source.Reply( _("You need to be identified to use this command."));
				}
				/* User doesn't have the proper permission to use this command */
				else if (!info.permission.empty() && !source.HasCommand(info.permission))
				{
					source.Reply(_("You cannot use this command."));
				}

				break;
			}

			if (helped == false)
				source.Reply(_("No help available for \002{0}\002."), params[0]);
		}

		EventManager::Get()->Dispatch(&Event::Help::OnPostHelp, source, params);
	}
};

class Help : public Module
{
	CommandHelp commandhelp;

 public:
	Help(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, VENDOR)
		, commandhelp(this)
	{

	}
};

MODULE_INIT(Help)
