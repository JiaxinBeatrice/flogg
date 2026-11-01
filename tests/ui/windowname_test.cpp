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
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QUuid>
#include <QtConcurrent>

#include "test_utils.h"

#include <QMenuBar>

#include "mainwindow.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "windowendpoint.h"

namespace {

QString uniqueName()
{
    return "bug " + QUuid::createUuid().toString( QUuid::WithoutBraces );
}

bool hasListener( const QString& name )
{
    QLocalSocket socket;
    socket.connectToServer( WindowEndpoint::serverName( name ) );
    return socket.waitForConnected( 1000 );
}

WindowEndpoint::Route forwardFile( const QString& name, const QString& file )
{
    auto future = QtConcurrent::run( [ name, file ] {
        WindowEndpoint sender;
        return sender.claimOrForward( name, { file }, false );
    } );
    while ( !future.isFinished() ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
    }
    return future.result();
}

// A new window queues work that uses it, it must run before the window goes away
void flushQueuedWork()
{
    QCoreApplication::processEvents();
}

struct NamedWindow {
    NamedWindow( size_t index = 1 )
        : appSession{ std::make_shared<Session>() }
        , windowId{ QUuid::createUuid().toString( QUuid::WithoutBraces ) }
        , window{ WindowSession{ appSession, windowId, index } }
    {
    }

    ~NamedWindow()
    {
        flushQueuedWork();
    }

    std::shared_ptr<Session> appSession;
    QString windowId;
    MainWindow window;
};

} // namespace

TEST_CASE( "The window name comes first in the title and replaces the number", "[windowname]" )
{
    NamedWindow fixture;
    const auto unnamedTitle = fixture.window.windowTitle();
    REQUIRE( unnamedTitle.contains( " #2" ) );

    const auto name = uniqueName();
    REQUIRE( fixture.window.setDisplayName( name ) );

    REQUIRE( fixture.window.displayName() == name );
    REQUIRE( fixture.window.windowTitle().startsWith( name + " - " ) );
    REQUIRE_FALSE( fixture.window.windowTitle().contains( " #2" ) );
}

TEST_CASE( "An empty name brings the original title back", "[windowname]" )
{
    NamedWindow fixture;
    const auto unnamedTitle = fixture.window.windowTitle();

    const auto name = uniqueName();
    REQUIRE( fixture.window.setDisplayName( name ) );
    REQUIRE( fixture.window.setDisplayName( "  " ) );

    REQUIRE( fixture.window.displayName().isEmpty() );
    REQUIRE( fixture.window.windowTitle() == unnamedTitle );
    REQUIRE_FALSE( hasListener( name ) );
}

TEST_CASE( "A name in use is refused and the window keeps its name", "[windowname]" )
{
    const auto taken = uniqueName();
    WindowEndpoint holder;
    REQUIRE( holder.claim( taken ) );

    NamedWindow fixture;
    const auto name = uniqueName();
    REQUIRE( fixture.window.setDisplayName( name ) );

    REQUIRE_FALSE( fixture.window.setDisplayName( taken ) );
    REQUIRE( fixture.window.displayName() == name );
    REQUIRE( fixture.window.windowTitle().startsWith( name + " - " ) );
}

TEST_CASE( "A renamed window answers to its new name only", "[windowname]" )
{
    NamedWindow fixture;
    const auto oldName = uniqueName();
    const auto newName = uniqueName();
    REQUIRE( fixture.window.setDisplayName( oldName ) );
    REQUIRE( fixture.window.setDisplayName( newName ) );

    REQUIRE_FALSE( hasListener( oldName ) );
    REQUIRE( hasListener( newName ) );
}

TEST_CASE( "A file sent to the name opens in that window", "[windowname]" )
{
    QTemporaryFile log;
    REQUIRE( log.open() );
    log.write( "line\n" );
    log.flush();

    NamedWindow fixture;
    NamedWindow other;
    const auto name = uniqueName();
    REQUIRE( fixture.window.setDisplayName( name ) );

    auto* tabs = fixture.window.findChild<TabbedCrawlerWidget*>();
    auto* otherTabs = other.window.findChild<TabbedCrawlerWidget*>();
    REQUIRE( tabs != nullptr );
    REQUIRE( otherTabs != nullptr );

    REQUIRE( forwardFile( name, log.fileName() ) == WindowEndpoint::Route::Forwarded );

    REQUIRE( waitUiState( [ & ] { return tabs->count() == 1; } ) );
    REQUIRE( otherTabs->count() == 0 );
}

TEST_CASE( "Closing a window frees its name", "[windowname]" )
{
    NamedWindow fixture;
    const auto name = uniqueName();
    REQUIRE( fixture.window.setDisplayName( name ) );
    fixture.window.show();
    REQUIRE( hasListener( name ) );

    fixture.window.close();

    REQUIRE_FALSE( hasListener( name ) );
}

TEST_CASE( "A window takes over a name claimed before it existed", "[windowname]" )
{
    const auto name = uniqueName();
    auto endpoint = std::make_unique<WindowEndpoint>();
    REQUIRE( endpoint->claim( name ) );

    NamedWindow fixture;
    fixture.window.adoptEndpoint( std::move( endpoint ) );

    REQUIRE( fixture.window.displayName() == name );
    REQUIRE( fixture.window.windowTitle().startsWith( name + " - " ) );
    REQUIRE( hasListener( name ) );
}

TEST_CASE( "A restored window gets its name back", "[windowname]" )
{
    const auto name = uniqueName();
    auto appSession = std::make_shared<Session>();
    const auto windowId = QUuid::createUuid().toString( QUuid::WithoutBraces );

    {
        MainWindow first( WindowSession{ appSession, windowId, 0 } );
        REQUIRE( first.setDisplayName( name ) );
        flushQueuedWork();
    }

    MainWindow restored( WindowSession{ appSession, windowId, 0 } );
    restored.reloadSession();

    REQUIRE( restored.displayName() == name );
    REQUIRE( hasListener( name ) );
    flushQueuedWork();
}

TEST_CASE( "A restored window does not take a name in use", "[windowname]" )
{
    const auto name = uniqueName();
    auto appSession = std::make_shared<Session>();
    const auto windowId = QUuid::createUuid().toString( QUuid::WithoutBraces );

    WindowEndpoint holder;
    {
        MainWindow first( WindowSession{ appSession, windowId, 0 } );
        REQUIRE( first.setDisplayName( name ) );
        REQUIRE( first.setDisplayName( QString{} ) );
        WindowSession( appSession, windowId, 0 ).setDisplayName( name );
        flushQueuedWork();
    }
    REQUIRE( holder.claim( name ) );

    MainWindow restored( WindowSession{ appSession, windowId, 0 } );
    restored.reloadSession();

    REQUIRE( restored.displayName().isEmpty() );
    flushQueuedWork();
}

TEST_CASE( "The file menu offers renaming the window", "[windowname]" )
{
    NamedWindow fixture;

    const auto actions = fixture.window.findChildren<QAction*>();
    const auto hasRename = std::any_of( actions.begin(), actions.end(), []( const QAction* action ) {
        return action->text().remove( '&' ) == "Rename window...";
    } );

    REQUIRE( hasRename );
}
