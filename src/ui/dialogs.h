#pragma once

#include <QDialog>
#include <QHash>
#include <QMessageBox>

class QMenu;
class QKeyEvent;
class QShowEvent;
class QMouseEvent;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QLineEdit;
class QTextEdit;
class QAbstractButton;

namespace echo {

void styleDialog(QDialog* dialog);
void styleMenu(QMenu* menu);
QString askName(QWidget* parent, const QString& title, const QString& initial, const QString& confirmLabel);

// Application dialogs own their layout instead of inheriting platform message-box spacers and chrome.
class DialogShell : public QDialog {
    Q_OBJECT
public:
    explicit DialogShell(QWidget* parent = nullptr);
    QVBoxLayout* contentLayout() const { return m_content; }
protected:
    void showEvent(QShowEvent* event) override;
    void fitToContents();
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    QVBoxLayout* m_content;
    QPoint m_dragOffset;
    bool m_dragging = false;
};

class NameDialog : public DialogShell {
    Q_OBJECT
public:
    NameDialog(QWidget* parent, const QString& title, const QString& initial, const QString& confirmLabel);
    void setTextValue(const QString& text);
    QString textValue() const;
private:
    QLineEdit* m_field;
};

class MessageDialog : public DialogShell {
    Q_OBJECT
public:
    using StandardButton = QMessageBox::StandardButton;
    MessageDialog(QMessageBox::Icon icon, const QString& title, const QString& message,
        QMessageBox::StandardButtons buttons = QMessageBox::Ok, QWidget* parent = nullptr);
    void setDetailedText(const QString& details);
    void setConfirmText(const QString& text, bool destructive = false);
    QPushButton* button(StandardButton button) const;
    QList<QAbstractButton*> buttons() const;
    QPushButton* addButton(const QString& text, QMessageBox::ButtonRole role);
    void setDefaultButton(StandardButton button);
    void setEscapeButton(StandardButton button);
    QPushButton* defaultButton() const { return m_default; }
    static StandardButton question(QWidget* parent, const QString& title, const QString& message,
        const QString& confirm = QStringLiteral("确认"), bool destructive = false);
    static void warning(QWidget* parent, const QString& title, const QString& message);
    static void information(QWidget* parent, const QString& title, const QString& message);
    static void critical(QWidget* parent, const QString& title, const QString& message);
signals:
    void buttonClicked(QAbstractButton* button);
public slots:
    void reject() override;
protected:
    void keyPressEvent(QKeyEvent* event) override;
private:
    QPushButton* createButton(const QString& text, StandardButton standard);
    QHBoxLayout* m_actions;
    QHash<StandardButton, QPushButton*> m_standard;
    QList<QAbstractButton*> m_buttons;
    QPushButton* m_default = nullptr;
    QPushButton* m_escape = nullptr;
    QPushButton* m_detailsToggle = nullptr;
    QTextEdit* m_details = nullptr;
};

} // namespace echo
