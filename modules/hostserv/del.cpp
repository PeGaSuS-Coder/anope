/* HostServ core functions
 *
 * (C) 2003-2014 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

#include "module.h"
#include "modules/hostserv/del.h"

class CommandHSDel : public Command
{
 public:
	CommandHSDel(Module *creator) : Command(creator, "hostserv/del", 1, 1)
	{
		this->SetDesc(_("Delete the vhost of another user"));
		this->SetSyntax(_("\037user\037"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (Anope::ReadOnly)
			source.Reply(_("Services are in read-only mode. Any changes made may not persist."));

		const Anope::string &nick = params[0];
		NickServ::Nick *na = NickServ::FindNick(nick);
		if (!na)
		{
			source.Reply(_("\002{0}\002 isn't registered."), nick);
			return;
		}

		Log(LOG_ADMIN, source, this) << "for user " << na->GetNick();
		EventManager::Get()->Dispatch(&Event::DeleteVhost::OnDeleteVhost, na);
		na->RemoveVhost();
		source.Reply(_("Vhost for \002{0}\002 has been removed."), na->GetNick());
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) override
	{
		source.Reply(_("Removes the vhost of \037user\037."));
		return true;
	}
};

class CommandHSDelAll : public Command
{
 public:
	CommandHSDelAll(Module *creator) : Command(creator, "hostserv/delall", 1, 1)
	{
		this->SetDesc(_("Delete the vhost for all nicks in a group"));
		this->SetSyntax(_("\037group\037"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) override
	{
		if (Anope::ReadOnly)
			source.Reply(_("Services are in read-only mode. Any changes made may not persist."));

		const Anope::string &nick = params[0];
		NickServ::Nick *na = NickServ::FindNick(nick);
		if (!na)
		{
			source.Reply(_("\002{0}\002 isn't registered."), nick);
			return;
		}

		EventManager::Get()->Dispatch(&Event::DeleteVhost::OnDeleteVhost, na);

		NickServ::Account *nc = na->GetAccount();
		for (NickServ::Nick *na2 : nc->GetRefs<NickServ::Nick *>())
			na2->RemoveVhost();
		Log(LOG_ADMIN, source, this) << "for all nicks in group " << nc->GetDisplay();
		source.Reply(_("Vhosts for group \002{0}\002 have been removed."), nc->GetDisplay());
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) override
	{
		source.Reply(_("Removes the vhost of all nicks in the group \037group\037."));
		return true;
	}
};

class HSDel : public Module
{
	CommandHSDel commandhsdel;
	CommandHSDelAll commandhsdelall;

 public:
	HSDel(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, VENDOR)
		, commandhsdel(this)
		, commandhsdelall(this)
	{
		if (!IRCD || !IRCD->CanSetVHost)
			throw ModuleException("Your IRCd does not support vhosts");
	}
};

MODULE_INIT(HSDel)