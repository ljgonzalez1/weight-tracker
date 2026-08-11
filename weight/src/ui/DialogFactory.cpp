#include "ui/DialogFactory.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QWidget>

#include "settings/Strings.hpp"

namespace weight::ui {

using settings::Strings;

DialogFactory::DialogFactory(const settings::Settings& configuration, QWidget* parent)
    : settings_(&configuration), parent_(parent) {}

void DialogFactory::centreOnPrimaryScreen(QWidget* widget) {
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr || widget == nullptr) {
        return;
    }
    QRect frame = widget->frameGeometry();
    frame.moveCenter(screen->availableGeometry().center());
    widget->move(frame.topLeft());
}

void DialogFactory::centreOnParent(QWidget* widget, const QWidget* parent) {
    if (widget == nullptr || parent == nullptr) {
        return;
    }
    QRect frame = widget->frameGeometry();
    frame.moveCenter(parent->frameGeometry().center());
    widget->move(frame.topLeft());
}

bool DialogFactory::confirmDiscard() const {

    QMessageBox box(parent_);
    box.setWindowTitle(Strings::get(QStringLiteral("dialog.discard.title")));
    box.setText(Strings::get(QStringLiteral("dialog.discard.text")));
    box.setInformativeText(Strings::get(QStringLiteral("dialog.discard.question")));
    box.setIcon(QMessageBox::Warning);

    QPushButton* confirm = box.addButton(Strings::get(QStringLiteral("dialog.discard.confirm")), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(Strings::get(QStringLiteral("dialog.discard.cancel")), QMessageBox::RejectRole);

    // Going back is both the default and the escape action: a stray return or
    // escape must never be the thing that throws away a generated chart.
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.setWindowModality(Qt::WindowModal);

    QTimer::singleShot(0, &box, [&box, this] { centreOnParent(&box, parent_); });
    box.exec();
    return box.clickedButton() == confirm;
}

QString DialogFactory::askImageDestination(const QString& directory,
                                           const QString& suggestedName) const {

    QFileDialog dialog(parent_);
    dialog.setWindowTitle(Strings::get(QStringLiteral("dialog.save.title")));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(Strings::get(QStringLiteral("dialog.save.filter")));
    dialog.setDirectory(directory);
    dialog.selectFile(suggestedName);
    dialog.setDefaultSuffix(QStringLiteral("png"));

    QTimer::singleShot(0, &dialog, [&dialog] { centreOnPrimaryScreen(&dialog); });

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList selected = dialog.selectedFiles();
    if (selected.isEmpty()) {
        return {};
    }

    QString path = selected.first();
    if (QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
        path += QStringLiteral(".png");
    }
    return path;
}

void DialogFactory::reportAlreadyRunning(qint64 processId) {
    QMessageBox box;
    box.setWindowTitle(Strings::get(QStringLiteral("instance.title")));
    box.setText(Strings::get(QStringLiteral("instance.text")));
    box.setIcon(QMessageBox::Information);

    // The process id goes in the informative text, which Qt renders a step
    // smaller than the main message, so it reads as a detail rather than as
    // part of the sentence.
    box.setInformativeText(
        processId > 0
            ? Strings::get(QStringLiteral("instance.pid"), QString::number(processId))
            : Strings::get(QStringLiteral("instance.pid.unknown")));
    box.setStandardButtons(QMessageBox::Ok);
    QTimer::singleShot(0, &box, [&box] { centreOnPrimaryScreen(&box); });
    box.exec();
}

void DialogFactory::reportError(const QString& title, const QString& message) const {
    QMessageBox box(parent_);
    box.setWindowTitle(title);
    box.setText(message);
    box.setIcon(QMessageBox::Critical);
    box.setWindowModality(Qt::WindowModal);
    QTimer::singleShot(0, &box, [&box, this] { centreOnParent(&box, parent_); });
    box.exec();
}

}  // namespace weight::ui
