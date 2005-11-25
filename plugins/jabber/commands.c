/* $Id$ */

/*
 *  (C) Copyright 2003 Wojtek Kaniewski <wojtekka@irc.pl>
 *		       Tomasz Torcz <zdzichu@irc.pl>	
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ekg2-config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/utsname.h> /* dla jabber:iq:version */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <netdb.h>

#ifdef HAVE_EXPAT_H
#  include <expat.h>
#endif

#include <ekg/commands.h>
#include <ekg/dynstuff.h>
#include <ekg/protocol.h>
#include <ekg/sessions.h>
#include <ekg/stuff.h>
#include <ekg/userlist.h>
#include <ekg/themes.h>
#include <ekg/vars.h>
#include <ekg/xmalloc.h>
#include <ekg/log.h>

#include "jabber.h"

COMMAND(jabber_command_connect)
{
	const char *server, *realserver = session_get(session, "server"); 
	int res, fd[2];
	jabber_private_t *j = session_private_get(session);
	
	if (j->connecting) {
		printq("during_connect", session_name(session));
		return -1;
	}

	if (session_connected_get(session)) {
		printq("already_connected", session_name(session));
		return -1;
	}

	if (!(session_get(session, "password"))) {
		printq("no_config");
		return -1;
	}

	debug("session->uid = %s\n", session->uid);
	
	if (!(server = xstrchr(session->uid, '@'))) {
		printq("wrong_id", session->uid);
		return -1;
	}

	xfree(j->server);
	j->server = xstrdup(++server);

	debug("[jabber] resolving %s\n", (realserver ? realserver : server));

	if (pipe(fd) == -1) {
		printq("generic_error", strerror(errno));
		return -1;
	}

	debug("[jabber] resolver pipes = { %d, %d }\n", fd[0], fd[1]);

	if ((res = fork()) == -1) {
		printq("generic_error", strerror(errno));
		close(fd[0]);
		close(fd[1]);
		return -1;
	}

	if (!res) {
		struct in_addr a;
		if ((a.s_addr = inet_addr(server)) == INADDR_NONE) {
			struct hostent *he = gethostbyname(realserver ? realserver : server);

			if (!he)
				a.s_addr = INADDR_NONE;
			else
				memcpy(&a, he->h_addr, sizeof(a));
		}
		write(fd[1], &a, sizeof(a));

		sleep(1);

		exit(0);
	} else {
                jabber_handler_data_t *jdta = xmalloc(sizeof(jabber_handler_data_t));

		close(fd[1]);
		
		jdta->session = session;
		jdta->roster_retrieved = 0;
		/* XXX doda� dzieciaka do przegl�dania */

		watch_add(&jabber_plugin, fd[0], WATCH_READ, 0, jabber_handle_resolver, jdta);
	}

	j->connecting = 1;

	printq("connecting", session_name(session));

	if (!xstrcmp(session_status_get(session), EKG_STATUS_NA))
		session_status_set(session, EKG_STATUS_AVAIL);
	return 0;
}

COMMAND(jabber_command_disconnect)
{
	jabber_private_t *j = session_private_get(session);
	char *descr = NULL;

	/* jesli istnieje timer reconnecta, to znaczy, ze przerywamy laczenie */
	if (timer_remove(&jabber_plugin, "reconnect") == 0) {
		printq("auto_reconnect_removed", session_name(session));
		return 0;
	}

	if (!j->connecting && !session_connected_get(session)) {
		printq("not_connected", session_name(session));
		return -1;
	}

	/* je�li jest /reconnect, nie mieszamy z opisami */
	if (xstrcmp(name, "reconnect")) {
		if (params[0])
			descr = xstrdup(params[0]);
		else
			descr = ekg_draw_descr("quit");
	} else
		descr = xstrdup(session_descr_get(session));

	if (descr) {
		char *tmp = jabber_escape(descr);
		jabber_write(j, "<presence type=\"unavailable\"><status>%s</status></presence>", tmp);
		xfree(tmp);
	} else
		jabber_write(j, "<presence type=\"unavailable\"/>");

	jabber_write(j, "</stream:stream>");

	if (j->connecting) 
		j->connecting = 0;

	userlist_free(session);

	if (j->connecting)
		jabber_handle_disconnect(session, descr, EKG_DISCONNECT_STOPPED);
	else
		jabber_handle_disconnect(session, descr, EKG_DISCONNECT_USER);
	
	watch_remove(&jabber_plugin, j->fd, WATCH_READ);
	xfree(descr);
	return 0;
}

COMMAND(jabber_command_reconnect)
{
	jabber_private_t *j = session_private_get(session);

	if (j->connecting || session_connected_get(session)) {
		jabber_command_disconnect(name, params, session, target, quiet);
	}

	return jabber_command_connect(name, params, session, target, quiet);
}

const char *jid_target2uid(session_t *s, const char *target, int quiet) {
	const char *uid;
/* CHECK: po co my wlasciwcie robimy to get_uid ? */
/* a) jak target jest '$' to zwraca aktualne okienko... 	(niepotrzebne raczej, CHECK)
   b) szuka targeta na userliscie 				(w sumie to trzeba jeszcze)
   c) robi to co my nizej tylko dla kazdego plugina 		(niepotrzebne)
 */
#if 1
	if (!(uid = get_uid(s, target))) 
		uid = target;
#endif
/* CHECK: i think we can omit it */
	if (xstrncasecmp(uid, "jid:", 4)) {
		printq("invalid_session");
		return NULL;
	}
	if (!xstrchr(uid, '@') || xstrchr(uid, '@') > xstrchr(uid, '.')) {
		printq("invalid_uid", uid);
		return NULL;
	}
	return uid;
}

COMMAND(jabber_command_msg)
{
	jabber_private_t *j = session_private_get(session);
	int chat = !xstrcasecmp(name, "chat");
	char *msg;
	char *subject = NULL;
	const char *uid;
	int ismuc = 0; 			/* TODO, IS that multi user chat */

	int subjectlen = xstrlen(config_subject_prefix);

	if (!xstrcmp(target, "*")) {
		if (msg_all(session, name, params[1]) == -1)
			printq("list_empty");
		return 0;
	}
	if (!(uid = jid_target2uid(session, target, quiet)))
		return -1;
	/* czy wiadomo�� ma mie� temat? */
	if (config_subject_prefix && !xstrncmp(params[1], config_subject_prefix, subjectlen)) {
		char *subtmp = xstrdup((params[1]+subjectlen)); /* obcinamy prefix tematu */
		char *tmp;

		/* je�li ma wi�cej linijek, zostawiamu tylko pierwsz� */
		if ((tmp = xstrchr(subtmp, 10)))
			*tmp = 0;

		subject = jabber_escape(subtmp);
		xfree(subtmp);

		/* body of wiadomo�� to wszystko po ko�cu pierwszej linijki */
		msg = jabber_escape(tmp ? tmp+1 : NULL);
	} else 
		msg = jabber_escape(params[1]); /* bez tematu */

	if (ismuc)
		jabber_write(j, "<message to=\"%s/%s\" id=\"%d\" type=\"chat\">", uid+4, "darkjames", time(NULL));
	else
		jabber_write(j, "<message %sto=\"%s\" id=\"%d\">", chat ? "type=\"chat\" " : "", uid+4, time(NULL));

	if (subject) {
		jabber_write(j, "<subject>%s</subject>", subject); 
		xfree(subject); 
	}
	if (msg) {
		jabber_write(j, "<body>%s</body>", msg);
        	if (config_last & 4) 
        		last_add(1, uid, time(NULL), 0, msg);
		xfree(msg);
	}

	jabber_write(j, "<x xmlns=\"jabber:x:event\">%s%s<displayed/><composing/></x>", 
		( config_display_ack == 1 || config_display_ack == 2 ? "<delivered/>" : ""),
		( config_display_ack == 1 || config_display_ack == 3 ? "<offline/>"   : "") );
	jabber_write(j, "</message>");

	if (!quiet && !ismuc) { /* if (1) ? */ 
		char *me 	= xstrdup(session_uid_get(session));
		char **rcpts 	= xmalloc(sizeof(char *) * 2);
		char *msg	= xstrdup(params[1]);
		time_t sent 	= time(NULL);
		int class 	= (chat) ? EKG_MSGCLASS_SENT_CHAT : EKG_MSGCLASS_SENT;
		int ekgbeep 	= EKG_NO_BEEP; /* bo co ma beepowac kiedy wysylamy ? */
		char *format 	= NULL;
		char *seq 	= NULL;
		int secure	= 0;

		rcpts[0] 	= xstrdup(uid);
		rcpts[1] 	= NULL;

		if (ismuc)
			class |= EKG_NO_THEMEBIT;
		
		query_emit(NULL, "protocol-message", &me, &me, &rcpts, &msg, &format, &sent, &class, &seq, &ekgbeep, &secure);

		xfree(msg);
		xfree(me);
		xfree(rcpts[0]);
		xfree(rcpts);
	}

	session_unidle(session);

	return 0;
}

COMMAND(jabber_command_inline_msg)
{
	const char *p[2] = { NULL, params[0] };
	if (!params[0])
		return -1;
	return jabber_command_msg("chat", p, session, target, quiet);
}

COMMAND(jabber_command_xml)
{
	jabber_private_t *j = session_private_get(session);
	jabber_write(j, "%s", params[0]);
	return 0;
}

COMMAND(jabber_command_away)
{
	const char *descr, *format;
	
	if (params[0]) {
		session_descr_set(session, (!xstrcmp(params[0], "-")) ? NULL : params[0]);
		reason_changed = 1;
	} 
	if (!xstrcmp(name, "_autoback")) {
		format = "auto_back";
		session_status_set(session, EKG_STATUS_AVAIL);
		session_unidle(session);
	} else if (!xstrcmp(name, "back")) {
		format = "back";
		session_status_set(session, EKG_STATUS_AVAIL);
		session_unidle(session);
	} else if (!xstrcmp(name, "_autoaway")) {
		format = "auto_away";
		session_status_set(session, EKG_STATUS_AUTOAWAY);
	} else if (!xstrcmp(name, "away")) {
		format = "away"; 
		session_status_set(session, EKG_STATUS_AWAY);
		session_unidle(session);
	} else if (!xstrcmp(name, "dnd")) {
		format = "dnd";
		session_status_set(session, EKG_STATUS_DND);
		session_unidle(session);
	} else if (!xstrcmp(name, "ffc")) {
	        format = "chat";
	        session_status_set(session, EKG_STATUS_FREE_FOR_CHAT);
                session_unidle(session);
        } else if (!xstrcmp(name, "xa")) {
		format = "xa";
		session_status_set(session, EKG_STATUS_XA);
		session_unidle(session);
	} else if (!xstrcmp(name, "invisible")) {
		format = "invisible";
		session_status_set(session, EKG_STATUS_INVISIBLE);
		session_unidle(session);
	} else
		return -1;
	if (!params[0]) {
                char *tmp;

                if ((tmp = ekg_draw_descr(format))) {
                        session_status_set(session, tmp);
                        xfree(tmp);
                }

                if (!config_keep_reason) {
                        session_descr_set(session, NULL);
                }
	}

	descr = (char *) session_descr_get(session);

	ekg_update_status(session);
	
	if (descr) {
		char *f = saprintf("%s_descr", format);
		printq(f, descr, "", session_name(session));
		xfree(f);
	} else
		printq(format, session_name(session));

	if (session_connected_get(session)) 
		jabber_write_status(session);
	
	return 0;
}

COMMAND(jabber_command_passwd)
{
	jabber_private_t *j = session_private_get(session);
	char *username, *passwd;

	username = xstrdup(session->uid + 4);
	*(xstrchr(username, '@')) = 0;

//	username = xstrndup(session->uid + 4, xstrchr(session->uid+4, '@') - session->uid+4);

	passwd = jabber_escape(params[0]);
	jabber_write(j, "<iq type=\"set\" to=\"%s\" id=\"passwd%d\"><query xmlns=\"jabber:iq:register\"><username>%s</username><password>%s</password></query></iq>", j->server, j->id++, username, passwd);
	
	session_set(session, "__new_password", params[0]);

	xfree(username);
	xfree(passwd);

	return 0;
}

COMMAND(jabber_command_auth) 
{
	jabber_private_t *j = session_private_get(session);
	session_t *s = session;
	const char *action;
	const char *uid;

	if (!(uid = jid_target2uid(session, params[1], quiet)))
		return -1;
	/* user jest OK, wi�c lepiej mie� go pod r�k� */
	tabnick_add(uid);

	if (match_arg(params[0], 'r', "request", 2)) {
		action = "subscribe";
		printq("jabber_auth_request", uid+4, session_name(s));
	} else if (match_arg(params[0], 'a', "accept", 2)) {
		action = "subscribed";
		printq("jabber_auth_accept", uid+4, session_name(s));
	} else if (match_arg(params[0], 'c', "cancel", 2)) {
		action = "unsubscribe";
		printq("jabber_auth_unsubscribed", uid+4, session_name(s));
	} else if (match_arg(params[0], 'd', "deny", 2)) {
		action = "unsubscribed";

		if (userlist_find(session, uid))  // mamy w rosterze
			printq("jabber_auth_cancel", uid+4, session_name(s));
		else // nie mamy w rosterze
			printq("jabber_auth_denied", uid+4, session_name(s));
	
	} else if (match_arg(params[0], 'p', "probe", 2)) {
	/* ha! undocumented :-); bo 
	   [Used on server only. Client authors need not worry about this.] */
		action = "probe";
		printq("jabber_auth_probe", uid+4, session_name(s));
	} else {
		printq("invalid_params", name);
		return -1;
	}

	jabber_write(j, "<presence to=\"%s\" type=\"%s\" id=\"roster\"/>", uid+4, action);
	return 0;
}

COMMAND(jabber_command_modify)
{
	jabber_private_t *j = session_private_get(session);
	const char *uid = NULL;
	char *nickname = NULL;
	int ret = 0;
	userlist_t *u;
	list_t m;
	
	int addcomm = !xstrcasecmp(name, "add");

	if (!(u = userlist_find(session, target))) {
		if (!addcomm) {
			printq("user_not_found", target);
			return -1;
		} else {
			/* khm ? a nie powinnismy userlist_add() ? */
			u = xmalloc(sizeof(userlist_t));
		}
	}

	if (params[1]) {
		char **argv = array_make(params[1], " \t", 0, 1, 1);
		int i;

		for (i = 0; argv[i]; i++) {

			if (match_arg(argv[i], 'g', "group", 2) && argv[i + 1]) {
				char **tmp = array_make(argv[++i], ",", 0, 1, 1);
				int x, off;	/* je�li zaczyna si� od '@', pomijamy pierwszy znak */

				for (x = 0; tmp[x]; x++)
					switch (*tmp[x]) {
						case '-':
							off = (tmp[x][1] == '@' && xstrlen(tmp[x]) > 1) ? 1 : 0;

							if (ekg_group_member(u, tmp[x] + 1 + off)) {
								ekg_group_remove(u, tmp[x] + 1 + off);
							} else {
								printq("group_member_not_yet", format_user(session, u->uid), tmp[x] + 1);
							}
							break;
						case '+':
							off = (tmp[x][1] == '@' && xstrlen(tmp[x]) > 1) ? 1 : 0;

							if (!ekg_group_member(u, tmp[x] + 1 + off)) {
								ekg_group_add(u, tmp[x] + 1 + off);
							} else {
								printq("group_member_already", format_user(session, u->uid), tmp[x] + 1);
							}
							break;
						default:
							off = (tmp[x][0] == '@' && xstrlen(tmp[x]) > 1) ? 1 : 0;

							if (!ekg_group_member(u, tmp[x] + off)) {
								ekg_group_add(u, tmp[x] + off);
							} else {
								printq("group_member_already", format_user(session, u->uid), tmp[x]);
							}
					}

				array_free(tmp);
				continue;
			}

			if (match_arg(argv[i], 'n', "nickname", 2) && argv[i + 1]) {
				xfree(nickname);
				nickname = jabber_escape(argv[++i]);
			}
		}
		array_free(argv);
	} 
	
	if (addcomm) {
		if (!nickname && params[1])
			nickname = jabber_escape(params[1]);
	} 
/* TODO: co robimy z nickname jesli jest jid:modify ? Pobieramy z userlisty jaka mamy akutualnie nazwe ? */
#if 0
	else if (!nickname && target) /* jesli jest modify i hmm. nie mamy nickname czyli params[1] to zamieniamy na target ? czyli zamieniamy na to samo co mielismy ? */
			nickname = jabber_escape(target);
#endif

	if (!(uid = jid_target2uid(session, target, quiet))) 
		return -1;

	jabber_write(j, "<iq type=\"set\"><query xmlns=\"jabber:iq:roster\">");

	/* nickname always should be set */
	if (nickname)	jabber_write(j, "<item jid=\"%s\" name=\"%s\"%s>", uid+4, nickname, (u->groups ? "" : "/"));
	else		jabber_write(j, "<item jid=\"%s\"%s>", uid+4, (u->groups ? "" : "/"));

	for (m = u->groups; m ; m = m->next) {
		struct ekg_group *g = m->data;
		char *gname = jabber_escape(g->name);

		jabber_write(j,"<group>%s</group>", gname);
		xfree(gname);
	}

	if (u->groups)
		jabber_write(j,"</item>");

	jabber_write(j, "</query></iq>");

	xfree(nickname);
	
	if (addcomm) {
		char *tmp = saprintf("/auth --request %s", uid);
		ret = command_exec(target, session, tmp, 0);
		xfree(tmp);
		xfree(u);
	}
	
	return ret;
}

COMMAND(jabber_command_del)
{
	const char *uid;
	if (!(uid = jid_target2uid(session, target, quiet)))
		return -1;
	{
		jabber_private_t *j = session_private_get(session);
		char *xuid = jabber_escape(uid+4);
		jabber_write(j, "<iq type=\"set\" id=\"roster\"><query xmlns=\"jabber:iq:roster\">");
		jabber_write(j, "<item jid=\"%s\" subscription=\"remove\"/></query></iq>", xuid);
		xfree(xuid);
	}
	print("user_deleted", target, session_name(session));
/* TODO: userlist_del() */
	
	return 0;
}

/*
 * Warning! This command is kinda special:
 * it needs destination uid, so destination must be in our userlist.
 * It won't work for unknown JIDs.
 * When implementing new command don't use jabber_command_ver as template.
 */

COMMAND(jabber_command_ver)
{
	const char *query_res, *uid;
        userlist_t *ut;
	if (!(uid = jid_target2uid(session, target, quiet)))
		return -1;

	if (!(ut = userlist_find(session, uid))) {
		print("user_not_found", session_name(session));
		return -1;
	}
	if (xstrcasecmp(ut->status, EKG_STATUS_NA) == 0) {
		print("jabber_status_notavail", session_name(session), ut->uid);
		return -1;
	}

	if (!(query_res = ut->resource)) {
		print("jabber_unknown_resource", session_name(session), target);
		return -1;
	}
	{
		jabber_private_t *j = session_private_get(session);
		char *xuid = jabber_escape(uid+4);
		char *xquery_res = jabber_escape(query_res);
       		jabber_write(j, "<iq id='%d' to='%s/%s' type='get'><query xmlns='jabber:iq:version'/></iq>", \
			     j->id++, xuid, xquery_res);
		xfree(xuid);
		xfree(xquery_res);
	}
	return 0;
}

COMMAND(jabber_command_userinfo)
{
	const char *uid;

	/* jabber id: [user@]host[/resource] */
	if (!(uid = jid_target2uid(session, target, quiet)))
		return -1;
	{ 
		jabber_private_t *j = session_private_get(session);
		char *xuid = jabber_escape(uid+4);
       		jabber_write(j, "<iq id='%d' to='%s' type='get'><vCard xmlns='vcard-temp'/></iq>", \
			     j->id++, xuid);
		xfree(xuid);
	}
	return 0;
}

COMMAND(jabber_command_lastseen)
{
	const char *uid;
	if (!(uid = jid_target2uid(session, target, quiet)))
		return -1;
#if 0 /* ? */
	if (!userlist_find(session, uid)) {
		print("user_not_found", session_name(session));
		return -1;
	}
#endif
	{
		jabber_private_t *j = session_private_get(session);
		char *xuid = jabber_escape(uid+4);
	       	jabber_write(j, "<iq id='%d' to='%s' type='get'><query xmlns='jabber:iq:last'/></iq>", \
			     j->id++, xuid);
		xfree(xuid);
	}
	return 0;
}

COMMAND(jabber_command_register)
{
	jabber_private_t *j = session_private_get(session);
	const char *server = params[0] ? params[0] : j->server;
	if (!params[1])
		jabber_write(j, "<iq type=\"get\" to=\"%s\" id=\"transpreg\" > <query xmlns=\"jabber:iq:register\"/> </iq>", server);
	else ;
	return 0;
}

COMMAND(jabber_command_transports) 
{
	jabber_private_t *j = session_private_get(session);
	const char *server = params[0] ? params[0] : j->server;
	
	jabber_write(j, "<iq type=\"get\" to=\"%s\" id=\"transplist\" > <query xmlns=\"http://jabber.org/protocol/disco#items\"/> </iq>", server);
	return 0;
}

COMMAND(jabber_muc_command_join) 
{
	/* params[0] - full channel name, 
	 * params[1] - nickname || default 
	 * params[2] - password || none
	 */
	jabber_private_t *j = session_private_get(session);
	char *password = (params[1] && params[2]) ? saprintf(" <password>%s</password> ", params[2]) : NULL;
	jabber_write(j, "<presence to='%s/%s'> <x xmlns='http://jabber.org/protocol/muc#user'>%s</x> </presence>", 
			params[0],
			params[1] ? params[1] : "darkjames",
			password ? password : "");

	xfree(password);
	return 0;
}

COMMAND(jabber_muc_command_part) 
{
	jabber_private_t *j = session_private_get(session);
	char *status;

//	return -1;

	status = params[1] ? saprintf(" <status>%s</status> ", params[1]) : NULL;

	jabber_write(j, "<presence to=\"%s/%s\" type=\"unavailable\">%s</presence>", target+4, "darkjames", status);

	xfree(status);
	return 0;
}

void jabber_register_commands()
{
#define JABBER_ONLY         SESSION_MUSTBELONG | SESSION_MUSTHASPRIVATE
#define JABBER_FLAGS        JABBER_ONLY  | SESSION_MUSTBECONNECTED
#define JABBER_FLAGS_TARGET JABBER_FLAGS | COMMAND_ENABLEREQPARAMS | COMMAND_PARAMASTARGET
	command_add(&jabber_plugin, "jid:", "?", jabber_command_inline_msg, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:_autoaway", "r", jabber_command_away,	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:_autoback", "r", jabber_command_away,	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:add", "!U ?", jabber_command_modify, 	JABBER_FLAGS_TARGET, NULL); 
	command_add(&jabber_plugin, "jid:auth", "!p !uU", jabber_command_auth, 	JABBER_FLAGS | COMMAND_ENABLEREQPARAMS, 
			"-a --accept -d --deny -r --request -c --cancel");
	command_add(&jabber_plugin, "jid:away", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:back", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:chat", "!uU !", jabber_command_msg, 	JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:connect", "r ?", jabber_command_connect, JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:del", "!u", jabber_command_del, 	JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:disconnect", "r ?", jabber_command_disconnect, JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:dnd", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:invisible", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:ffc", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:msg", "!uU !", jabber_command_msg, 	JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:modify", "!Uu !", jabber_command_modify,JABBER_FLAGS_TARGET, 
			"-n --nickname -g --group");
	command_add(&jabber_plugin, "jid:mucjoin", "! ? ?", jabber_muc_command_join, JABBER_FLAGS | COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&jabber_plugin, "jid:mucpart", "! ?", jabber_muc_command_part, JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:passwd", "!", jabber_command_passwd, 	JABBER_FLAGS | COMMAND_ENABLEREQPARAMS, NULL);
	command_add(&jabber_plugin, "jid:reconnect", NULL, jabber_command_reconnect, JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:transports", "? ?", jabber_command_transports, JABBER_FLAGS, NULL);
	command_add(&jabber_plugin, "jid:ver", "!u", jabber_command_ver, 	JABBER_FLAGS_TARGET, NULL); /* ??? ?? ? ?@?!#??#!@? */
	command_add(&jabber_plugin, "jid:userinfo", "!u", jabber_command_userinfo, JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:lastseen", "!u", jabber_command_lastseen, JABBER_FLAGS_TARGET, NULL);
	command_add(&jabber_plugin, "jid:register", "? ?", jabber_command_register, JABBER_FLAGS, NULL);
	command_add(&jabber_plugin, "jid:xa", "r", jabber_command_away, 	JABBER_ONLY, NULL);
	command_add(&jabber_plugin, "jid:xml", "!", jabber_command_xml, 	JABBER_ONLY | COMMAND_ENABLEREQPARAMS, NULL);
};

/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
