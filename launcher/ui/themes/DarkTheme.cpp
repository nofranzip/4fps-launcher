// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2024 Tayou <git@tayou.org>
 *  Copyright (C) 2024 TheKodeToad <TheKodeToad@proton.me>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */
#include "DarkTheme.h"

#include <QObject>

QString DarkTheme::id()
{
    return "dark";
}

QString DarkTheme::name()
{
    return QStringLiteral("4fps");
}

QPalette DarkTheme::colorScheme()
{
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#141416"));
    darkPalette.setColor(QPalette::WindowText, QColor("#f4f4f5"));
    darkPalette.setColor(QPalette::Base, QColor("#0d0d0e"));
    darkPalette.setColor(QPalette::AlternateBase, QColor("#1b1b1e"));
    darkPalette.setColor(QPalette::ToolTipBase, QColor("#232327"));
    darkPalette.setColor(QPalette::ToolTipText, QColor("#f4f4f5"));
    darkPalette.setColor(QPalette::Text, QColor("#f4f4f5"));
    darkPalette.setColor(QPalette::Button, QColor("#1b1b1e"));
    darkPalette.setColor(QPalette::ButtonText, QColor("#f4f4f5"));
    darkPalette.setColor(QPalette::BrightText, QColor("#ffffff"));
    darkPalette.setColor(QPalette::Link, QColor("#e4e4e7"));
    darkPalette.setColor(QPalette::Highlight, QColor("#ffffff"));
    darkPalette.setColor(QPalette::HighlightedText, QColor("#0d0d0e"));
    darkPalette.setColor(QPalette::PlaceholderText, QColor("#71717a"));
    return fadeInactive(darkPalette, fadeAmount(), fadeColor());
}

double DarkTheme::fadeAmount()
{
    return 0.5;
}

QColor DarkTheme::fadeColor()
{
    return QColor("#0d0d0e");
}

bool DarkTheme::hasStyleSheet()
{
    return true;
}

QString DarkTheme::appStyleSheet()
{
    return QString::fromUtf8(
#include "FourFpsStyle.inc"
    );
}

QString DarkTheme::tooltip()
{
    return "";
}
