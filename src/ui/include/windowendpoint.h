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

#ifndef WINDOWENDPOINT_H
#define WINDOWENDPOINT_H

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

// The name of a window, unique among the flogg processes of the user. Every named window
// listens on a local socket derived from its name, so a new flogg process can find it
// without a registry.
class WindowEndpoint : public QObject {
    Q_OBJECT

  public:
    enum class Route {
        Claimed, // the name was free and is now held by this endpoint
        Forwarded, // a window with that name took the files
        Failed, // the name is in use but its window did not answer
    };

    explicit WindowEndpoint( QObject* parent = nullptr );
    ~WindowEndpoint() override;

    // Holds the name, replacing the previous one. Returns false if it is in use elsewhere.
    bool claim( const QString& name );
    void release();
    QString name() const;

    // Gives the files to the window holding the name, or holds the name when it is free
    Route claimOrForward( const QString& name, const QStringList& files, bool follow,
                          int timeoutMs = DefaultTimeoutMs );

    // Empty when the name cannot be used
    static QString normalizedName( const QString& name );
    // Address of the local socket, for QLocalServer and QLocalSocket
    static QString serverName( const QString& name );

    static constexpr int DefaultTimeoutMs = 5000;

  Q_SIGNALS:
    void filesReceived( const QStringList& files, bool follow );

  private:
    bool listen( const QString& name );
    void onNewConnection();
    void onReadyRead( QLocalSocket* socket );

    std::unique_ptr<QLocalServer> server_;
    QString name_;
};

#endif
