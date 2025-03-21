/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwintabboxdata.h"

#include "kwinswitcheffectsettings.h"
#include "kwintabboxsettings.h"
#include "shortcutsettings.h"

namespace KWin
{
namespace TabBox
{

KWinTabboxData::KWinTabboxData(QObject *parent)
    : KCModuleData(parent)
    , m_tabBoxConfig(new TabBoxSettings(QStringLiteral("TabBox"), this))
    , m_tabBoxAlternativeConfig(new TabBoxSettings(QStringLiteral("TabBoxAlternative"), this))
    , m_shortcutConfig(new ShortcutSettings(this))
{
    registerSkeleton(m_tabBoxConfig);
    registerSkeleton(m_tabBoxAlternativeConfig);
    registerSkeleton(m_shortcutConfig);
}

TabBoxSettings *KWinTabboxData::tabBoxConfig() const
{
    return m_tabBoxConfig;
}

TabBoxSettings *KWinTabboxData::tabBoxAlternativeConfig() const
{
    return m_tabBoxAlternativeConfig;
}

ShortcutSettings *KWinTabboxData::shortcutConfig() const
{
    return m_shortcutConfig;
}
}
}

#include "moc_kwintabboxdata.cpp"
