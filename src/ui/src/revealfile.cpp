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

#include <QDir>
#include <QMessageBox>
#include <QObject>

#include "configuration.h"
#include "openfilehelper.h"
#include "revealfile.h"

void openFolderOfFile( QWidget* parent, const QString& filePath )
{
    if ( !Configuration::get().robustOpenContainingFolder() ) {
        showPathInFileExplorer( filePath );
        return;
    }

    if ( !revealPathInFileExplorer( filePath ) ) {
        QMessageBox::warning( parent, QObject::tr( "flogg" ),
                              QObject::tr( "Can't open the folder of %1" )
                                  .arg( QDir::toNativeSeparators( filePath ) ) );
    }
}
