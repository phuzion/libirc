//This program is free software: you can redistribute it and/or modify
//it under the terms of the GNU Lesser General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU Lesser General Public License for more details.

// Copyright (c) Petr Bena 2015

#include <QtNetwork>
#include <QDebug>
#include "network.h"
#include "server.h"
#include "channel.h"
#include "parser.h"
#include "user.h"
#include "../libirc/serveraddress.h"
#include "../libirc/error_code.h"

using namespace libircclient;

Network::Network(libirc::ServerAddress &server, QString name) : libirc::Network(name)
{
    this->socket = NULL;
    this->hostname = server.GetHost();
    this->localUser.SetIdent("grumpy");
    if (server.GetNick().isEmpty())
        this->localUser.SetNick("GrumpyUser");
    else
        this->localUser.SetNick(server.GetNick());
    this->localUser.SetRealname("GrumpyIRC");
    this->usingSSL = server.UsingSSL();
    this->pingTimeout = 60;
    this->port = server.GetPort();
    this->timerPingTimeout = NULL;
    this->timerPingSend = NULL;
    this->pingRate = 20000;
    this->defaultQuit = "Grumpy IRC";
    this->channelPrefix = '#';
    this->autoRejoin = false;
    this->identifyString = "PRIVMSG NickServ identify $nickname $password";
    this->alternateNickNumber = 0;
    this->server = new Server();
    // This is overriden for every server that is following IRC standards
    this->channelUserPrefixes << '@' << '+';
    this->CUModes << 'o' << 'v';
    this->CModes << 'i' << 'm';
    this->ResolveOnNickConflicts = true;
    this->ChannelModeHelp.insert('m', "Moderated - will suppress all messages from people who don't have voice (+v) or higher.");
    this->ChannelModeHelp.insert('t', "Topic changes restricted - only allow privileged users to change the topic.");
}

Network::Network(QHash<QString, QVariant> hash) : libirc::Network("")
{
    this->socket = NULL;
    this->port = 0;
    this->server = NULL;
    this->timerPingSend = NULL;
    this->timerPingTimeout = NULL;
    this->ResolveOnNickConflicts = true;
    this->usingSSL = false;
    this->pingTimeout = 0;
    this->LoadHash(hash);
}

Network::~Network()
{
    delete this->server;
    this->deleteTimers();
    delete this->socket;
    qDeleteAll(this->channels);
    qDeleteAll(this->users);
}

void Network::Connect()
{
    if (this->IsConnected())
        return;
    //delete this->network_thread;
    delete this->socket;
    //this->network_thread = new NetworkThread(this);
    if (!this->IsSSL())
        this->socket = new QTcpSocket();
    else
        this->socket = new QSslSocket();

    /*
    if (!this->SSL)
    {
        connect(this->socket, SIGNAL(connected()), this, SLOT(OnConnected()));
        this->socket->connectToHost(this->hostname, this->port);
    }
    else
    {
        // We don't care about self signed certificates
        connect(((QSslSocket*)this->socket), SIGNAL(encrypted()), this, SLOT(OnConnected()));
        //QList<QSslError> errors;
        //errors << QSslError(QSslError::SelfSignedCertificate);
        //errors << QSslError(QSslError::HostNameMismatch);
        ((QSslSocket*)this->socket)->ignoreSslErrors();
        connect(((QSslSocket*)this->socket), SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(OnSslHandshakeFailure(QList<QSslError>)));
        ((QSslSocket*)this->socket)->connectToHostEncrypted(this->hostname, this->port);
        if (!((QSslSocket*)this->socket)->waitForEncrypted())
        {
            this->closeError("SSL handshake failed: " + this->socket->errorString());
        }
    }
     */

    connect(this->socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(OnError(QAbstractSocket::SocketError)));
    connect(this->socket, SIGNAL(readyRead()), this, SLOT(OnReceive()));

    if (!this->IsSSL())
    {
        connect(this->socket, SIGNAL(connected()), this, SLOT(OnConnected()));
        this->socket->connectToHost(this->hostname, this->port);
    } else
    {
        ((QSslSocket*)this->socket)->ignoreSslErrors();
        connect(((QSslSocket*)this->socket), SIGNAL(sslErrors(QList<QSslError>)), this, SLOT(OnSslHandshakeFailure(QList<QSslError>)));
        connect(((QSslSocket*)this->socket), SIGNAL(encrypted()), this, SLOT(OnConnected()));
        ((QSslSocket*)this->socket)->connectToHostEncrypted(this->hostname, this->port);
        if (this->socket && !((QSslSocket*)this->socket)->waitForEncrypted())
        {
            this->closeError("SSL handshake failed: " + this->socket->errorString(), EHANDSHAKE);
        }
    }
}

void Network::Reconnect()
{
    this->Disconnect();
    this->Connect();
}

void Network::Disconnect(QString reason)
{
    if (reason.isEmpty())
        reason = this->defaultQuit;
    if (this->IsConnected())
    {
        this->TransferRaw("QUIT :" + reason);
        this->socket->close();
    }
    delete this->socket;
    this->socket = NULL;
    this->deleteTimers();
}

bool Network::IsConnected()
{
    if (!this->socket)
        return false;

    return this->socket->isOpen();
}

void Network::SetDefaultNick(QString nick)
{
    if (!this->IsConnected())
        this->localUser.SetNick(nick);
}

void Network::SetDefaultIdent(QString ident)
{
    if (!this->IsConnected())
        this->localUser.SetIdent(ident);
}

void Network::SetDefaultUsername(QString realname)
{
    if (!this->IsConnected())
        this->localUser.SetRealname(realname);
}

bool Network::IsSSL()
{
    return this->usingSSL;
}

QString Network::GetNick()
{
    return this->localUser.GetNick();
}

QString Network::GetHost()
{
    return this->localUser.GetHost();
}

QString Network::GetIdent()
{
    return this->localUser.GetIdent();
}

void Network::TransferRaw(QString raw)
{
    if (!this->IsConnected())
        return;

    // Remove garbage for security reasons
    raw.replace("\r", "").replace("\n", "");

    QByteArray data = QString(raw + "\n").toUtf8();
    emit this->Event_RawOutgoing(data);
    this->socket->write(data);
    this->socket->flush();
}

#define SEPARATOR QString((char)1)

int Network::SendMessage(QString text, QString target)
{
    this->TransferRaw("PRIVMSG " + target + " :" + text);
    return SUCCESS;
}

int Network::SendAction(QString text, Channel *channel)
{
    return this->SendAction(text, channel->GetName());
}

int Network::SendAction(QString text, QString target)
{
    this->TransferRaw(QString("PRIVMSG ") + target + " :" + SEPARATOR + "ACTION " + text + SEPARATOR);
    return SUCCESS;
}

int Network::SendMessage(QString text, Channel *channel)
{
    return this->SendMessage(text, channel->GetName());
}

int Network::SendMessage(QString text, User *user)
{
    return this->SendMessage(text, user->GetNick());
}

void Network::RequestPart(QString channel_name)
{
    this->TransferRaw("PART " + channel_name);
}

void Network::RequestPart(Channel *channel)
{
    this->TransferRaw("PART " + channel->GetName());
}

void Network::RequestNick(QString nick)
{
    this->TransferRaw("NICK " + nick);
}

void Network::Identify(QString Nickname, QString Password)
{
    if (Nickname.isEmpty())
        Nickname = this->localUser.GetNick();
    if (Password.isEmpty())
        Password = this->password;
    QString ident_line = this->identifyString;
    ident_line.replace("$nickname", Nickname).replace("$password", Password);
    this->TransferRaw(ident_line);
}

QString Network::GetServerAddress()
{
    return this->hostname;
}

void Network::SetNick(QString nick)
{
    this->localUser.SetNick(nick);
}

int Network::GetTimeout() const
{
    return this->pingTimeout;
}

void Network::OnPing()
{
    // Check ping timeout
    if (QDateTime::currentDateTime() > this->lastPing.addSecs(this->pingTimeout))
    {
        emit this->Event_Timeout();
        this->Disconnect();
    }
}

void Network::SetPassword(QString Password)
{
    this->password = Password;
}

void Network::OnPingSend()
{
    this->TransferRaw("PING :" + this->GetServerAddress());
}

void Network::closeError(QString error, int code)
{
    // Delete the socket first to prevent neverending loop
    // for some reason when you call the destructor Qt emits
    // some errors again causing program to hang
    if (this->socket == NULL)
        return;
    QTcpSocket *temp = this->socket;
    this->socket = NULL;
    temp->close();
    delete temp;
    this->deleteTimers();
}

void Network::OnError(QAbstractSocket::SocketError er)
{
    if (this->socket == NULL)
        return;
    emit this->Event_ConnectionFailure(er);
    //! \todo Improve this
    // Write the proper error here
    this->closeError("Error", 1);
}

void Network::OnReceive()
{
    if (!this->IsConnected())
        return;
    while (this->socket->canReadLine())
    {
        QByteArray line = this->socket->readLine();
        if (line.length() == 0)
            return;

        emit this->Event_RawIncoming(line);

        this->processIncomingRawData(line);
    }
}

User *Network::GetLocalUserInfo()
{
    return &this->localUser;
}

char Network::StartsWithCUPrefix(QString user_name)
{
    if (user_name.isEmpty())
        return 0;
    char first_symbol = user_name[0].toLatin1();
    if (this->channelUserPrefixes.contains(first_symbol))
        return this->CUModes[this->channelUserPrefixes.indexOf(first_symbol)];
    return 0;
}

int Network::PositionOfUCPrefix(char prefix)
{
    // Check if there is such a prefix
    if (!this->channelUserPrefixes.contains(prefix))
        return -1;

    return this->channelUserPrefixes.indexOf(prefix);
}

void Network::SetChannelUserPrefixes(QList<char> data)
{
    this->channelUserPrefixes = data;
}

void Network::SetCModes(QList<char> data)
{
    this->CModes = data;
}

QList<char> Network::GetChannelUserPrefixes()
{
    return this->channelUserPrefixes;
}

QList<char> Network::GetCModes()
{
    return this->CModes;
}

QList<char> Network::GetCPModes()
{
    return this->CPModes;
}

void Network::SetCPModes(QList<char> data)
{
    this->CPModes = data;
}

void Network::SetCRModes(QList<char> data)
{
    this->CRModes = data;
}

QList<char> Network::GetCRModes()
{
    return this->CRModes;
}

void Network::SetCUModes(QList<char> data)
{
    this->CUModes = data;
}

void Network::SetCCModes(QList<char> data)
{
    this->CCModes = data;
}

QList<char> Network::GetCCModes()
{
    return this->CCModes;
}

#define UNSERIALIZE_CHARLIST(list) if (hash.contains(#list)) { list = deserializeList(hash[#list]); }

static QList<char> deserializeList(QVariant hash)
{
    QList<char> list;
    foreach (QVariant x, hash.toList())
        list.append(x.toChar().toLatin1());
    // here we go
    return list;
}

static QVariant serializeList(QList<char> data)
{
    QList<QVariant> result;
    foreach (char x, data)
        result.append(QVariant(QChar(x)));
    return QVariant(result);
}

void Network::LoadHash(QHash<QString, QVariant> hash)
{
    libirc::Network::LoadHash(hash);
    UNSERIALIZE_STRING(hostname);
    UNSERIALIZE_UINT(port);
    UNSERIALIZE_INT(pingTimeout);
    UNSERIALIZE_INT(pingRate);
    UNSERIALIZE_STRING(defaultQuit);
    UNSERIALIZE_BOOL(autoRejoin);
    UNSERIALIZE_BOOL(autoIdentify);
    UNSERIALIZE_STRING(identifyString);
    UNSERIALIZE_CHARLIST(CModes);
    UNSERIALIZE_CHARLIST(CCModes);
    UNSERIALIZE_CHARLIST(CUModes);
    UNSERIALIZE_CHARLIST(CPModes);
    UNSERIALIZE_CHARLIST(CRModes);
    UNSERIALIZE_CHARLIST(channelUserPrefixes);
    UNSERIALIZE_STRING(password);
    UNSERIALIZE_STRING(alternateNick);
    if (hash.contains("server"))
        this->server = new Server(hash["server"].toHash());
    if (hash.contains("users"))
    {
        foreach (QVariant user, hash["user"].toList())
            this->users.append(new User(user.toHash()));
    }
    if (hash.contains("channels"))
    {
        foreach (QVariant channel, hash["channels"].toList())
            this->channels.append(new Channel(channel.toHash()));
    }
    if (hash.contains("localUserMode"))
        this->localUserMode = UMode(hash["localUserMode"].toHash());
    if (hash.contains("localUser"))
        this->localUser = User(hash["localUser"].toHash());
}

QHash<QString, QVariant> Network::ToHash()
{
    QHash<QString, QVariant> hash = libirc::Network::ToHash();
    SERIALIZE(hostname);
    SERIALIZE(port);
    SERIALIZE(pingTimeout);
    SERIALIZE(pingRate);
    SERIALIZE(defaultQuit);
    SERIALIZE(autoRejoin);
    SERIALIZE(autoIdentify);
    SERIALIZE(identifyString);
    SERIALIZE(password);
    SERIALIZE(alternateNick);
    hash.insert("CCModes", serializeList(this->CCModes));
    hash.insert("CModes", serializeList(this->CModes));
    hash.insert("CPModes", serializeList(this->CPModes));
    hash.insert("CUModes", serializeList(this->CUModes));
    hash.insert("channelUserPrefixes", serializeList(this->channelUserPrefixes));
    hash.insert("CRModes", serializeList(this->CRModes));
    hash.insert("localUserMode", this->localUserMode.ToHash());
    SERIALIZE(channelPrefix);
    hash.insert("server", this->server->ToHash());
    hash.insert("localUser", this->localUser.ToHash());
    SERIALIZE(lastPing);
    QList<QVariant> users_x, channels_x;
    foreach (Channel *ch, this->channels)
        channels_x.append(QVariant(ch->ToHash()));
    hash.insert("channels", QVariant(channels_x));
    foreach (User *user, this->users)
        users_x.append(QVariant(user->ToHash()));
    hash.insert("users", QVariant(users_x));
    return hash;
}

void Network::OnSslHandshakeFailure(QList<QSslError> errors)
{
    bool temp = false;
    emit this->Event_SSLFailure(errors, &temp);
    if (!temp)
        ((QSslSocket*)this->socket)->ignoreSslErrors();
    else
        this->closeError("Requested by hook", EHANDSHAKE);
}

Channel *Network::GetChannel(QString channel_name)
{
    channel_name = channel_name.toLower();
    foreach(Channel *channel, this->channels)
    {
        if (channel->GetName().toLower() == channel_name)
        {
            return channel;
        }
    }
    return NULL;
}

QList<Channel *> Network::GetChannels()
{
    return this->channels;
}

QList<char> Network::GetCUModes()
{
    return this->CUModes;
}

bool Network::ContainsChannel(QString channel_name)
{
    return this->GetChannel(channel_name) != NULL;
}

void Network::_st_ClearChannels()
{
    qDeleteAll(this->channels);
    this->channels.clear();
}

Channel *Network::_st_InsertChannel(Channel *channel)
{
    Channel *cx = new Channel(channel);
    cx->SetNetwork(this);
    this->channels.append(cx);
    return cx;
}

void Network::OnConnected()
{
    // We just connected to an IRC network
    this->TransferRaw("USER " + this->localUser.GetIdent() + " 8 * :" + this->localUser.GetRealname());
    this->TransferRaw("NICK " + this->localUser.GetNick());
    this->lastPing = QDateTime::currentDateTime();
    this->deleteTimers();
    this->timerPingSend = new QTimer(this);
    connect(this->timerPingSend, SIGNAL(timeout()), this, SLOT(OnPingSend()));
    this->timerPingTimeout = new QTimer(this);
    this->timerPingSend->start(this->pingRate);
    connect(this->timerPingTimeout, SIGNAL(timeout()), this, SLOT(OnPing()));
    this->timerPingTimeout->start(2000);
}

static void DebugInvalid(QString message, Parser *parser)
{
    qDebug() << "IRC PARSER: " + message + ": " + parser->GetRaw();
}

void Network::processIncomingRawData(QByteArray data)
{
    this->lastPing = QDateTime::currentDateTime();
    QString l(data);
    // let's try to parse this IRC command
    Parser parser(l);
    if (!parser.IsValid())
    {
        emit this->Event_Invalid(data);
        return;
    }
    bool self_command = false;
    if (parser.GetSourceUserInfo() != NULL)
        self_command = parser.GetSourceUserInfo()->GetNick().toLower() == this->GetNick().toLower();
    // This is a fixup for our own hostname as seen by the server, it may actually change runtime
    // based on cloak mechanisms used by a server, so when it happens we need to update it
    if (self_command && !parser.GetSourceUserInfo()->GetHost().isEmpty() && parser.GetSourceUserInfo()->GetHost() != this->localUser.GetHost())
        this->localUser.SetHost(parser.GetSourceUserInfo()->GetHost());
    bool known = true;
    switch (parser.GetNumeric())
    {
        case IRC_NUMERIC_PING_CHECK:
            if (parser.GetParameters().count() == 0)
                this->TransferRaw("PONG");
            else
                this->TransferRaw("PONG :" + parser.GetParameters()[0]);
            break;
        case IRC_NUMERIC_MYINFO:
            // Process the information about network
            if (parser.GetParameters().count() < 4)
                break;
            this->server->SetName(parser.GetParameters()[0]);
            this->server->SetVersion(parser.GetParameters()[1]);
            break;

        case IRC_NUMERIC_JOIN:
        {
            Channel *channel_p = NULL;
            if (parser.GetParameters().count() < 1 && parser.GetText().isEmpty())
            {
                // broken
                DebugInvalid("Malformed JOIN", &parser);
                break;
            }
            // On some servers channel is passed as text and on some as parameter
            QString channel_name = parser.GetText();
            if (parser.GetParameters().count() > 0)
                channel_name = parser.GetParameters()[0];
            if (!channel_name.startsWith(this->channelPrefix))
            {
                DebugInvalid("Malformed JOIN", &parser);
                break;
            }
            // Check if the person who joined the channel isn't us
            if (self_command)
            {
                // Yes, we joined a new channel
                if (this->ContainsChannel(channel_name))
                {
                    // what the fuck?? we joined the channel which we are already in
                    qDebug() << "Server told us we just joined a channel we are already in: " + channel_name + " network: " + this->GetHost();
                    goto join_emit;
                }
                channel_p = new Channel(channel_name, this);
                this->channels.append(channel_p);
                emit this->Event_SelfJoin(channel_p);
            }
        join_emit:
            if (!channel_p)
                channel_p = this->GetChannel(channel_name);
            if (!channel_p)
            {
                qDebug() << "Server told us that user " + parser.GetSourceUserInfo()->ToString() + " joined channel " + channel_name + " which we are not in";
                break;
            }
            // Insert this user to a channel
            User *user = channel_p->InsertUser(parser.GetSourceUserInfo());
            emit this->Event_Join(&parser, user, channel_p);
        }   break;
        case IRC_NUMERIC_PRIVMSG:
            this->processPrivMsg(&parser);
            break;

        case IRC_NUMERIC_NOTICE:
            emit this->Event_NOTICE(&parser);
            break;

        case IRC_NUMERIC_NICK:
            this->processNick(&parser, self_command);
            break;

        case IRC_NUMERIC_QUIT:
        {
            // Remove the user from all channels
            foreach (Channel *channel, this->channels)
            {
                if (channel->ContainsUser(parser.GetSourceUserInfo()->GetNick()))
                {
                    channel->RemoveUser(parser.GetSourceUserInfo()->GetNick());
                    emit this->Event_PerChannelQuit(&parser, channel);
                }
            }
            emit this->Event_Quit(&parser);
        }   break;
        case IRC_NUMERIC_PART:
        {
            if (parser.GetParameters().count() < 1)
            {
                qDebug() << "IRC PARSER: Invalid PART: " + parser.GetRaw();
                break;
            }
            Channel *channel = this->GetChannel(parser.GetParameters()[0]);
            if (self_command)
            {
                if (!channel)
                {
                    DebugInvalid("Channel struct not in memory", &parser);
                }
                else
                {
                    emit this->Event_SelfPart(&parser, channel);
                    this->channels.removeOne(channel);
                    emit this->Event_Part(&parser, channel);
                    delete channel;
                    break;
                }
            }
            else if (channel)
            {
                channel->RemoveUser(parser.GetSourceUserInfo()->GetNick());
            }
            emit this->Event_Part(&parser, channel);
        }   break;
        case IRC_NUMERIC_KICK:
        {
            if (parser.GetParameters().count() < 2)
            {
                qDebug() << "IRC PARSER: Invalid KICK: " + parser.GetRaw();
                break;
            }
            Channel *channel = this->GetChannel(parser.GetParameters()[0]);
            if (self_command)
            {
                if (!channel)
                {
                    DebugInvalid("Channel struct not in memory", &parser);
                }
                else
                {
                    emit this->Event_SelfKick(&parser, channel);
                    this->channels.removeOne(channel);
                    emit this->Event_Kick(&parser, channel);
                    delete channel;
                    break;
                }
            }
            else if (channel)
            {
                channel->RemoveUser(parser.GetParameters()[1]);
            }
            emit this->Event_Kick(&parser, channel);
        }   break;
        case IRC_NUMERIC_PONG:
            break;
        case IRC_NUMERIC_MODE:
        {
            if (parser.GetParameters().count() < 1)
            {
                qDebug() << "IRC PARSER: Invalid MODE: " + parser.GetRaw();
                break;
            }
            QString entity = parser.GetParameters()[0];
            if (entity.toLower() == this->localUser.GetNick().toLower())
            {
                // Someone changed our own UMode
                this->localUserMode.SetMode(parser.GetText());
            } else if (entity.startsWith(this->channelPrefix))
            {
                // Someone changed a channel mode
            } else
            {
                // Someone changed UMode of another user, this is not supported on majority of servers, unless you are services
            }
            emit this->Event_Mode(&parser);
        }
            break;
        case IRC_NUMERIC_ISUPPORT:
            this->processInfo(&parser);
            emit this->Event_INFO(&parser);
            break;
        case IRC_NUMERIC_NAMREPLY:
            this->processNamrpl(&parser);
            break;
        case IRC_NUMERIC_ENDOFNAMES:
            emit this->Event_EndOfNames(&parser);
            break;
        case IRC_NUMERIC_TOPIC:
        {
            if (parser.GetParameters().count() < 1)
            {
                qDebug() << "IRC PARSER: Invalid TOPIC: " + parser.GetRaw();
                break;
            }
            Channel *channel = this->GetChannel(parser.GetParameters()[0]);
            if (!channel)
            {
                DebugInvalid("Channel struct not in memory", &parser);
                break;
            }
            QString topic = channel->GetTopic();
            channel->SetTopic(parser.GetText());
            emit this->Event_TOPIC(&parser, channel, topic);
        }
            break;
        case IRC_NUMERIC_TOPICINFO:
        {
            if (parser.GetParameters().count() < 2)
            {
                qDebug() << "IRC PARSER: Invalid TOPICINFO: " + parser.GetRaw();
                break;
            }
            Channel *channel = this->GetChannel(parser.GetParameters()[1]);
            if (!channel)
            {
                DebugInvalid("Channel struct not in memory", &parser);
                break;
            }
            channel->SetTopic(parser.GetText());
            emit this->Event_TOPICInfo(&parser, channel);
        }
            break;
        case IRC_NUMERIC_TOPICWHOTIME:
        {
            if (parser.GetParameters().count() < 2)
            {
                qDebug() << "IRC PARSER: Invalid TOPICWHOTIME: " + parser.GetRaw();
                break;
            }
            Channel *channel = this->GetChannel(parser.GetParameters()[1]);
            if (!channel)
            {
                DebugInvalid("Channel struct not in memory", &parser);
                break;
            }
            channel->SetTopicTime(QDateTime::fromTime_t(parser.GetText().toUInt()));
            emit this->Event_TOPICWhoTime(&parser, channel);
        }
            break;
        case IRC_NUMERIC_NICKUSED:
            this->process433(&parser);
            break;
        case IRC_NUMERIC_MOTD:
            emit this->Event_MOTD(&parser);
            break;
        case IRC_NUMERIC_MOTDBEGIN:
            emit this->Event_MOTDBegin(&parser);
            break;
        case IRC_NUMERIC_MOTDEND:
            emit this->Event_MOTDEnd(&parser);
            break;
		case IRC_NUMERIC_WHOREPLY:
			this->processWho(&parser);
			break;
        case IRC_NUMERIC_WHOEND:
            // 315
            emit this->Event_EndOfWHO(&parser);
            break;
        case IRC_NUMERIC_MODEINFO:
            this->processMode(&parser);
            break;
        case IRC_NUMERIC_MODETIME:
            this->processMTime(&parser);
            break;
        default:
            known = false;
            break;
    }
    if (!known)
        emit this->Event_Unknown(&parser);
    emit this->Event_Parse(&parser);
}

void Network::processNamrpl(Parser *parser)
{
    // 353 GrumpyUser = #support :GrumpyUser petan|home @petan %wm-bot &OperBot
    // Server sent us an initial list of users that are in the channel
    if (parser->GetParameters().size() < 3)
    {
        DebugInvalid("Malformed NAMRPL", parser);
        return;
    }

    Channel *channel = this->GetChannel(parser->GetParameters()[2]);
    if (channel == NULL)
    {
        DebugInvalid("Unknown channel", parser);
        return;
    }

    // Now insert new users one by one

    foreach (QString user, parser->GetText().split(" "))
    {
        if (user.isEmpty())
            continue;
        char cumode = this->StartsWithCUPrefix(user);
        char prefix = 0;
        if (cumode != 0)
        {
            prefix = user[0].toLatin1();
            user = user.mid(1);
        }
        User ux;
        ux.SetNick(user);
        ux.CUMode = cumode;
        ux.ChannelPrefix = prefix;
        channel->InsertUser(&ux);
    }
}

void Network::processInfo(Parser *parser)
{
    // WATCHOPTS=A SILENCE=15 MODES=12 CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=beI,kfL,lj,psmntirRcOAQKVCuzNSMTGZ NETWORK=tm-irc CASEMAPPING=ascii EXTBAN=~,qjncrRa ELIST=MNUCT STATUSMSG=~&@%+
    foreach (QString info, parser->GetParameters())
    {
        if (info.startsWith("PREFIX"))
        {
            QString cu, prefix;
            if (info.length() < 8 || !info.contains("("))
                goto broken_prefix;
            prefix = info.mid(8);
            if (prefix.contains("(") || !prefix.contains(")"))
                goto broken_prefix;
            cu = prefix.mid(0, prefix.indexOf(")"));
            prefix = prefix.mid(prefix.indexOf(")") + 1);
            if (cu.length() != prefix.length())
                goto broken_prefix;
            this->channelUserPrefixes.clear();
            this->CUModes.clear();
            foreach (QChar CUMode, cu)
                this->CUModes.append(CUMode.toLatin1());
            foreach (QChar pr, prefix)
                this->channelUserPrefixes.append(pr.toLatin1());

            continue;
            broken_prefix:
                qDebug() << "IRC PARSER: broken prefix: " + parser->GetRaw();
                break;
        }
        else if (info.startsWith("NETWORK="))
        {
            this->networkName = info.mid(8);
            continue;
        }
    }
    emit this->Event_MyInfo(parser);
}

void Network::processWho(Parser *parser)
{
    // GrumpyUser1 #support grumpy hidden-715465F6.net.upcbroadband.cz hub.tm-irc.org GrumpyUser1 H
    QStringList parameters = parser->GetParameters();
    Channel *channel = NULL;
    User *user = NULL;
    if (parameters.count() < 7)
        goto finish;
    if (!parameters[1].startsWith(this->channelPrefix))
        goto finish;

    // Find a channel related to this message and update the user details
    channel = this->GetChannel(parameters[1]);
    if (!channel)
        goto finish;
    user = channel->GetUser(parameters[5]);
    if (!user)
        goto finish;
    user->SetRealname(parser->GetText());
    user->SetIdent(parameters[2]);
    user->SetHost(parameters[3]);
    user->ServerName = parameters[4];

    finish:
	    emit this->Event_WHO(parser, channel, user);
}

void Network::processPrivMsg(Parser *parser)
{
    if (parser->GetParameters().count() < 1)
    {
        qDebug() << "IRC PARSER: Malformed PRIVMSG: " + parser->GetRaw();
        return;
    }

    QString text = parser->GetText();
    if (text.startsWith(SEPARATOR))
    {
        // This is a CTCP message
        text = text.mid(1);
        if (text.endsWith(SEPARATOR))
            text = text.mid(0, text.size() - 1);

        // These are usually split by space
        QString command = text;
        QString parameters;
        if (text.contains(" "))
        {
            parameters = command.mid(command.indexOf(" ") + 1);
            command = command.mid(0, command.indexOf(" "));
        }
        emit this->Event_CTCP(parser, command, parameters);
    } else
    {
        emit this->Event_PRIVMSG(parser);
    }
}

void Network::processMode(Parser *parser)
{

}

void Network::processMTime(Parser *parser)
{
    
}

void Network::processNick(Parser *parser, bool self_command)
{
    QString new_nick;
    if (parser->GetParameters().count() < 1)
    {
        if (parser->GetText().isEmpty())
        {
            // wrong amount of parameters
            qDebug() << "IRC PARSER: Malformed NICK: " + parser->GetRaw();
            return;
        }
        else
        {
            new_nick = parser->GetText();
        }
    }
    else
    {
        new_nick = parser->GetParameters()[0];
    }
    // Precache the nicks to save hundreds of function calls
    QString old_nick = parser->GetSourceUserInfo()->GetNick();

    if (self_command)
    {
        // our own nick was changed
        this->localUser.SetNick(new_nick);
        emit this->Event_SelfNICK(parser, old_nick, new_nick);
    }
    // Change the nicks in every channel this user is in
    foreach (Channel *channel, this->channels)
        channel->ChangeNick(old_nick, new_nick);
    emit this->Event_NICK(parser, old_nick, new_nick);
}

void Network::process433(Parser *parser)
{
    // Try to get some alternative nick
    bool no_alternative = this->alternateNick.isEmpty();
    if (this->originalNick == this->alternateNick || this->GetNick() == this->alternateNick)
        no_alternative = true;
    if (no_alternative)
    {
        // We need to request original nick with a suffix number
        this->alternateNickNumber++;
        if (this->originalNick.isEmpty())
            this->originalNick = this->GetNick();
        QString nick = this->originalNick + QString::number(this->alternateNickNumber);
        this->localUser.SetNick(nick);
        this->RequestNick(nick);
    }
    else
    {
        this->RequestNick(this->alternateNick);
        this->localUser.SetNick(this->alternateNick);
    }
    emit this->Event_NickCollision(parser);
}

void Network::deleteTimers()
{
    if (this->timerPingSend)
    {
        this->timerPingSend->stop();
        delete this->timerPingSend;
        this->timerPingSend = NULL;
    }
    if (this->timerPingTimeout)
    {
        this->timerPingTimeout->stop();
        delete this->timerPingTimeout;
        this->timerPingTimeout = NULL;
    }
}
