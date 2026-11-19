#pragma once
#include <QWizard>

namespace bc {

// First-run onboarding: Beginner (auto-configures from hardware) or Advanced
// (every page). Device lists use Qt Multimedia so they work before libobs
// sources exist; choices are written to Settings when the wizard finishes.
class SetupWizard : public QWizard {
    Q_OBJECT
public:
    enum Page { Welcome, Hardware, Camera, Microphone, Output, Resolution, FrameRate, Encoder, Platform, Theme, Finish };
    explicit SetupWizard(QWidget *parent = nullptr);
    int nextId() const override;
    void accept() override;
    bool beginner() const;
};

} // namespace bc
