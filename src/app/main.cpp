/*
 * Copyright (C) 2009, 2010, 2011, 2013, 2014 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2016 -- 2021 Anton Filimonov and other contributors
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

#include "log.h"
#include <QtGlobal>
#include <qapplication.h>
#include <qthreadpool.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <shellapi.h>
#endif // _WIN32

#include <cstdio>

#include <mimalloc.h>
#include <QStyleHints>

#include <roaring.hh>

#ifdef KLOGG_HAS_HS
#include <hs.h>
#endif

#include "tbb/global_control.h"

#include "configuration.h"
#include "logger.h"
#include "mainwindow.h"
#include "persistentinfo.h"
#include "styles.h"

#include "cli.h"
#include "kloggapp.h"

// On Windows the settings and the filters are always kept next to flogg.exe
#if defined( KLOGG_PORTABLE ) || defined( Q_OS_WIN )
const bool PersistentInfo::ForcePortable = true;
#else
const bool PersistentInfo::ForcePortable = false;
#endif

void setApplicationAttributes( bool enableQtHdpi, int scaleFactorRounding )
{
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // all network interfaces to see if anything changes and if so, what. This
    // creates a latency spike every 10 seconds on Mac OS 10.12+ and Windows 7 >=
    // when on a wifi connection.
    // So here we disable it for lack of better measure.
    // This will also cause this message: QObject::startTimer: Timers cannot
    // have negative intervals
    // For more info see:
    // - https://bugreports.qt.io/browse/QTBUG-40332
    // - https://bugreports.qt.io/browse/QTBUG-46015
    qputenv( "QT_BEARER_POLL_TIMEOUT", QByteArray::number( std::numeric_limits<int>::max() ) );

#if QT_VERSION < QT_VERSION_CHECK( 6, 0, 0 )
#ifdef Q_OS_WIN
    QCoreApplication::setAttribute( Qt::AA_DisableWindowContextHelpButton );
#endif

    if ( !enableQtHdpi ) {
        QCoreApplication::setAttribute( Qt::AA_DisableHighDpiScaling );
    }
    else {

#if QT_VERSION >= QT_VERSION_CHECK( 5, 14, 0 )
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
            static_cast<Qt::HighDpiScaleFactorRoundingPolicy>( scaleFactorRounding ) );
#else
        Q_UNUSED( scaleFactorRounding );
#endif

        // This attribute must be set before QGuiApplication is constructed:
        QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling );
        // We support high-dpi (aka Retina) displays
        QCoreApplication::setAttribute( Qt::AA_UseHighDpiPixmaps );
    }
#else
    Q_UNUSED( enableQtHdpi );
    Q_UNUSED( scaleFactorRounding );
#endif

    QCoreApplication::setAttribute( Qt::AA_DontShowIconsInMenus );
}

// flogg is a GUI application, on Windows it writes to the console it is started from unless
// its output is redirected
void printToConsole( const QString& text, bool isError )
{
    if ( text.isEmpty() ) {
        return;
    }

#ifdef Q_OS_WIN
    static const auto consoleReady = [] {
        const auto handle = ::GetStdHandle( STD_OUTPUT_HANDLE );
        if ( ( handle == nullptr || handle == INVALID_HANDLE_VALUE )
             && ::AttachConsole( ATTACH_PARENT_PROCESS ) ) {
            FILE* stream = nullptr;
            freopen_s( &stream, "CONOUT$", "w", stdout );
            freopen_s( &stream, "CONOUT$", "w", stderr );
        }
        return true;
    }();
    Q_UNUSED( consoleReady );
#endif

    auto* stream = isError ? stderr : stdout;
    std::fputs( text.toLocal8Bit().constData(), stream );
    if ( !text.endsWith( '\n' ) ) {
        std::fputs( "\n", stream );
    }
    std::fflush( stream );
}

QStringList rawArguments( int argc, char* argv[] )
{
    QStringList arguments;
#ifdef Q_OS_WIN
    // argv is in the ANSI code page, which can't hold every file name
    Q_UNUSED( argc );
    Q_UNUSED( argv );
    int count = 0;
    if ( auto* wideArguments = ::CommandLineToArgvW( ::GetCommandLineW(), &count ) ) {
        for ( int i = 0; i < count; ++i ) {
            arguments.append( QString::fromWCharArray( wideArguments[ i ] ) );
        }
        ::LocalFree( wideArguments );
    }
#else
    for ( int i = 0; i < argc; ++i ) {
        arguments.append( QString::fromLocal8Bit( argv[ i ] ) );
    }
#endif
    return arguments;
}

int main( int argc, char* argv[] )
{
#ifdef KLOGG_USE_MIMALLOC
    mi_process_init();
#endif

    // The settings are read before the command line is parsed
    const auto configFile = CliParameters::configFileFromArguments( rawArguments( argc, argv ) );
    if ( !configFile.isEmpty() ) {
        PersistentInfo::setConfigFile( configFile );
    }

    const auto& config = Configuration::getSynced();
    setApplicationAttributes( config.enableQtHighDpi(), config.scaleFactorRounding() );

    KloggApp app( argc, argv );
    // macOS hides the keys in context menus by default (with both the attribute and the
    // style hint), flogg shows them on every platform
    QCoreApplication::setAttribute( Qt::AA_DontShowShortcutsInContextMenus, false );
    QGuiApplication::styleHints()->setShowShortcutsInContextMenus( true );


    MainWindow::installLanguage( config.language() );
    CliParameters parameters( app );
    if ( !parameters.error.isEmpty() ) {
        printToConsole( "flogg: " + parameters.error, true );
        return EXIT_FAILURE;
    }

    const auto logLevel
        = static_cast<logging::LogLevel>( std::max( parameters.log_level, config.loggingLevel() ) );
    logging::enableLogging( parameters.enable_logging || config.enableLogging(), logLevel );
    logging::enableFileLogging( parameters.log_to_file || config.enableLogging(), logLevel );

    app.initCrashHandler();

    auto maxConcurrency
        = tbb::global_control::active_value( tbb::global_control::max_allowed_parallelism );

    LOG_INFO << "Klogg instance"
             << ", mimalloc v" << mi_version()
             << ", default concurrency " << maxConcurrency;


    roaring_memory_t roaring_memory_allocators;
    roaring_memory_allocators.malloc = mi_malloc;
    roaring_memory_allocators.realloc = mi_realloc;
    roaring_memory_allocators.calloc = mi_calloc;
    roaring_memory_allocators.free = mi_free;
    roaring_memory_allocators.aligned_malloc = mi_aligned_alloc;
    roaring_memory_allocators.aligned_free = mi_free;
    roaring_init_memory_hook(roaring_memory_allocators);

#ifdef KLOGG_HAS_HS
    hs_set_allocator(mi_malloc, mi_free);
#endif

    if ( maxConcurrency < 2 ) {
        maxConcurrency = 2;
        LOG_INFO << "Overriding default concurrency to " << maxConcurrency;
        tbb::global_control concurrencyControl( tbb::global_control::max_allowed_parallelism,
                                                maxConcurrency );
        QThreadPool::globalInstance()->setMaxThreadCount( static_cast<int>( maxConcurrency ) );
    }

    // A flogg started later with the same settings opens its window in the first one
    // (unless -m), which also lists and changes its windows for the command line
    const auto request = parameters.toRequest();
    if ( !parameters.multi_instance ) {
        app.startSingleInstance(
            remote::instanceName( PersistentInfo::getSettings( app_settings{} ).fileName() ) );

        if ( app.isSecondary() ) {
            LOG_INFO << "Found another flogg, pid " << app.primaryPid();
            if ( const auto reply = app.sendToPrimary( request ) ) {
                if ( !( *reply )[ "ok" ].toBool() ) {
                    printToConsole( "flogg: " + ( *reply )[ "error" ].toString(), true );
                    return EXIT_FAILURE;
                }
                printToConsole( ( *reply )[ "output" ].toString(), false );
                return EXIT_SUCCESS;
            }

            if ( parameters.targetsRunningInstance() ) {
                printToConsole( "flogg: the running flogg does not answer", true );
                return EXIT_FAILURE;
            }
            LOG_WARNING << "The running flogg does not answer, starting a separate one";
        }
    }

    if ( parameters.targetsRunningInstance() ) {
        if ( parameters.list_windows ) {
            printToConsole( parameters.json ? "[]" : "", false );
            return EXIT_SUCCESS;
        }
        printToConsole( "flogg: flogg is not running, there is no window " + parameters.window,
                        true );
        return EXIT_FAILURE;
    }

    QString commandError;
    const auto command = KloggApp::readCommand( request, &commandError );
    if ( !command ) {
        printToConsole( "flogg: " + commandError, true );
        return EXIT_FAILURE;
    }

    {
        StyleManager::applyStyle( config.style() );

        // The previous session is only restored by the primary instance
        MainWindow* mw = nullptr;
        if ( parameters.load_session
             || ( parameters.filenames.empty() && !parameters.new_session
                  && config.loadLastSession() && app.isPrimary() ) ) {
            mw = app.reloadSession();
        }
        else {
            mw = app.newWindow();
            mw->reloadGeometry();
            mw->show();
        }

        if ( parameters.window_width > 0 && parameters.window_height > 0 ) {
            mw->resize( parameters.window_width, parameters.window_height );
        }

        KloggApp::runCommand( mw, request, *command );
    }

    if ( app.isPrimary() ) {
        app.startControlServer();
    }

    return app.exec();
}
