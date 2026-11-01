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

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QPushButton>
#include <QTemporaryDir>

#include "configuration.h"
#include "openfilehelper.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"

namespace {

// Restores the setting, the tests share the configuration of the process
struct SettingGuard {
    SettingGuard()
        : saved{ Configuration::get().robustOpenContainingFolder() }
    {
    }

    ~SettingGuard()
    {
        auto& config = Configuration::getSynced();
        config.setRobustOpenContainingFolder( saved );
        config.save();
    }

    bool saved;
};

} // namespace

TEST_CASE( "An existing file is selected in its folder", "[revealfile]" )
{
    QTemporaryDir folder;
    const auto path = folder.filePath( "a.log" );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.close();

    REQUIRE( revealActionFor( path ) == RevealAction::SelectFile );
}

TEST_CASE( "A deleted file falls back to its folder", "[revealfile]" )
{
    QTemporaryDir folder;

    REQUIRE( revealActionFor( folder.filePath( "gone.log" ) ) == RevealAction::OpenFolder );
}

TEST_CASE( "A file in a missing folder is reported", "[revealfile]" )
{
    QTemporaryDir folder;

    REQUIRE( revealActionFor( folder.filePath( "gone/gone.log" ) ) == RevealAction::NotFound );
}

TEST_CASE( "The path is quoted after the select switch", "[revealfile]" )
{
    const auto path = QString::fromUtf8( "C:\\my logs\\a,b \xE5\x95\x8F\xE9\xA1\x8C&%.log" );

    REQUIRE( explorerSelectArguments( path ) == "/select,\"" + path + "\"" );
}

TEST_CASE( "A path without spaces is quoted as well", "[revealfile]" )
{
    REQUIRE( explorerSelectArguments( "C:\\logs\\a,b.log" ) == "/select,\"C:\\logs\\a,b.log\"" );
}

TEST_CASE( "The robust folder opening is off by default", "[revealfile]" )
{
    REQUIRE_FALSE( Configuration{}.robustOpenContainingFolder() );
}

TEST_CASE( "The robust folder opening setting is saved", "[revealfile]" )
{
    SettingGuard guard;
    auto& config = Configuration::getSynced();
    config.setRobustOpenContainingFolder( !guard.saved );
    config.save();
    config.setRobustOpenContainingFolder( guard.saved );

    REQUIRE( Configuration::getSynced().robustOpenContainingFolder() == !guard.saved );
}

TEST_CASE( "The options dialog changes the robust folder opening setting", "[revealfile]" )
{
    SettingGuard guard;
    auto& config = Configuration::getSynced();
    config.setRobustOpenContainingFolder( false );
    config.save();

    SavedSearches::getSynced();
    RecentFiles::getSynced();
    OptionsDialog dialog;
    auto* checkBox = dialog.findChild<QCheckBox*>( "robustOpenFolderCheckBox" );
    REQUIRE( checkBox != nullptr );
    REQUIRE_FALSE( checkBox->isChecked() );

    checkBox->setChecked( true );
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply )->click();

    REQUIRE( Configuration::getSynced().robustOpenContainingFolder() );
}
