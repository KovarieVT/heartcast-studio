#include "ui/DeveloperCredentialsDialog.h"
#include "auth/OAuth.h"
#include "auth/SecureStore.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace bc {

using Key = DevCredentials::Key;

static QLabel *wrapLabel(const QString &html, const char *objectName = nullptr) {
    auto *l = new QLabel(html);
    l->setWordWrap(true);
    l->setTextFormat(Qt::RichText);
    l->setOpenExternalLinks(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    if (objectName) l->setObjectName(objectName);
    return l;
}

// Fills the field from the current value. If an environment variable supplies it,
// the field is read-only because the environment always wins.
static QLineEdit *credField(Key k, const QString &placeholder, QLabel **noteOut) {
    auto *e = new QLineEdit;
    e->setPlaceholderText(placeholder);
    e->setClearButtonEnabled(true);
    QString note;
    switch (DevCredentials::source(k)) {
    case DevCredentials::Source::Environment:
        e->setText(DevCredentials::get(k));
        e->setReadOnly(true);
        note = QObject::tr("Set by the environment variable %1, which takes priority.").arg(DevCredentials::envName(k));
        break;
    case DevCredentials::Source::App:
        e->setText(DevCredentials::get(k));
        note = QObject::tr("Saved ✓");
        break;
    case DevCredentials::Source::File:
        e->setText(DevCredentials::get(k));
        note = QObject::tr("Read from developer.json. Saving here stores it securely instead.");
        break;
    case DevCredentials::Source::None: break;
    }
    auto *n = new QLabel(note);
    n->setObjectName("Muted");
    n->setVisible(!note.isEmpty());
    *noteOut = n;
    return e;
}

DeveloperCredentialsDialog::DeveloperCredentialsDialog(const QString &focusPlatform, QWidget *parent) : QDialog(parent) {
    DevCredentials::reload();
    setWindowTitle(tr("Developer credentials"));
    setMinimumWidth(620);
    resize(680, 760);

    auto *outer = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget;
    auto *lay = new QVBoxLayout(body);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    QString lead;
    if (focusPlatform == "twitch") lead = "<b>" + tr("To connect Twitch, KovaLive needs a Twitch Client ID first.") + "</b><br>";
    if (focusPlatform == "youtube") lead = "<b>" + tr("To connect YouTube, KovaLive needs a Google Client ID and secret first.") + "</b><br>";
    lay->addWidget(wrapLabel(lead + tr(
        "Twitch and Google only let an app sign you in if the app has its own registration. KovaLive "
        "doesn't ship with one built in, so you make your own. It's free, takes about 5 minutes, and you only do it once.<br><br>"
        "You can stream without this: paste your stream key into the destination. Connecting an account adds "
        "chat, alerts, title/category editing and automatic stream keys.")));

    // ---------------------------------------------------------------- Twitch
    auto *tw = new QGroupBox(tr("Twitch"));
    auto *twl = new QVBoxLayout(tw);
    twl->addWidget(wrapLabel(tr(
        "<ol style='margin-left:-20px'>"
        "<li>Turn on <b>two-factor authentication</b> for your Twitch account (Settings → Security and Privacy). Twitch requires it before you can register an app.</li>"
        "<li>Open the Twitch Developer Console → <b>Applications</b> → <b>Register Your Application</b>.</li>"
        "<li><b>Name:</b> anything unique, e.g. <i>KovaLive - yourname</i><br>"
        "<b>OAuth Redirect URLs:</b> type <code>https://localhost</code> into the box. Don't press <b>Add</b>, because that only makes another empty box, which Twitch rejects.<br>"
        "<b>Category:</b> Broadcaster Suite<br>"
        "<b>Client Type:</b> <b>Public</b> (important: Confidential won't work)</li>"
        "<li>Click <b>Create</b>, then <b>Manage</b>, and copy the <b>Client ID</b> into the box below. No secret is needed.</li>"
        "</ol>")));
    auto *twOpen = new QPushButton(tr("Open Twitch Developer Console"));
    twOpen->setProperty("accent", "secondary");
    connect(twOpen, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://dev.twitch.tv/console/apps")); });
    QLabel *twNote = nullptr;
    m_twitchId = credField(Key::TwitchClientId, tr("Paste your Twitch Client ID"), &twNote);
    auto *twForm = new QFormLayout;
    twForm->addRow(twOpen);
    twForm->addRow(tr("Client ID"), m_twitchId);
    twForm->addRow(QString(), twNote);
    twl->addLayout(twForm);
    lay->addWidget(tw);

    // ---------------------------------------------------------------- YouTube
    auto *yt = new QGroupBox(tr("YouTube (Google)"));
    auto *ytl = new QVBoxLayout(yt);
    ytl->addWidget(wrapLabel(tr(
        "<ol style='margin-left:-20px'>"
        "<li>Open the Google Cloud Console and create a project (any name).</li>"
        "<li><b>APIs &amp; Services → Library</b>: search for <b>YouTube Data API v3</b> and click <b>Enable</b>.</li>"
        "<li><b>Google Auth Platform</b> (OAuth consent screen): click <b>Get started</b>, choose <b>External</b>, "
        "then under <b>Audience → Test users</b> add your own Google account. While the app is in Testing, only test users can sign in.</li>"
        "<li><b>Clients → Create client</b>: Application type <b>Desktop app</b>. Copy the <b>Client ID</b> and <b>Client secret</b> into the boxes below.</li>"
        "</ol>"
        "Google treats a Desktop-app secret as non-confidential, but KovaLive still stores it encrypted.")));
    auto *ytOpen = new QPushButton(tr("Open Google Cloud Console"));
    ytOpen->setProperty("accent", "secondary");
    connect(ytOpen, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl("https://console.cloud.google.com/apis/credentials")); });
    QLabel *gidNote = nullptr, *gsecNote = nullptr;
    m_googleId = credField(Key::GoogleClientId, tr("…apps.googleusercontent.com"), &gidNote);
    m_googleSecret = credField(Key::GoogleClientSecret, tr("Paste your Client secret"), &gsecNote);
    m_googleSecret->setEchoMode(QLineEdit::Password);
    auto *show = new QCheckBox(tr("Show secret"));
    connect(show, &QCheckBox::toggled, m_googleSecret, [this](bool on) { m_googleSecret->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
    auto *ytForm = new QFormLayout;
    ytForm->addRow(ytOpen);
    ytForm->addRow(tr("Client ID"), m_googleId);
    ytForm->addRow(QString(), gidNote);
    ytForm->addRow(tr("Client secret"), m_googleSecret);
    ytForm->addRow(QString(), gsecNote);
    ytForm->addRow(QString(), show);
    ytl->addLayout(ytForm);
    lay->addWidget(yt);

    lay->addWidget(wrapLabel(tr(
        "These IDs identify <i>your KovaLive registration</i> to Twitch and Google. They are not your account "
        "password, and KovaLive will never ask for that. You sign in on the platform's own website.%1")
        .arg(SecureStore::isOsProtected() ? tr(" Stored on this PC with Windows data protection.") : QString()), "Muted"));
    lay->addStretch();

    if (focusPlatform == "youtube") { yt->setProperty("highlight", true); m_googleId->setFocus(); }
    else { if (focusPlatform == "twitch") tw->setProperty("highlight", true); m_twitchId->setFocus(); }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));
    buttons->button(QDialogButtonBox::Save)->setProperty("accent", "primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &DeveloperCredentialsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

void DeveloperCredentialsDialog::save() {
    // Pasting from a web page often picks up spaces or line breaks.
    auto clean = [](QLineEdit *e) { QString s = e->text(); s.remove(QRegularExpression("\\s")); return s; };
    const QString tid = clean(m_twitchId), gid = clean(m_googleId), gsec = clean(m_googleSecret);

    QStringList doubts;
    if (!tid.isEmpty() && !QRegularExpression("^[a-z0-9]{20,40}$", QRegularExpression::CaseInsensitiveOption).match(tid).hasMatch())
        doubts << tr("The Twitch Client ID doesn't look right. It's usually 30 letters and numbers. Make sure you copied the Client ID, not the app name.");
    if (!gid.isEmpty() && !gid.endsWith(".apps.googleusercontent.com"))
        doubts << tr("The Google Client ID usually ends with .apps.googleusercontent.com.");
    if (!gid.isEmpty() && gsec.isEmpty())
        doubts << tr("YouTube also needs the Client secret from the same Desktop-app client.");
    if (!doubts.isEmpty() &&
        QMessageBox::question(this, tr("Double-check"), doubts.join("\n\n") + "\n\n" + tr("Save anyway?")) != QMessageBox::Yes)
        return;

    bool ok = true;
    if (!m_twitchId->isReadOnly()) ok &= DevCredentials::set(Key::TwitchClientId, tid);
    if (!m_googleId->isReadOnly()) ok &= DevCredentials::set(Key::GoogleClientId, gid);
    if (!m_googleSecret->isReadOnly()) ok &= DevCredentials::set(Key::GoogleClientSecret, gsec);
    if (!ok) {
        QMessageBox::warning(this, tr("Developer credentials"), tr("Couldn't save to secure storage. Check that your %1 folder is writable.").arg("KovaLive"));
        return;
    }
    accept();
}

bool ensureDevCredentials(const QString &platformId, QWidget *parent) {
    if (DevCredentials::ready(platformId)) return true;
    DeveloperCredentialsDialog dlg(platformId, parent);
    return dlg.exec() == QDialog::Accepted && DevCredentials::ready(platformId);
}

} // namespace bc
