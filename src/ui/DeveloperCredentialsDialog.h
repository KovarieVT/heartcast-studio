#pragma once
#include <QDialog>

class QLabel;
class QLineEdit;

namespace bc {

// Lets the user paste the OAuth Client IDs they registered with Twitch / Google,
// with the exact console steps beside each field. Values go to SecureStore (DPAPI).
// These identify the user's KovaLive registration to the platform. They are not the
// user's account password, which KovaLive never asks for.
class DeveloperCredentialsDialog : public QDialog {
    Q_OBJECT
public:
    // focusPlatform: "twitch", "youtube" or empty. That section is highlighted and focused.
    explicit DeveloperCredentialsDialog(const QString &focusPlatform = {}, QWidget *parent = nullptr);

private:
    void save();
    QLineEdit *m_twitchId = nullptr, *m_googleId = nullptr, *m_googleSecret = nullptr;
};

// Returns true when everything `platformId` needs to sign in is configured. If something
// is missing, it opens the dialog first and returns whether the user completed it.
bool ensureDevCredentials(const QString &platformId, QWidget *parent);

} // namespace bc
