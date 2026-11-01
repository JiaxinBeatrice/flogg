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

#include <QSettings>
#include <QTemporaryDir>

#include "sessioninfo.h"

TEST_CASE( "The window name survives saving the session", "[sessioninfo]" )
{
    QTemporaryDir folder;
    QSettings settings( folder.filePath( "session.conf" ), QSettings::IniFormat );

    SessionInfo saved;
    saved.add( "named" );
    saved.add( "unnamed" );
    saved.setName( "named", QString::fromUtf8( "\xE5\x95\x8F\xE9\xA1\x8C A" ) );
    saved.saveToStorage( settings );

    SessionInfo loaded;
    loaded.retrieveFromStorage( settings );

    REQUIRE( loaded.name( "named" ) == QString::fromUtf8( "\xE5\x95\x8F\xE9\xA1\x8C A" ) );
    REQUIRE( loaded.name( "unnamed" ).isEmpty() );
}

TEST_CASE( "A session saved before window names still loads", "[sessioninfo]" )
{
    QTemporaryDir folder;
    QSettings settings( folder.filePath( "session.conf" ), QSettings::IniFormat );

    settings.beginGroup( "Window" );
    settings.setValue( "version", 1 );
    settings.beginWriteArray( "windows" );
    settings.setArrayIndex( 0 );
    settings.setValue( "id", "old" );
    settings.endArray();
    settings.endGroup();

    SessionInfo loaded;
    loaded.retrieveFromStorage( settings );

    REQUIRE( loaded.windows() == QStringList{ "old" } );
    REQUIRE( loaded.name( "old" ).isEmpty() );
}
