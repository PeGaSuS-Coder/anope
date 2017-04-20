/*
 * Anope IRC Services
 *
 * Copyright (C) 2014-2017 Anope Team <team@anope.org>
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

namespace Event
{
	struct CoreExport NickInfo : Events
	{
		static constexpr const char *NAME = "nickinfo";

		using Events::Events;

		/** Called when a user requests info for a nick
		 * @param source The user requesting info
		 * @param na The nick the user is requesting info from
		 * @param info Data to show the user requesting information
		 * @param show_hidden true if we should show the user everything
		 */
		virtual void OnNickInfo(CommandSource &source, NickServ::Nick *na, InfoFormatter &info, bool show_hidden) anope_abstract;
	};
}

