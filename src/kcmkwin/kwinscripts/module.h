/*
    SPDX-FileCopyrightText: 2011 Tamas Krutki <ktamasw@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MODULE_H
#define MODULE_H

#include <KCModule>
#include <KPluginMetaData>
#include <KPluginModel>
#include <KQuickAddons/ConfigModule>
#include <KSharedConfig>

class KJob;
class KWinScriptsData;

class Module : public KQuickAddons::ConfigModule
{
    Q_OBJECT
        
    Q_PROPERTY(QAbstractItemModel *effectsModel READ effectsModel CONSTANT)
public:
    /**
     * Constructor.
     *
     * @param parent Parent widget of the module
     * @param args Arguments for the module
     */
    explicit Module(QObject *parent, const QVariantList &args = QVariantList());

    /**
     * Destructor.
     */
    ~Module() override;
    void load() override;
    void save() override;
    void defaults() override;
    QAbstractItemModel *effectsModel() const
    {
        return m_model;
    }

Q_SIGNALS:
    void pendingDeletionsChanged();

protected Q_SLOTS:

    /**
     * Called when the import script button is clicked.
     */
    void importScript();

    void importScriptInstallFinished(KJob *job);

    void configure(const KPluginMetaData &data);

private:
    KSharedConfigPtr m_kwinConfig;
    KWinScriptsData *m_kwinScriptsData;
    QList<KPluginMetaData> m_pendingDeletions;
    KPluginModel *m_model;
};

#endif // MODULE_H
