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

#include <catch2/catch.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QUuid>
#include <QtConcurrent>

#ifndef Q_OS_WIN
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "windowendpoint.h"

namespace {

QString uniqueName()
{
    return QUuid::createUuid().toString( QUuid::WithoutBraces );
}

QString logPath( const char* name )
{
    return QDir::temp().absoluteFilePath( QString::fromUtf8( name ) );
}

// The receiver needs the event loop of this thread, the sender blocks
template <typename Function>
auto whileProcessingEvents( Function&& function )
{
    auto future = QtConcurrent::run( std::forward<Function>( function ) );
    while ( !future.isFinished() ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    }
    return future.result();
}

} // namespace

TEST_CASE( "Claiming a free name succeeds", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint endpoint;

    REQUIRE( endpoint.claim( name ) );
    REQUIRE( endpoint.name() == name );
}

TEST_CASE( "A name held by another endpoint is refused", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );

    WindowEndpoint other;
    REQUIRE_FALSE( other.claim( name ) );
    REQUIRE( other.name().isEmpty() );
}

TEST_CASE( "Case and surrounding spaces do not make a different name", "[windowendpoint]" )
{
    const auto name = "Abc" + uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );

    WindowEndpoint other;
    REQUIRE_FALSE( other.claim( "  " + name.toUpper() + " " ) );
}

TEST_CASE( "A blank name cannot be claimed", "[windowendpoint]" )
{
    WindowEndpoint endpoint;
    REQUIRE_FALSE( endpoint.claim( "   " ) );
}

TEST_CASE( "Releasing a name makes it free again", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint first;
    REQUIRE( first.claim( name ) );
    first.release();
    REQUIRE( first.name().isEmpty() );

    WindowEndpoint second;
    REQUIRE( second.claim( name ) );
}

TEST_CASE( "A destroyed endpoint frees its name", "[windowendpoint]" )
{
    const auto name = uniqueName();
    {
        WindowEndpoint first;
        REQUIRE( first.claim( name ) );
    }

    WindowEndpoint second;
    REQUIRE( second.claim( name ) );
}

TEST_CASE( "Claiming a new name frees the previous one", "[windowendpoint]" )
{
    const auto oldName = uniqueName();
    const auto newName = uniqueName();
    WindowEndpoint endpoint;
    REQUIRE( endpoint.claim( oldName ) );
    REQUIRE( endpoint.claim( newName ) );
    REQUIRE( endpoint.name() == newName );

    WindowEndpoint other;
    REQUIRE( other.claim( oldName ) );
    REQUIRE_FALSE( other.claim( newName ) );
}

TEST_CASE( "A refused claim keeps the current name", "[windowendpoint]" )
{
    const auto taken = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( taken ) );

    const auto current = uniqueName();
    WindowEndpoint endpoint;
    REQUIRE( endpoint.claim( current ) );
    REQUIRE_FALSE( endpoint.claim( taken ) );
    REQUIRE( endpoint.name() == current );

    WindowEndpoint other;
    REQUIRE_FALSE( other.claim( current ) );
}

TEST_CASE( "An endpoint can change the case of its own name", "[windowendpoint]" )
{
    const auto name = "abc" + uniqueName();
    WindowEndpoint endpoint;
    REQUIRE( endpoint.claim( name ) );
    REQUIRE( endpoint.claim( name.toUpper() ) );
    REQUIRE( endpoint.name() == name.toUpper() );

    WindowEndpoint other;
    REQUIRE_FALSE( other.claim( name ) );
}

#ifndef Q_OS_WIN
TEST_CASE( "A stale socket file does not block the name", "[windowendpoint]" )
{
    const auto name = uniqueName();
    const auto path = WindowEndpoint::serverName( name ).toLocal8Bit();
    REQUIRE( QFileInfo( QString::fromLocal8Bit( path ) ).isAbsolute() );

    {
        // Makes sure the folder exists, the same way a crashed process would have left it
        WindowEndpoint endpoint;
        REQUIRE( endpoint.claim( name ) );
    }

    const auto fd = ::socket( AF_UNIX, SOCK_STREAM, 0 );
    REQUIRE( fd >= 0 );
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    REQUIRE( static_cast<size_t>( path.size() ) < sizeof( address.sun_path ) );
    ::strncpy( address.sun_path, path.constData(), sizeof( address.sun_path ) - 1 );
    REQUIRE( ::bind( fd, reinterpret_cast<sockaddr*>( &address ), sizeof( address ) ) == 0 );
    ::close( fd );
    REQUIRE( QFile::exists( QString::fromLocal8Bit( path ) ) );

    WindowEndpoint endpoint;
    REQUIRE( endpoint.claim( name ) );
}
#endif

TEST_CASE( "A free name is claimed instead of forwarded", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint endpoint;

    REQUIRE( endpoint.claimOrForward( name, { logPath( "a.log" ) }, false )
             == WindowEndpoint::Route::Claimed );
    REQUIRE( endpoint.name() == name );
}

TEST_CASE( "Files reach the window holding the name", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );
    QSignalSpy received( &holder, &WindowEndpoint::filesReceived );

    const QStringList files{ logPath( "a.log" ), logPath( "\xE5\x95\x8F\xE9\xA1\x8C.log" ) };
    const auto route = whileProcessingEvents( [ & ] {
        WindowEndpoint sender;
        return sender.claimOrForward( name, files, true );
    } );

    REQUIRE( route == WindowEndpoint::Route::Forwarded );
    REQUIRE( received.count() == 1 );
    REQUIRE( received.at( 0 ).at( 0 ).toStringList() == files );
    REQUIRE( received.at( 0 ).at( 1 ).toBool() );
}

TEST_CASE( "A window can be raised without files", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );
    QSignalSpy received( &holder, &WindowEndpoint::filesReceived );

    const auto route = whileProcessingEvents( [ & ] {
        WindowEndpoint sender;
        return sender.claimOrForward( name, {}, false );
    } );

    REQUIRE( route == WindowEndpoint::Route::Forwarded );
    REQUIRE( received.count() == 1 );
    REQUIRE( received.at( 0 ).at( 0 ).toStringList().isEmpty() );
}

TEST_CASE( "The sender does not hold the name after forwarding", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );

    const auto senderName = whileProcessingEvents( [ & ] {
        WindowEndpoint sender;
        sender.claimOrForward( name, { logPath( "a.log" ) }, false );
        return sender.name();
    } );

    REQUIRE( senderName.isEmpty() );
}

TEST_CASE( "A window that does not answer is reported, not replaced", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );

    // No event loop here, the holder accepts nothing and never answers
    WindowEndpoint sender;
    QElapsedTimer timer;
    timer.start();
    const auto route = sender.claimOrForward( name, { logPath( "a.log" ) }, false, 300 );

    REQUIRE( route == WindowEndpoint::Route::Failed );
    REQUIRE( sender.name().isEmpty() );
    REQUIRE( timer.elapsed() < 3000 );
}

TEST_CASE( "Invalid messages are ignored", "[windowendpoint]" )
{
    const auto name = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( name ) );
    QSignalSpy received( &holder, &WindowEndpoint::filesReceived );

    const auto send = [ & ]( const QByteArray& message ) {
        QLocalSocket socket;
        socket.connectToServer( WindowEndpoint::serverName( name ) );
        REQUIRE( socket.waitForConnected( 1000 ) );
        socket.write( message );
        socket.waitForBytesWritten( 1000 );
        for ( auto i = 0; i < 20; ++i ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
        }
        socket.disconnectFromServer();
        for ( auto i = 0; i < 20; ++i ) {
            QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
        }
    };

    send( "not json\n" );
    send( "{\"version\":999,\"files\":[\"/tmp/a.log\"]}\n" );
    send( "{\"version\":1,\"files\":\"/tmp/a.log\"}\n" );
    send( "{\"version\":1,\"files\":[1]}\n" );
    send( "{\"version\":1,\"files\":[\"relative.log\"]}\n" );
    send( QByteArray( 2 * 1024 * 1024, 'x' ) );

    REQUIRE( received.isEmpty() );

    // Still alive after all of that
    const auto route = whileProcessingEvents( [ & ] {
        WindowEndpoint sender;
        return sender.claimOrForward( name, { logPath( "a.log" ) }, false );
    } );
    REQUIRE( route == WindowEndpoint::Route::Forwarded );
}
