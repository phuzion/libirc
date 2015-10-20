//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU Lesser General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU Lesser General Public License for more details.

// Copyright (c) Petr Bena 2015


#ifndef PARSER_H
#define PARSER_H

#include <QString>
#include <QList>
#include "libircclient_global.h"
#include "user.h"

#define IRC_NUMERIC_UNKNOWN      -200
#define IRC_NUMERIC_PING_CHECK   -1
#define IRC_NUMERIC_PRIVMSG      -2
#define IRC_NUMERIC_NOTICE       -3
#define IRC_NUMERIC_KICK         -4
#define IRC_NUMERIC_QUIT         -5
#define IRC_NUMERIC_PART         -6
#define IRC_NUMERIC_JOIN         -7
#define IRC_NUMERIC_NICK         -8
#define IRC_NUMERIC_MODE         -9
#define IRC_NUMERIC_TOPIC        -10
#define IRC_NUMERIC_CTCP         -11
#define IRC_NUMERIC_PONG         0
#define IRC_NUMERIC_WELCOME      1
#define IRC_NUMERIC_YOURHOST     2
#define IRC_NUMERIC_CREATED      3
#define IRC_NUMERIC_MYINFO       4
#define IRC_NUMERIC_BOUNCE       5
#define IRC_NUMERIC_ISUPPORT     5
#define IRC_NUMERIC_MAP          6
#define IRC_NUMERIC_MAPEND       7
#define IRC_NUMERIC_SNOMASK      8
#define IRC_NUMERIC_STATMEMTOT   9
//#define IRC_NUMERIC_BOUNCE       10
#define IRC_NUMERIC_YOURCOOKIE   14
#define IRC_NUMERIC_YOURID       42
#define IRC_NUMERIC_SAVENICK     43
#define IRC_NUMERIC_TRACELINK    200
#define IRC_NUMERIC_STATS        210
#define IRC_NUMERIC_AWAY         301
#define IRC_NUMERIC_LISTSTART    321
#define IRC_NUMERIC_LIST         322
#define IRC_NUMERIC_LISTEND      323
#define IRC_NUMERIC_NOTOPIC      331
#define IRC_NUMERIC_TOPICINFO    332
#define IRC_NUMERIC_TOPICWHOTIME 333
#define IRC_NUMERIC_BADCHANPASS  339
#define IRC_NUMERIC_WHOREPLY     352
#define IRC_NUMERIC_NAMREPLY     353
#define IRC_NUMERIC_WHOSPCRPL    354
#define IRC_NUMERIC_ENDOFNAMES   366
#define IRC_NUMERIC_YOUREOPER    381
#define IRC_NUMERIC_NICKUSED     433

// 353 GrumpyUser = #support :GrumpyUser petan|home @petan %wm-bot &OperBot

namespace libircclient
{
    class LIBIRCCLIENTSHARED_EXPORT Parser
    {
        public:
            Parser(QString incoming_text);
            ~Parser();
            int GetNumeric();
            bool IsValid();
            User *GetSourceUserInfo();
            QString GetSourceInfo();
            QString GetParameterLine();
            QString GetRaw();
            QString GetText();
            QList<QString> GetParameters();

        private:
            void obtainNumeric();
            User *user;
            QString source;
            QString text;
            QString message_text;
            bool _valid;
            int _numeric;
            QList<QString> parameters;
            QString command;
            QString parameterLine;

    };
}

#endif // PARSER_H