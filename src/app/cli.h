/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
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

#ifndef KLOGG_CLI_H
#define KLOGG_CLI_H

#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "klogg_version.h"
#include "log.h"

struct CliParameters {
    bool multi_instance = false;
    bool new_session = false;
    bool load_session = false;
    bool log_to_file = false;
    bool follow_file = false;

    bool enable_logging = false;
    int log_level = 3;

    std::vector<QString> filenames;

    int window_width = 0;
    int window_height = 0;

    QString pattern;

    QString filters_file;

    // Commands for the windows of the running flogg
    bool list_windows = false;
    bool json = false;
    QString window;
    std::optional<QString> window_name;
    QStringList added_filters;
    QStringList added_regexes;
    QString filter_color;
    bool shared_filters = false;
    uint64_t line = 0;

    // Set when the command line is invalid
    QString error;

    static constexpr auto ConfigOptionName = "config";

    // The settings are read before the application object exists, so the configuration
    // file is looked up in the raw arguments (only as --config file or --config=file)
    static QString configFileFromArguments( const QStringList& arguments )
    {
        const auto option = QString( "--" ) + ConfigOptionName;
        for ( auto i = 1; i < arguments.size(); ++i ) {
            const auto& argument = arguments.at( i );
            if ( argument == "--" ) {
                break;
            }
            if ( argument == option && i + 1 < arguments.size() ) {
                return arguments.at( i + 1 );
            }
            if ( argument.startsWith( option + "=" ) ) {
                return argument.mid( option.size() + 1 );
            }
        }
        return {};
    }

    CliParameters( QCoreApplication& app, bool console = false )
    {
        QCommandLineParser parser;
        parser.setApplicationDescription( "flogg log viewer" );
        const auto helpOption = parser.addHelpOption();
        // Shown without the build number, in a message box on Windows
        QCoreApplication::setApplicationVersion( releaseVersion() );
        const auto versionOption = parser.addVersionOption();

        const QCommandLineOption multiInstanceOption(
            QStringList() << "m"
                          << "multi",
            "start a separate flogg, instead of opening a new window in the flogg already "
            "running with the same settings (its windows can't be listed or targeted)" );

        const QCommandLineOption listWindowsOption(
            QStringList() << "L"
                          << "list-windows",
            "list the windows of the running flogg (id, name, tabs) and exit" );

        const QCommandLineOption jsonOption( "json", "print --list-windows as JSON" );

        const QCommandLineOption windowOption(
            QStringList() << "w"
                          << "window",
            "open the files as tabs of this window of the running flogg (its id from "
            "--list-windows, like 2 or #2, or its name) instead of a new window; without "
            "files the options apply to its current tab",
            "id|name" );

        const QCommandLineOption windowNameOption(
            "window-name", "name of the new window (or of the --window one), shown in its title",
            "name" );

        const QCommandLineOption addFilterOption(
            "add-filter", "add a text filter to the opened tabs (can be repeated)", "text" );

        const QCommandLineOption addRegexOption(
            "add-regex", "add a regular expression filter to the opened tabs (can be repeated)",
            "regex" );

        const QCommandLineOption filterColorOption(
            "filter-color", "background colour of the filters added, like orange or #ffa500",
            "color" );

        const QCommandLineOption sharedFiltersOption(
            "shared-filters", "the opened tabs use the filters shared by all windows" );

        const QCommandLineOption lineOption( "line", "select this line (from 1) in the opened tabs",
                                             "number" );

        const QCommandLineOption configOption(
            ConfigOptionName,
            "keep the settings in this file instead of flogg.conf (the session goes to "
            "<name>_session.conf next to it)",
            "file" );

        const QCommandLineOption filtersOption(
            "filters",
            "load the filters of the opened tabs from this file, without making it the default "
            "one",
            "file" );

        const QCommandLineOption loadSessionOption(
            QStringList() << "s"
                          << "load-session",
            "load the previous session" );

        const QCommandLineOption newSessionOption(
            QStringList() << "n"
                          << "new-session",
            "do not load the previous session (default unless enabled in the options for "
            "the first flogg started)" );

        const QCommandLineOption logToFileOption( QStringList() << "l"
                                                                << "log",
                                                  "save the log to a file" );

        const QCommandLineOption followOption( QStringList() << "f"
                                                             << "follow",
                                               "follow initial opened files" );

        const QCommandLineOption patternOption( QStringList() << "e"
                                                              << "pattern",
                                                "pattern to search for", "pattern" );

        const QCommandLineOption debugOption(
            QStringList() << "d"
                          << "debug",
            "output more debug (increase number for more verbosity)", "debug_level", "0" );

        parser.addOption( debugOption );

        const QCommandLineOption windowWidthOption( "window-width", "new window width", "1024" );
        const QCommandLineOption windowHeightOption( "window-height", "new window height",
                                                     "768" );

        if ( !console ) {
            parser.addOption( multiInstanceOption );
            parser.addOption( loadSessionOption );
            parser.addOption( newSessionOption );
            parser.addOption( logToFileOption );
            parser.addOption( followOption );
            parser.addOption( windowWidthOption );
            parser.addOption( windowHeightOption );
            parser.addOption( configOption );
            parser.addOption( filtersOption );
            parser.addOption( listWindowsOption );
            parser.addOption( jsonOption );
            parser.addOption( windowOption );
            parser.addOption( windowNameOption );
            parser.addOption( addFilterOption );
            parser.addOption( addRegexOption );
            parser.addOption( filterColorOption );
            parser.addOption( sharedFiltersOption );
            parser.addOption( lineOption );
        }
        else {
            parser.addOption( patternOption );
        }

        parser.process( app );

        if ( parser.isSet( helpOption ) ) {
            parser.showHelp( EXIT_SUCCESS );
        }

        if ( parser.isSet( versionOption ) ) {
            parser.showVersion();
        }

        if (parser.value( debugOption ).toInt() > 0) {
            enable_logging = true;
        }

        log_level += parser.value( debugOption ).toInt();

        if ( !console ) {
            if ( parser.isSet( loadSessionOption ) ) {
                load_session = true;
            }

            if ( parser.isSet( newSessionOption ) ) {
                new_session = true;
            }

            if ( parser.isSet( logToFileOption ) ) {
                log_to_file = true;
            }

            if ( parser.isSet( followOption ) ) {
                follow_file = true;
            }

            if ( parser.isSet( filtersOption ) ) {
                filters_file = QFileInfo( parser.value( filtersOption ) ).absoluteFilePath();
            }

            multi_instance = parser.isSet( multiInstanceOption );
            list_windows = parser.isSet( listWindowsOption );
            json = parser.isSet( jsonOption );
            window = parser.value( windowOption );
            if ( parser.isSet( windowNameOption ) ) {
                window_name = parser.value( windowNameOption );
            }
            added_filters = parser.values( addFilterOption );
            added_regexes = parser.values( addRegexOption );
            filter_color = parser.value( filterColorOption );
            shared_filters = parser.isSet( sharedFiltersOption );
            if ( parser.isSet( lineOption ) ) {
                bool ok = false;
                line = parser.value( lineOption ).toULongLong( &ok );
                if ( !ok || line == 0 ) {
                    error = "--line takes a line number from 1";
                }
            }
            if ( parser.isSet( windowWidthOption ) ) {
                window_width = parser.value( windowWidthOption ).toInt();
            }
            if ( parser.isSet( windowHeightOption ) ) {
                window_height = parser.value( windowHeightOption ).toInt();
            }
        }
        else {

            if ( parser.isSet( patternOption ) ) {
                pattern = parser.value( patternOption );
            }
        }

        for ( const auto& file : parser.positionalArguments() ) {
            const auto fileInfo = QFileInfo( file );
            filenames.emplace_back( fileInfo.absoluteFilePath() );
        }
    }

    // Whether the command line only makes sense for the windows of a running flogg
    bool targetsRunningInstance() const
    {
        return list_windows || !window.isEmpty();
    }

    // The command line as sent to the running flogg
    QJsonObject toRequest() const
    {
        QJsonObject request;
        request[ "list" ] = list_windows;
        request[ "json" ] = json;
        request[ "window" ] = window;
        if ( window_name ) {
            request[ "windowName" ] = *window_name;
        }
        QJsonArray files;
        for ( const auto& file : filenames ) {
            files.append( file );
        }
        request[ "files" ] = files;
        request[ "follow" ] = follow_file;
        request[ "line" ] = static_cast<qint64>( line );
        request[ "filters" ] = filters_file;
        request[ "addFilters" ] = QJsonArray::fromStringList( added_filters );
        request[ "addRegexes" ] = QJsonArray::fromStringList( added_regexes );
        request[ "filterColor" ] = filter_color;
        request[ "shared" ] = shared_filters;
        request[ "width" ] = window_width;
        request[ "height" ] = window_height;
        return request;
    }

    // Release version without the build number: 1.0.0 for 1.0.0.3
    static QString releaseVersion()
    {
        const auto parts = QString( kloggVersion() ).split( '.' );
        return parts.mid( 0, 3 ).join( '.' );
    }
};

#endif