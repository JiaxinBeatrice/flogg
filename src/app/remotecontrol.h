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

#ifndef KLOGG_REMOTECONTROL_H
#define KLOGG_REMOTECONTROL_H

#include <algorithm>
#include <functional>
#include <optional>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QThread>

#include "log.h"

// The flogg started first with a settings file takes the command lines of the ones started
// later with the same settings: each request is one line of JSON, answered by one line
// of JSON on the same local socket.
namespace remote {

// Requests bigger than this are dropped
constexpr qint64 MaxRequestSize = 4 * 1024 * 1024;

// Name shared by the instances of a user using the same settings file
inline QString instanceName( const QString& settingsFile )
{
    const auto key = QDir::homePath() + '\n' + QFileInfo( settingsFile ).absoluteFilePath();
    const auto hash = QCryptographicHash::hash( key.toUtf8(), QCryptographicHash::Sha1 );
    return "flogg-" + QString::fromLatin1( hash.toHex().left( 16 ) );
}

class ControlServer : public QObject {
    Q_OBJECT

  public:
    using Handler = std::function<QJsonObject( const QJsonObject& )>;

    explicit ControlServer( Handler handler, QObject* parent = nullptr )
        : QObject( parent )
        , handler_( std::move( handler ) )
    {
        connect( &server_, &QLocalServer::newConnection, this, &ControlServer::acceptConnections );
    }

    // Only the primary instance listens: a socket left by one that crashed is removed
    bool listen( const QString& name )
    {
        server_.setSocketOptions( QLocalServer::UserAccessOption );
        if ( server_.listen( name ) ) {
            return true;
        }
        QLocalServer::removeServer( name );
        if ( !server_.listen( name ) ) {
            LOG_ERROR << "Can't listen for other flogg starts on " << name << ": "
                      << server_.errorString();
            return false;
        }
        return true;
    }

  private:
    void acceptConnections()
    {
        while ( auto* socket = server_.nextPendingConnection() ) {
            connect( socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater );
            connect( socket, &QLocalSocket::readyRead, this,
                     [ this, socket ] { readRequest( socket ); } );
        }
    }

    void readRequest( QLocalSocket* socket )
    {
        if ( !socket->canReadLine() ) {
            if ( socket->bytesAvailable() > MaxRequestSize ) {
                socket->abort();
            }
            return;
        }

        const auto request = QJsonDocument::fromJson( socket->readLine() ).object();
        const auto reply = handler_( request );
        socket->write( QJsonDocument( reply ).toJson( QJsonDocument::Compact ) + '\n' );
        socket->flush();
        socket->disconnectFromServer();
    }

    Handler handler_;
    QLocalServer server_;
};

// Sends a request to the primary instance and waits for its reply, nothing when it can't
// be reached in time
inline std::optional<QJsonObject> sendRequest( const QString& name, const QJsonObject& request,
                                               int timeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    const auto remaining = [ & ] {
        return std::max( 1, timeoutMs - static_cast<int>( timer.elapsed() ) );
    };

    // The primary instance may still be starting
    QLocalSocket socket;
    while ( true ) {
        socket.connectToServer( name );
        if ( socket.waitForConnected( std::min( 500, remaining() ) ) ) {
            break;
        }
        if ( timer.elapsed() >= timeoutMs ) {
            return {};
        }
        QThread::msleep( 100 );
    }

    socket.write( QJsonDocument( request ).toJson( QJsonDocument::Compact ) + '\n' );
    if ( !socket.waitForBytesWritten( remaining() ) ) {
        return {};
    }

    QByteArray reply;
    while ( !reply.endsWith( '\n' ) ) {
        if ( !socket.bytesAvailable() && !socket.waitForReadyRead( remaining() ) ) {
            return {};
        }
        reply += socket.readAll();
        if ( timer.elapsed() >= timeoutMs ) {
            return {};
        }
    }

    return QJsonDocument::fromJson( reply ).object();
}

} // namespace remote

#endif
