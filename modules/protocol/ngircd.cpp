/*
 * Anope IRC Services
 *
 * Copyright (C) 2011-2017 Anope Team <team@anope.org>
 * Copyright (C) 2011-2012, 2014 Alexander Barton <alex@barton.de>
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

/* Dependencies: anope_protocol.rfc1459 */

#include "module.h"
#include "modules/protocol/rfc1459.h"
#include "modules/protocol/ngircd.h"

void ngircd::senders::Akill::Send(User* u, XLine* x)
{
	// Calculate the time left before this would expire, capping it at 2 days
	time_t timeleft = x->GetExpires() - Anope::CurTime;
	if (timeleft > 172800 || !x->GetExpires())
		timeleft = 172800;
	Uplink::Send(Me, "GLINE", x->GetMask(), timeleft, x->GetReason() + " (" + x->GetBy() + ")");
}

void ngircd::senders::AkillDel::Send(XLine* x)
{
	Uplink::Send(Me, "GLINE", x->GetMask());
}

void ngircd::senders::MessageChannel::Send(Channel* c)
{
	Uplink::Send(Me, "CHANINFO", c->name, "+" + c->GetModes(true, true));
}

void ngircd::senders::Login::Send(User *u, NickServ::Nick *na)
{
	Uplink::Send(Me, "METADATA", u->GetUID(), "accountname", na->GetAccount()->GetDisplay());
}

void ngircd::senders::Logout::Send(User *u)
{
	Uplink::Send(Me, "METADATA", u->GetUID(), "accountname", "");
}

void ngircd::senders::SVSNick::Send(User* u, const Anope::string& newnick, time_t ts)
{
	Uplink::Send(Me, "SVSNICK", u->nick, newnick);
}

// Received: :dev.anope.de NICK DukeP 1 ~DukePyro p57ABF9C9.dip.t-dialin.net 1 +i :DukePyrolator
void ngircd::senders::NickIntroduction::Send(User *user)
{
	Anope::string modes = "+" + user->GetModes();
	Uplink::Send(Me, "NICK", user->nick, 1, user->GetIdent(), user->host, 1, modes, user->realname);
}

void ngircd::senders::VhostDel::Send(User* u)
{
	IRCD->Send<messages::VhostSet>(u, u->GetIdent(), "");
}

void ngircd::senders::VhostSet::Send(User* u, const Anope::string& vident, const Anope::string& vhost)
{
	if (!vident.empty())
		Uplink::Send(Me, "METADATA", u->nick, "user", vident);

	Uplink::Send(Me, "METADATA", u->nick, "cloakhost", vhost);
	if (!u->HasMode("CLOAK"))
	{
		u->SetMode(Config->GetClient("HostServ"), "CLOAK");
		ModeManager::ProcessModes();
	}
}

ngircd::Proto::Proto(Module *creator) : IRCDProto(creator, "ngIRCd")
{
	DefaultPseudoclientModes = "+oi";
	CanCertFP = true;
	CanSVSNick = true;
	CanSetVHost = true;
	CanSetVIdent = true;
	MaxModes = 5;
}

void ngircd::Proto::Handshake()
{
	Uplink::Send("PASS", Config->Uplinks[Anope::CurrentUplink].password, "0210-IRC+", "Anope|" + Anope::VersionShort(), "CLHMSo P");
	/* Make myself known to myself in the serverlist */
	IRCD->Send<messages::MessageServer>(Me);
	/* finish the enhanced server handshake and register the connection */
	Uplink::Send("376", "*", "End of MOTD command");
}

Anope::string ngircd::Proto::Format(IRCMessage &message)
{
	if (message.GetSource().GetSource().empty())
		message.SetSource(Me);
	return IRCDProto::Format(message);
}

// Please see <http://www.irc.org/tech_docs/005.html> for details.
void ngircd::Numeric005::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	size_t pos;
	Anope::string parameter, data;
	for (unsigned i = 0, end = params.size(); i < end; ++i)
	{
		pos = params[i].find('=');
		if (pos != Anope::string::npos)
		{
			parameter = params[i].substr(0, pos);
			data = params[i].substr(pos+1, params[i].length());
			if (parameter == "MODES")
			{
				unsigned maxmodes = convertTo<unsigned>(data);
				IRCD->MaxModes = maxmodes;
			}
			else if (parameter == "NICKLEN")
			{
				unsigned newlen = convertTo<unsigned>(data), len = Config->GetBlock("networkinfo")->Get<unsigned>("nicklen");
				if (len != newlen)
				{
					Anope::Logger.Log("Warning: NICKLEN is {0} but networkinfo:nicklen is {1}", newlen, len);
				}
			}
		}
	}
}

/*
 * CHANINFO is used by servers to inform each other about a channel: its
 * modes, channel key, user limits and its topic. The parameter combination
 * <key> and <limit> is optional, as well as the <topic> parameter, so that
 * there are three possible forms of this command:
 *
 * CHANINFO <chan> +<modes>
 * CHANINFO <chan> +<modes> :<topic>
 * CHANINFO <chan> +<modes> <key> <limit> :<topic>
 *
 * The parameter <key> must be ignored if a channel has no key (the parameter
 * <modes> doesn't list the "k" channel mode). In this case <key> should
 * contain "*" because the parameter <key> is required by the CHANINFO syntax
 * and therefore can't be omitted. The parameter <limit> must be ignored when
 * a channel has no user limit (the parameter <modes> doesn't list the "l"
 * channel mode). In this case <limit> should be "0".
 */
void ngircd::ChanInfo::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	bool created;
	Channel *c = Channel::FindOrCreate(params[0], created);

	Anope::string modes = params[1];

	if (params.size() == 3)
	{
		c->ChangeTopicInternal(NULL, source.GetName(), params[2], Anope::CurTime);
	}
	else if (params.size() == 5)
	{
		for (size_t i = 0, end = params[1].length(); i < end; ++i)
		{
			switch(params[1][i])
			{
				case 'k':
					modes += " " + params[2];
					continue;
				case 'l':
					modes += " " + params[3];
					continue;
			}
		}
		c->ChangeTopicInternal(NULL, source.GetName(), params[4], Anope::CurTime);
	}

	c->SetModesInternal(source, modes);
}

/*
 * <@po||ux> DukeP: RFC 2813, 4.2.1: the JOIN command on server-server links
 * separates the modes ("o") with ASCII 7, not space. And you can't see ASCII 7.
 *
 * if a user joins a new channel, the ircd sends <channelname>\7<umode>
 */
void ngircd::Join::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	User *user = source.GetUser();
	size_t pos = params[0].find('\7');
	Anope::string channel, modes;

	if (pos != Anope::string::npos)
	{
		channel = params[0].substr(0, pos);
		modes = '+' + params[0].substr(pos+1, params[0].length()) + " " + user->nick;
	}
	else
	{
		channel = params[0];
	}

	std::vector<Anope::string> new_params;
	new_params.push_back(channel);

	rfc1459::Join::Run(source, new_params);

	if (!modes.empty())
	{
		Channel *c = Channel::Find(channel);
		if (c)
			c->SetModesInternal(source, modes);
	}
}

/*
 * Received: :ngircd.dev.anope.de METADATA DukePyrolator host :anope-e2ee5c7d
 *
 * params[0] = nick of the user
 * params[1] = command
 * params[2] = data
 *
 * following commands are supported:
 *  - "accountname": the account name of a client (can't be empty)
 *  - "certfp": the certificate fingerprint of a client (can't be empty)
 *  - "cloakhost" : the cloaked hostname of a client
 *  - "host": the hostname of a client (can't be empty)
 *  - "info": info text ("real name") of a client
 *  - "user": the user name (ident) of a client (can't be empty)
 */
void ngircd::Metadata::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	User *u = User::Find(params[0]);
	if (!u)
		return;

	if (params[1].equals_cs("accountname"))
	{
		NickServ::Account *nc = NickServ::FindAccount(params[2]);
		if (nc)
			u->Login(nc);
	}
	else if (params[1].equals_cs("certfp"))
	{
		u->fingerprint = params[2];
		EventManager::Get()->Dispatch(&Event::Fingerprint::OnFingerprint, u);
	}
	else if (params[1].equals_cs("cloakhost"))
	{
		if (!params[2].empty())
			u->SetDisplayedHost(params[2]);
	}
	else if (params[1].equals_cs("host"))
	{
		u->SetCloakedHost(params[2]);
	}
	else if (params[1].equals_cs("info"))
	{
		u->SetRealname(params[2]);
	}
	else if (params[1].equals_cs("user"))
	{
		u->SetVIdent(params[2]);
	}
}

/*
 * Received: :DukeP MODE #anope +b *!*@*.aol.com
 * Received: :DukeP MODE #anope +h DukeP
 * params[0] = channel or nick
 * params[1] = modes
 * params[n] = parameters
 */
void ngircd::Mode::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	Anope::string modes = params[1];

	for (size_t i = 2; i < params.size(); ++i)
		modes += " " + params[i];

	if (IRCD->IsChannelValid(params[0]))
	{
		Channel *c = Channel::Find(params[0]);

		if (c)
			c->SetModesInternal(source, modes);
	}
	else
	{
		User *u = User::Find(params[0]);

		if (u)
			u->SetModesInternal(source, "%s", params[1].c_str());
	}
}

/*
 * NICK - NEW
 * Received: :dev.anope.de NICK DukeP_ 1 ~DukePyro ip-2-201-236-154.web.vodafone.de 1 + :DukePyrolator
 * Parameters: <nickname> <hopcount> <username> <host> <servertoken> <umode> :<realname>
 * source = server
 * params[0] = nick
 * params[1] = hopcount
 * params[2] = username/ident
 * params[3] = host
 * params[4] = servertoken
 * params[5] = modes
 * params[6] = info
 *
 * NICK - change
 * Received: :DukeP_ NICK :test2
 * source    = oldnick
 * params[0] = newnick
 *
 */
void ngircd::Nick::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	if (params.size() == 1)
	{
		// we have a nickchange
		User *u = source.GetUser();

		if (u)
			u->ChangeNick(params[0]);
	}
	else if (params.size() == 7)
	{
		// a new user is connecting to the network
		Server *s = Server::Find(params[4]);
		if (s == nullptr)
		{
			Anope::Logger.Debug("User {0} introduced from non-existent server {1}", params[0], params[4]);
			return;
		}
		User::OnIntroduce(params[0], params[2], params[3], "", "", s, params[6], Anope::CurTime, params[5], "", NULL);
	}
}

/*
 * RFC 2813, 4.2.2: Njoin Message:
 * The NJOIN message is used between servers only.
 * It is used when two servers connect to each other to exchange
 * the list of channel members for each channel.
 *
 * Even though the same function can be performed by using a succession
 * of JOIN, this message SHOULD be used instead as it is more efficient.
 *
 * Received: :dev.anope.de NJOIN #test :DukeP2,@DukeP,%test,+test2
 */
void ngircd::NJoin::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	std::list<rfc1459::Join::SJoinUser> users;

	commasepstream sep(params[1]);
	Anope::string buf;
	while (sep.GetToken(buf))
	{

		rfc1459::Join::SJoinUser sju;

		/* Get prefixes from the nick */
		for (char ch; !buf.empty() && (ch = ModeManager::GetStatusChar(buf[0]));)
		{
			buf.erase(buf.begin());
			sju.first.AddMode(ch);
		}

		sju.second = User::Find(buf);
		if (!sju.second)
		{
			Anope::Logger.Debug("NJOIN for non-existent user {0} on {1}", buf, params[0]);
			continue;
		}
		users.push_back(sju);
	}

	rfc1459::Join::SJoin(source, params[0], 0, "", users);
}

/*
 * ngIRCd does not send an EOB, so we send a PING immediately
 * when receiving a new server and then finish sync once we
 * get a pong back from that server.
 */
void ngircd::Pong::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	if (!source.GetServer()->IsSynced())
		source.GetServer()->Sync(false);
}

/*
 * New directly linked server:
 *
 * SERVER tolsun.oulu.fi 1 :Experimental server
 * 	New server tolsun.oulu.fi introducing itself
 * 	and attempting to register.
 *
 * params[0] = servername
 * params[1] = hop count
 * params[2] = server description
 *
 * New remote server in the network:
 *
 * :tolsun.oulu.fi SERVER csd.bu.edu 5 34 :BU Central Server
 *	Server tolsun.oulu.fi is our uplink for csd.bu.edu
 *	which is 5 hops away. The token "34" will be used
 *	by tolsun.oulu.fi when introducing new users or
 *	services connected to csd.bu.edu.
 *
 * params[0] = servername
 * params[1] = hop count
 * params[2] = server numeric
 * params[3] = server description
 */

void ngircd::ServerMessage::Run(MessageSource &source, const std::vector<Anope::string> &params)
{
	if (params.size() == 3)
	{
		// our uplink is introducing itself
		new Server(Me, params[0], 1, params[2], "1");
	}
	else
	{
		// our uplink is introducing a new server
		unsigned int hops = 0;

		try
		{
			hops = convertTo<unsigned>(params[1]);
		}
		catch (const ConvertException &) { }

		new Server(source.GetServer(), params[0], hops, params[3], params[2]);
	}
	/*
	 * ngIRCd does not send an EOB, so we send a PING immediately
	 * when receiving a new server and then finish sync once we
	 * get a pong back from that server.
	 */
	IRCD->Send<messages::Ping>(Me->GetName(), params[0]);
}

class ProtongIRCd : public Module
	, public EventHook<Event::UserNickChange>
{
	ngircd::Proto ircd_proto;

	/* Core message handlers */
	rfc1459::Capab message_capab;
	rfc1459::Error message_error;
	rfc1459::Invite message_invite;
	rfc1459::Kick message_kick;
	rfc1459::Kill message_kill;
	rfc1459::MOTD message_motd;
	rfc1459::Notice message_notice;
	rfc1459::Part message_part;
	rfc1459::Ping message_ping;
	rfc1459::Privmsg message_privmsg, message_squery;
	rfc1459::Quit message_quit;
	rfc1459::SQuit message_squit;
	rfc1459::Stats message_stats;
	rfc1459::Time message_time;
	rfc1459::Topic message_topic;
	rfc1459::Version message_version;
	rfc1459::Whois message_whois;

	/* Our message handlers */
	ngircd::Numeric005 message_005;
	ngircd::ChanInfo message_chaninfo;
	ngircd::Join message_join;
	ngircd::Metadata message_metadata;
	ngircd::Mode message_mode;
	ngircd::Nick message_nick;
	ngircd::NJoin message_njoin;
	ngircd::Pong message_pong;
	ngircd::ServerMessage message_server;

	/* Core message senders */
	rfc1459::senders::GlobalNotice sender_global_notice;
	rfc1459::senders::GlobalPrivmsg sender_global_privmsg;
	rfc1459::senders::Invite sender_invite;
	rfc1459::senders::Join sender_join;
	rfc1459::senders::Kick sender_kick;
	rfc1459::senders::Kill sender_svskill;
	rfc1459::senders::ModeChannel sender_mode_chan;
	rfc1459::senders::ModeUser sender_mode_user;
	rfc1459::senders::NickChange sender_nickchange;
	rfc1459::senders::Notice sender_notice;
	rfc1459::senders::Part sender_part;
	rfc1459::senders::Ping sender_ping;
	rfc1459::senders::Pong sender_pong;
	rfc1459::senders::Privmsg sender_privmsg;
	rfc1459::senders::Quit sender_quit;
	rfc1459::senders::MessageServer sender_server;
	rfc1459::senders::SQuit sender_squit;
	rfc1459::senders::Topic sender_topic;
	rfc1459::senders::Wallops sender_wallops;

	ngircd::senders::Akill sender_akill;
	ngircd::senders::AkillDel sender_akill_del;
	ngircd::senders::MessageChannel sender_channel;
	ngircd::senders::Login sender_login;
	ngircd::senders::Logout sender_logout;
	ngircd::senders::SVSNick sender_svsnick;
	ngircd::senders::VhostDel sender_vhost_del;
	ngircd::senders::VhostSet sender_vhost_set;

 public:
	ProtongIRCd(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, PROTOCOL | VENDOR)
		, EventHook<Event::UserNickChange>(this)
		, ircd_proto(this)
		, message_capab(this)
		, message_error(this)
		, message_invite(this)
		, message_kick(this)
		, message_kill(this)
		, message_motd(this)
		, message_notice(this)
		, message_part(this)
		, message_ping(this)
		, message_privmsg(this)
		, message_squery(this, "SQUERY")
		, message_quit(this)
		, message_squit(this)
		, message_stats(this)
		, message_time(this)
		, message_version(this)
		, message_whois(this)

		, message_005(this)
		, message_chaninfo(this)
		, message_join(this)
		, message_metadata(this)
		, message_mode(this)
		, message_nick(this)
		, message_njoin(this)
		, message_pong(this)
		, message_server(this)
		, message_topic(this)

		, sender_akill(this)
		, sender_akill_del(this)
		, sender_channel(this)
		, sender_global_notice(this)
		, sender_global_privmsg(this)
		, sender_invite(this)
		, sender_join(this)
		, sender_kick(this)
		, sender_svskill(this)
		, sender_login(this)
		, sender_logout(this)
		, sender_mode_chan(this)
		, sender_mode_user(this)
		, sender_nickchange(this)
		, sender_notice(this)
		, sender_part(this)
		, sender_ping(this)
		, sender_pong(this)
		, sender_privmsg(this)
		, sender_quit(this)
		, sender_server(this)
		, sender_squit(this)
		, sender_svsnick(this)
		, sender_topic(this)
		, sender_vhost_del(this)
		, sender_vhost_set(this)
		, sender_wallops(this)
	{
		Servers::Capab.insert("QS");

		IRCD = &ircd_proto;
	}

	~ProtongIRCd()
	{
		IRCD = nullptr;
	}

	void OnUserNickChange(User *u, const Anope::string &) override
	{
		u->RemoveMode(Config->GetClient("NickServ"), "REGISTERED");
	}
};

MODULE_INIT(ProtongIRCd)
