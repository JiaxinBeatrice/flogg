/*
 * Copyright (C) 2026 flogg contributors
 *
 * This file is part of klogg.
 *
 * klogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * klogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with klogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>

#include "log.h"
#include "windowendpoint.h"

namespace {

constexpr int ProtocolVersion = 1;
constexpr qint64 MaxMessageSize = 1024 * 1024;
const char* const Acknowledge = "ok\n";

// Fixed under the home folder: the temporary folder can be a different one in a shell than
// in the desktop session, on macOS for example
QString runtimeFolder()
{
    const auto folder = QDir::home().filePath( ".flogg-windows" );
    if ( QDir().mkpath( folder ) ) {
        QFile::setPermissions( folder,
                               QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner );
    }
    return folder;
}

QString nameHash( const QString& name )
{
    const auto key = QDir::homePath() + '\n' + WindowEndpoint::normalizedName( name ).toCaseFolded();
    return QString::fromLatin1(
        QCryptographicHash::hash( key.toUtf8(), QCryptographicHash::Sha1 ).toHex().left( 24 ) );
}

bool connectTo( QLocalSocket& socket, const QString& name, int timeoutMs )
{
    socket.connectToServer( WindowEndpoint::serverName( name ) );
    return socket.waitForConnected( timeoutMs );
}

bool forward( QLocalSocket& socket, const QStringList& files, bool follow, int timeoutMs )
{
    auto message = QJsonDocument( QJsonObject{ { "version", ProtocolVersion },
                                               { "files", QJsonArray::fromStringList( files ) },
                                               { "follow", follow } } )
                       .toJson( QJsonDocument::Compact );
    message.append( '\n' );

    if ( message.size() > MaxMessageSize ) {
        LOG_ERROR << "Too many files for one message";
        return false;
    }

    socket.write( message );
    QDeadlineTimer deadline( timeoutMs );
    if ( !socket.waitForBytesWritten( timeoutMs ) ) {
        return false;
    }

    while ( !socket.canReadLine() ) {
        if ( !socket.waitForReadyRead( static_cast<int>( deadline.remainingTime() ) ) ) {
            return false;
        }
    }
    return socket.readLine() == Acknowledge;
}

// Nothing but what forward() writes opens a file
bool parse( const QByteArray& line, QStringList* files, bool* follow )
{
    const auto message = QJsonDocument::fromJson( line ).object();
    const auto list = message.value( "files" );
    if ( message.value( "version" ).toInt() != ProtocolVersion || !list.isArray() ) {
        return false;
    }

    files->clear();
    const auto array = list.toArray();
    for ( const auto& value : array ) {
        if ( !value.isString() || !QFileInfo( value.toString() ).isAbsolute() ) {
            return false;
        }
        files->append( value.toString() );
    }
    *follow = message.value( "follow" ).toBool();
    return true;
}

} // namespace

WindowEndpoint::WindowEndpoint( QObject* parent )
    : QObject( parent )
{
}

WindowEndpoint::~WindowEndpoint() = default;

bool WindowEndpoint::claim( const QString& name )
{
    const auto normalized = normalizedName( name );
    if ( normalized.isEmpty() ) {
        return false;
    }

    if ( !name_.isEmpty() && serverName( name_ ) == serverName( normalized ) ) {
        name_ = normalized;
        return true;
    }

    QLockFile lock( runtimeFolder() + '/' + nameHash( normalized ) + ".lock" );
    if ( !lock.tryLock( 2 * DefaultTimeoutMs ) ) {
        return false;
    }

    QLocalSocket probe;
    return !connectTo( probe, normalized, DefaultTimeoutMs ) && listen( normalized );
}

void WindowEndpoint::release()
{
    server_.reset();
    name_.clear();
}

QString WindowEndpoint::name() const
{
    return name_;
}

WindowEndpoint::Route WindowEndpoint::claimOrForward( const QString& name,
                                                      const QStringList& files, bool follow,
                                                      int timeoutMs )
{
    const auto normalized = normalizedName( name );
    if ( normalized.isEmpty() ) {
        return Route::Failed;
    }

    // Two starts at the same time must not both end up with a window
    QLockFile lock( runtimeFolder() + '/' + nameHash( normalized ) + ".lock" );
    if ( !lock.tryLock( 2 * timeoutMs ) ) {
        return Route::Failed;
    }

    QLocalSocket socket;
    if ( connectTo( socket, normalized, timeoutMs ) ) {
        return forward( socket, files, follow, timeoutMs ) ? Route::Forwarded : Route::Failed;
    }

    return listen( normalized ) ? Route::Claimed : Route::Failed;
}

QString WindowEndpoint::normalizedName( const QString& name )
{
    return name.simplified();
}

QString WindowEndpoint::serverName( const QString& name )
{
#ifdef Q_OS_WIN
    return "flogg-window-" + nameHash( name );
#else
    return runtimeFolder() + '/' + nameHash( name );
#endif
}

bool WindowEndpoint::listen( const QString& name )
{
    auto server = std::make_unique<QLocalServer>();
    server->setSocketOptions( QLocalServer::UserAccessOption );
    connect( server.get(), &QLocalServer::newConnection, this,
             &WindowEndpoint::onNewConnection );

    const auto address = serverName( name );
    if ( !server->listen( address ) ) {
        // Nobody answered, a crashed process left its socket behind
        QLocalServer::removeServer( address );
        if ( !server->listen( address ) ) {
            LOG_ERROR << "Can't listen for window " << name << ": " << server->errorString();
            return false;
        }
    }

    server_ = std::move( server );
    name_ = name;
    return true;
}

void WindowEndpoint::onNewConnection()
{
    while ( auto* socket = server_->nextPendingConnection() ) {
        connect( socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater );
        connect( socket, &QLocalSocket::readyRead, this,
                 [ this, socket ] { onReadyRead( socket ); } );
        if ( socket->bytesAvailable() > 0 ) {
            onReadyRead( socket );
        }
    }
}

void WindowEndpoint::onReadyRead( QLocalSocket* socket )
{
    if ( !socket->canReadLine() ) {
        if ( socket->bytesAvailable() > MaxMessageSize ) {
            socket->abort();
        }
        return;
    }

    QStringList files;
    bool follow = false;
    const auto line = socket->readLine( MaxMessageSize );
    if ( !parse( line, &files, &follow ) ) {
        LOG_ERROR << "Invalid message for window " << name_;
        socket->abort();
        return;
    }

    // Before the files load: the sender only waits to know they arrived
    socket->write( Acknowledge );
    socket->flush();

    Q_EMIT filesReceived( files, follow );
}
