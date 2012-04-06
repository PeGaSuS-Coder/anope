/* OperServ core functions
 *
 * (C) 2003-2012 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

/*************************************************************************/

#include "module.h"

static service_reference<XLineManager> akills("XLineManager", "xlinemanager/sgline");

class AkillDelCallback : public NumberList
{
	CommandSource &source;
	unsigned Deleted;
 public:
	AkillDelCallback(CommandSource &_source, const Anope::string &numlist) : NumberList(numlist, true), source(_source), Deleted(0)
	{
	}

	~AkillDelCallback()
	{
		if (!Deleted)
			source.Reply(_("No matching entries on the AKILL list."));
		else if (Deleted == 1)
			source.Reply(_("Deleted 1 entry from the AKILL list."));
		else
			source.Reply(_("Deleted %d entries from the AKILL list."), Deleted);
	}

	void HandleNumber(unsigned Number) anope_override
	{
		if (!Number)
			return;

		XLine *x = akills->GetEntry(Number - 1);

		if (!x)
			return;

		++Deleted;
		DoDel(source, x);
	}

	static void DoDel(CommandSource &source, XLine *x)
	{
		akills->DelXLine(x);
	}
};

class CommandOSAKill : public Command
{
 private:
	void DoAdd(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		Anope::string expiry, mask;

		if (params.size() < 2)
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		spacesepstream sep(params[1]);
		sep.GetToken(mask);

		if (mask[0] == '+')
		{
			expiry = mask;
			sep.GetToken(mask);
		}

		time_t expires = !expiry.empty() ? dotime(expiry) : Config->AutokillExpiry;
		/* If the expiry given does not contain a final letter, it's in days,
		 * said the doc. Ah well.
		 */
		if (!expiry.empty() && isdigit(expiry[expiry.length() - 1]))
			expires *= 86400;
		/* Do not allow less than a minute expiry time */
		if (expires && expires < 60)
		{
			source.Reply(BAD_EXPIRY_TIME);
			return;
		}
		else if (expires > 0)
			expires += Anope::CurTime;

		if (sep.StreamEnd())
		{
			this->OnSyntaxError(source, "ADD");
			return;
		}

		Anope::string reason;
		if (mask.find('#') != Anope::string::npos)
		{
			Anope::string remaining = sep.GetRemaining();

			size_t co = remaining[0] == ':' ? 0 : remaining.rfind(" :");
			if (co == Anope::string::npos)
			{
				this->OnSyntaxError(source, "ADD");
				return;
			}

			if (co != 0)
				++co;

			reason = remaining.substr(co + 1);
			mask += " " + remaining.substr(0, co);
			mask.trim();
		}
		else
			reason = sep.GetRemaining();

		if (mask[0] == '/' && mask[mask.length() - 1] == '/')
		{
			if (Config->RegexEngine.empty())
			{
				source.Reply(_("Regex is enabled."));
				return;
			}

			service_reference<RegexProvider> provider("Regex", Config->RegexEngine);
			if (!provider)
			{
				source.Reply(_("Unable to find regex engine %s"), Config->RegexEngine.c_str());
				return;
			}

			try
			{
				Anope::string stripped_mask = mask.substr(1, mask.length() - 2);
				delete provider->Compile(stripped_mask);
			}
			catch (const RegexException &ex)
			{
				source.Reply("%s", ex.GetReason().c_str());
				return;
			}
		}

		User *targ = finduser(mask);
		if (targ)
			mask = "*@" + targ->host;

		if (!akills->CanAdd(source, mask, expires, reason))
			return;
		else if (mask.find_first_not_of("/~@.*?") == Anope::string::npos)
		{
			source.Reply(USERHOST_MASK_TOO_WIDE, mask.c_str());
			return;
		}

		XLine *x = new XLine(mask, u->nick, expires, reason);
		if (Config->AkillIds)
			x->UID = XLineManager::GenerateUID();

		unsigned int affected = 0;
		for (Anope::insensitive_map<User *>::iterator it = UserListByNick.begin(); it != UserListByNick.end(); ++it)
			if (akills->Check(it->second, x))
				++affected;
		float percent = static_cast<float>(affected) / static_cast<float>(UserListByNick.size()) * 100.0;

		if (percent > 95)
		{
			source.Reply(USERHOST_MASK_TOO_WIDE, mask.c_str());
			Log(LOG_ADMIN, u, this) << "tried to akill " << percent << "% of the network (" << affected << " users)";
			delete x;
			return;
		}

		EventReturn MOD_RESULT;
		FOREACH_RESULT(I_OnAddXLine, OnAddXLine(u, x, akills));
		if (MOD_RESULT == EVENT_STOP)
		{
			delete x;
			return;
		}

		akills->AddXLine(x);
		if (Config->AkillOnAdd)
			akills->Send(NULL, x);

		source.Reply(_("\002%s\002 added to the AKILL list."), mask.c_str());

		Log(LOG_ADMIN, u, this) << "on " << mask << " (" << x->Reason << ") expires in " << (expires ? duration(expires - Anope::CurTime) : "never") << " [affects " << affected << " user(s) (" << percent << "%)]";
		if (readonly)
			source.Reply(READ_ONLY_MODE);
	}

	void DoDel(CommandSource &source, const std::vector<Anope::string> &params)
	{
		User *u = source.u;
		const Anope::string &mask = params.size() > 1 ? params[1] : "";

		if (mask.empty())
		{
			this->OnSyntaxError(source, "DEL");
			return;
		}

		if (akills->GetList().empty())
		{
			source.Reply(_("AKILL list is empty."));
			return;
		}

		if (isdigit(mask[0]) && mask.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			AkillDelCallback list(source, mask);
			list.Process();
		}
		else
		{
			XLine *x = akills->HasEntry(mask);

			if (!x)
			{
				source.Reply(_("\002%s\002 not found on the AKILL list."), mask.c_str());
				return;
			}

			do
			{
				FOREACH_MOD(I_OnDelXLine, OnDelXLine(u, x, akills));

				source.Reply(_("\002%s\002 deleted from the AKILL list."), x->Mask.c_str());
				AkillDelCallback::DoDel(source, x);
			}
			while ((x = akills->HasEntry(mask)));

		}

		if (readonly)
			source.Reply(READ_ONLY_MODE);

		return;
	}

	void ProcessList(CommandSource &source, const std::vector<Anope::string> &params, ListFormatter &list)
	{
		const Anope::string &mask = params.size() > 1 ? params[1] : "";

		if (!mask.empty() && isdigit(mask[0]) && mask.find_first_not_of("1234567890,-") == Anope::string::npos)
		{
			class ListCallback : public NumberList
			{
				ListFormatter &list;
			 public:
				ListCallback(ListFormatter &_list, const Anope::string &numstr) : NumberList(numstr, false), list(_list)
				{
				}

				void HandleNumber(unsigned number) anope_override
				{
					if (!number)
						return;

					XLine *x = akills->GetEntry(number - 1);

					if (!x)
						return;

					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(number);
					entry["Mask"] = x->Mask;
					entry["Creator"] = x->By;
					entry["Created"] = do_strftime(x->Created, NULL, true);
					entry["Expires"] = expire_left(NULL, x->Expires);
					entry["Reason"] = x->Reason;
					this->list.addEntry(entry);
				}
			}
			nl_list(list, mask);
			nl_list.Process();
		}
		else
		{
			for (unsigned i = 0, end = akills->GetCount(); i < end; ++i)
			{
				XLine *x = akills->GetEntry(i);

				if (mask.empty() || mask.equals_ci(x->Mask) || mask == x->UID || Anope::Match(x->Mask, mask, false, true))
				{
					ListFormatter::ListEntry entry;
					entry["Number"] = stringify(i + 1);
					entry["Mask"] = x->Mask;
					entry["Creator"] = x->By;
					entry["Created"] = do_strftime(x->Created, NULL, true);
					entry["Expires"] = expire_left(source.u->Account(), x->Expires);
					entry["Reason"] = x->Reason;
					list.addEntry(entry);
				}
			}
		}

		if (list.isEmpty())
			source.Reply(_("No matching entries on the AKILL list."));
		else
		{
			source.Reply(_("Current akill list:"));
		
			std::vector<Anope::string> replies;
			list.Process(replies);

			for (unsigned i = 0; i < replies.size(); ++i)
				source.Reply(replies[i]);

			source.Reply(_("End of \2akill\2 list."));
		}
	}

	void DoList(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (akills->GetList().empty())
		{
			source.Reply(_("AKILL list is empty."));
			return;
		}

		ListFormatter list;
		list.addColumn("Number").addColumn("Mask").addColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoView(CommandSource &source, const std::vector<Anope::string> &params)
	{
		if (akills->GetList().empty())
		{
			source.Reply(_("AKILL list is empty."));
			return;
		}

		ListFormatter list;
		list.addColumn("Number").addColumn("Mask").addColumn("Creator").addColumn("Created").addColumn("Expires").addColumn("Reason");

		this->ProcessList(source, params, list);
	}

	void DoClear(CommandSource &source)
	{
		User *u = source.u;

		for (unsigned i = akills->GetCount(); i > 0; --i)
		{
			XLine *x = akills->GetEntry(i - 1);
			FOREACH_MOD(I_OnDelXLine, OnDelXLine(u, x, akills));
			akills->DelXLine(x);
		}

		source.Reply(_("The AKILL list has been cleared."));
	}
 public:
	CommandOSAKill(Module *creator) : Command(creator, "operserv/akill", 1, 2)
	{
		this->SetDesc(_("Manipulate the AKILL list"));
		this->SetSyntax(_("ADD [+\037expiry\037] \037mask\037 \037reason\037"));
		this->SetSyntax(_("DEL {\037mask\037 | \037entry-num\037 | \037list\037 | \037id\037}"));
		this->SetSyntax(_("LIST [\037mask\037 | \037list\037 | \037id\037]"));
		this->SetSyntax(_("VIEW [\037mask\037 | \037list\037 | \037id\037]"));
		this->SetSyntax(_("CLEAR"));
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		const Anope::string &cmd = params[0];

		if (!akills)
			return;

		if (cmd.equals_ci("ADD"))
			return this->DoAdd(source, params);
		else if (cmd.equals_ci("DEL"))
			return this->DoDel(source, params);
		else if (cmd.equals_ci("LIST"))
			return this->DoList(source, params);
		else if (cmd.equals_ci("VIEW"))
			return this->DoView(source, params);
		else if (cmd.equals_ci("CLEAR"))
			return this->DoClear(source);
		else
			this->OnSyntaxError(source, "");

		return;
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Allows Services operators to manipulate the AKILL list. If\n"
				"a user matching an AKILL mask attempts to connect, Services\n"
				"will issue a KILL for that user and, on supported server\n"
				"types, will instruct all servers to add a ban for the mask\n"
				"which the user matched.\n"
				" \n"
				"\002AKILL ADD\002 adds the given mask to the AKILL\n"
				"list for the given reason, which \002must\002 be given.\n"
				"Mask should be in the format of nick!user@host#real name,\n"
				"though all that is required is user@host. If a real name is specified,\n"
				"the reason must be prepended with a :.\n"
				"\037expiry\037 is specified as an integer followed by one of \037d\037 \n"
				"(days), \037h\037 (hours), or \037m\037 (minutes). Combinations (such as \n"
				"\0371h30m\037) are not permitted. If a unit specifier is not \n"
				"included, the default is days (so \037+30\037 by itself means 30 \n"
				"days). To add an AKILL which does not expire, use \037+0\037. If the\n"
				"usermask to be added starts with a \037+\037, an expiry time must\n"
				"be given, even if it is the same as the default. The\n"
				"current AKILL default expiry time can be found with the\n"
				"\002STATS AKILL\002 command.\n"));
		if (!Config->RegexEngine.empty())
			source.Reply(" \n"
					"Regex matches are also supported using the %s engine.\n"
					"Enclose your mask in // if this desired.", Config->RegexEngine.c_str());
		source.Reply(_(
				" \n"
				"The \002AKILL DEL\002 command removes the given mask from the\n"
				"AKILL list if it is present.  If a list of entry numbers is \n"
				"given, those entries are deleted.  (See the example for LIST \n"
				"below.)\n"
				" \n"
				"The \002AKILL LIST\002 command displays the AKILL list.  \n"
				"If a wildcard mask is given, only those entries matching the\n"
				"mask are displayed.  If a list of entry numbers is given,\n"
				"only those entries are shown; for example:\n"
				"   \002AKILL LIST 2-5,7-9\002\n"
				"      Lists AKILL entries numbered 2 through 5 and 7 \n"
				"      through 9.\n"
				"      \n"
				"\002AKILL VIEW\002 is a more verbose version of \002AKILL LIST\002, and \n"
				"will show who added an AKILL, the date it was added, and when \n"
				"it expires, as well as the user@host/ip mask and reason.\n"
				" \n"
				"\002AKILL CLEAR\002 clears all entries of the AKILL list."));
		return true;
	}
};

class OSAKill : public Module
{
	CommandOSAKill commandosakill;

 public:
	OSAKill(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, CORE),
		commandosakill(this)
	{
		this->SetAuthor("Anope");

	}
};

MODULE_INIT(OSAKill)