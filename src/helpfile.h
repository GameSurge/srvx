/* helpfile.h - Help file loading and display
 * Copyright 2000-2004 srvx Development Team
 *
 * This file is part of srvx.
 *
 * srvx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with srvx; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#if !defined(HELPFILE_H)
#define HELPFILE_H

#include "common.h"

struct userNode;
struct handle_info;
struct string_list;

extern struct userNode *message_dest; /* message destination; useful in expansion callbacks */

#define MIN_LINE_SIZE		40
#define MAX_LINE_SIZE		450

#define TABLE_REPEAT_HEADERS 0x0001 /* repeat the headers for each columnset? */
#define TABLE_PAD_LEFT       0x0002 /* pad cells on the left? */
#define TABLE_REPEAT_ROWS    0x0004 /* put more than one row on a line? */
#define TABLE_NO_FREE        0x0008 /* don't free the contents? */
#define TABLE_NO_HEADERS     0x0010 /* is there actually no header? */

struct helpfile_table {
    unsigned int length : 16;
    unsigned int width : 8;
    unsigned int flags : 8;
    const char ***contents;
};

struct helpfile_expansion {
    enum { HF_STRING, HF_TABLE } type;
    union {
        char *str;
        struct helpfile_table table;
    } value;
};

typedef struct helpfile_expansion (*expand_func_t)(const char *variable);
typedef void (*irc_send_func)(struct userNode *from, const char *to, const char *msg);

struct helpfile {
    const char *name;
    struct dict *db;
    expand_func_t expand;
};

struct language
{
    char *name;
    struct language *parent;
    struct dict *messages; /* const char* -> const char* */
    struct dict *helpfiles; /* phelpfile->name -> phelpfile */
};
extern struct language *lang_C;
extern struct dict *languages;

#define MSG_TYPE_NOTICE    0
#define MSG_TYPE_PRIVMSG   1
#define MSG_TYPE_WALLCHOPS 2
#define MSG_TYPE_NOXLATE   4
#define MSG_TYPE_MULTILINE 8

int send_message(struct userNode *dest, struct userNode *src, const char *message, ...);
int send_message_type(int msg_type, struct userNode *dest, struct userNode *src, const char *message, ...);
int send_target_message(int msg_type, const char *dest, struct userNode *src, const char *format, ...);
int send_help(struct userNode *dest, struct userNode *src, struct helpfile *hf, const char *topic);
/* size is maximum line width (up to MAX_LINE_SIZE); 0 means figure it out.
 * irc_send is either irc_privmsg or irc_notice; NULL means figure it out. */
void table_send(struct userNode *from, const char *to, unsigned int size, irc_send_func irc_send, struct helpfile_table table);

#define send_channel_message(CHANNEL, ARGS...) send_target_message(5, (CHANNEL)->name, ARGS)
#define send_channel_notice(CHANNEL, ARGS...) send_target_message(4, (CHANNEL)->name, ARGS)
#define send_channel_wallchops(CHANNEL, ARGS...) send_target_message(6, (CHANNEL)->name, ARGS)

struct message_entry
{
    const char *msgid;
    const char *format;
};
void message_register_table(const struct message_entry *table);
struct language *language_find(const char *name);
const char *language_find_message(struct language *lang, const char *msgid);
#define handle_find_message(HANDLE, MSGID) language_find_message((HANDLE) ? (HANDLE)->language : lang_C, (MSGID))
#define user_find_message(USER, MSGID) language_find_message((USER)->handle_info ? (USER)->handle_info->language : lang_C, (MSGID))
void helpfile_init(void);
void helpfile_finalize(void);

struct helpfile *open_helpfile(const char *fname, expand_func_t expand);
void close_helpfile(struct helpfile *hf);

#endif
