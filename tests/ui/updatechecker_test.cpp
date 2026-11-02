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

#include <QFile>
#include <QJsonDocument>
#include <QTextBrowser>

#include "updatechecker.h"

SCENARIO( "Update checker reads the build version", "[update]" )
{
    // Release builds: <tag>.<build number>
    CHECK( updates::releaseVersion( "1.1.0.57" ) == QVersionNumber( 1, 1, 0 ) );
    CHECK( updates::releaseVersion( "1.2.0" ) == QVersionNumber( 1, 2, 0 ) );
    // Development builds keep the klogg version
    CHECK_FALSE( updates::releaseVersion( "24.11.0.57" ) );
    CHECK_FALSE( updates::releaseVersion( "" ) );
}

SCENARIO( "Update checker reads the GitHub release", "[update]" )
{
    // Shape of https://api.github.com/repos/howardgood88/flogg/releases/latest
    const auto json = QJsonDocument::fromJson( R"({
        "tag_name": "v1.2.0",
        "html_url": "https://github.com/howardgood88/flogg/releases/tag/v1.2.0",
        "body": "## Changes\n- Faster",
        "assets": [
            { "name": "flogg-1.2.0-macos-arm64.dmg",
              "browser_download_url": "https://example.com/flogg-1.2.0-macos-arm64.dmg" },
            { "name": "flogg-1.2.0-windows-x64.zip",
              "browser_download_url": "https://example.com/flogg-1.2.0-windows-x64.zip" }
        ]
    })" ).object();

    const auto release = updates::parseRelease( json, "-windows-x64.zip" );
    REQUIRE( release );
    CHECK( release->version == QVersionNumber( 1, 2, 0 ) );
    CHECK( release->tag == "v1.2.0" );
    CHECK( release->notes == "## Changes\n- Faster" );
    CHECK( release->assetName == "flogg-1.2.0-windows-x64.zip" );
    CHECK( release->assetUrl == QUrl( "https://example.com/flogg-1.2.0-windows-x64.zip" ) );
    CHECK( release->version > *updates::releaseVersion( "1.1.0.57" ) );

    // No package for this platform: only the page
    const auto other = updates::parseRelease( json, "-linux-x64.tar.gz" );
    REQUIRE( other );
    CHECK( other->assetUrl.isEmpty() );

    // A release without notes (body is null)
    auto noNotes = json;
    noNotes.remove( "body" );
    REQUIRE( updates::parseRelease( noNotes, {} ) );
    CHECK( updates::parseRelease( noNotes, {} )->notes.isEmpty() );

    CHECK_FALSE( updates::parseRelease( QJsonObject{ { "tag_name", "latest" } }, {} ) );
}

// Hidden, saves how the changelog of Help > What's new looks:
// FLOGG_RENDER_DIR=<dir> FLOGG_CHANGELOG=<CHANGELOG.md> klogg_itests "[.render]"
SCENARIO( "Changelog rendering", "[.render]" )
{
    const auto renderDir = qEnvironmentVariable( "FLOGG_RENDER_DIR" );
    QFile changes( qEnvironmentVariable( "FLOGG_CHANGELOG" ) );
    if ( renderDir.isEmpty() || !changes.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        WARN( "FLOGG_RENDER_DIR or FLOGG_CHANGELOG is not set" );
        return;
    }

    QTextBrowser text;
    text.resize( 640, 560 );
    text.setMarkdown( QString::fromUtf8( changes.readAll() ) );
    REQUIRE( text.grab().save( renderDir + "/changelog.png" ) );
}
