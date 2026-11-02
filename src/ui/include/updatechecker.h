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

#ifndef KLOGG_UPDATECHECKER_H
#define KLOGG_UPDATECHECKER_H

#include <optional>

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVersionNumber>

class QWidget;

namespace updates {

// The latest release published on GitHub
struct Release {
    QVersionNumber version;
    QString tag;
    QString notes;
    QUrl page;
    // The package of this platform, empty when there is none
    QString assetName;
    QUrl assetUrl;
};

// Version of a release build (1.2.0 from 1.2.0.<build>), nothing for a development build
// (numbered after klogg, 24.11.0.<build>)
std::optional<QVersionNumber> releaseVersion( const QString& buildVersion );

// The release in the reply of the GitHub releases API, the package matching the suffix
std::optional<Release> parseRelease( const QJsonObject& json, const QString& assetSuffix );

// Package suffix of this platform, empty when releases have none
QString platformAssetSuffix();

} // namespace updates

// Looks for a newer release on GitHub, once a day at start or from the Help menu, and lets the
// user install it: the package is downloaded and replaces flogg once it is closed
class UpdateChecker : public QObject {
    Q_OBJECT
  public:
    static UpdateChecker& instance();

    // A few seconds after start, when enabled and not done in the last day
    void scheduleStartupCheck();

    // From the Help menu: also tells when there is nothing new
    void checkNow( QWidget* parent );

  private:
    UpdateChecker();

    void check( bool manual );
    void onReply( bool manual );
    void offer( const updates::Release& release, bool manual );
    void download( const updates::Release& release );
    // Prepared to run when flogg quits, then asks every window to close
    bool prepareInstall( const QString& package );
    QWidget* dialogParent() const;

  private:
    QNetworkAccessManager network_;
    QPointer<QWidget> parent_;
    bool checking_ = false;
    bool installPending_ = false;
};

#endif // KLOGG_UPDATECHECKER_H
