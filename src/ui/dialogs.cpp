#include "ui/dialogs.h"
#include "ui/theme.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTextEdit>
#include <QVBoxLayout>

namespace echo {

void styleDialog(QDialog* dialog)
{
    dialog->setStyleSheet(QStringLiteral(
        "QDialog { background: transparent; color: #F5F5F7; }"
        "QFrame#dialogCard { background: #28282B; border: 1px solid rgba(255,255,255,0.10); border-radius: 14px; }"
        "QLabel { background: transparent; border: none; color: #B4B4BB; font-size: 13px; font-weight: 400; }"
        "QLabel#dialogTitle { color: #F5F5F7; font-size: 18px; font-weight: 600; }"
        "QPushButton { background: #3D3D41; color: #F5F5F7; border: 1px solid transparent; border-radius: 7px; "
        "min-height: 32px; min-width: 64px; padding: 0 14px; font-size: 13px; font-weight: 500; }"
        "QPushButton:hover { background: #48484D; }"
        "QPushButton:default { border-color: rgba(255,255,255,0.32); }"
        "QPushButton[primary=true] { background: #FA2D48; color: white; border-color: transparent; font-weight: 600; }"
        "QPushButton[primary=true]:hover { background: #FF4560; }"
        "QPushButton:disabled { background: #353538; color: #77777E; border-color: transparent; }"
        "QPushButton#detailsToggle { background: transparent; color: #B4B4BB; border: none; min-width: 0; padding: 0; text-align: left; font-size: 12px; }"
        "QPushButton#detailsToggle:hover { color: #F5F5F7; }"
        "QLineEdit, QSpinBox { background: #1F1F22; color: #F5F5F7; border: 1px solid #49494E; border-radius: 7px; "
        "padding: 6px 10px; min-height: 22px; font-size: 13px; selection-background-color: #FA2D48; }"
        "QLineEdit:focus, QSpinBox:focus { border-color: #FA2D48; }"
        "QTextEdit { background: #202023; color: #B4B4BB; border: 1px solid #414146; border-radius: 7px; padding: 8px; font-size: 12px; }"
        "QCheckBox { color: #F5F5F7; font-size: 13px; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #505056; border-radius: 4px; background: #202023; }"
        "QCheckBox::indicator:checked { background: #FA2D48; border-color: #FA2D48; image: url(:/icons/checkmark.svg); }"));
}

DialogShell::DialogShell(QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(440);
    styleDialog(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("dialogCard"));
    card->setAttribute(Qt::WA_StyledBackground);
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(0, 0, 0, 110));
    card->setGraphicsEffect(shadow);
    outer->addWidget(card);
    m_content = new QVBoxLayout(card);
    m_content->setContentsMargins(24, 24, 24, 24);
    m_content->setSpacing(16);
}

void DialogShell::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < 72) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}
void DialogShell::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}
void DialogShell::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

void DialogShell::fitToContents()
{
    layout()->activate();
    const int height = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width()) : layout()->sizeHint().height();
    resize(width(), height);
}

void DialogShell::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    fitToContents();
    const auto center = parentWidget() ? parentWidget()->window()->frameGeometry().center()
                                       : screen()->availableGeometry().center();
    const auto* target = QGuiApplication::screenAt(center);
    const auto area = (target ? target : screen())->availableGeometry();
    const QPoint position = center - QPoint(width() / 2, height() / 2);
    move(qBound(area.left(), position.x(), qMax(area.left(), area.right() - width() + 1)),
        qBound(area.top(), position.y(), qMax(area.top(), area.bottom() - height() + 1)));
}

void styleMenu(QMenu* menu)
{
    const auto colors = palette();
    menu->setStyleSheet(QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 9px; padding: 6px; font-size: 13px; }"
        "QMenu::item { padding: 7px 20px; border-radius: 5px; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::item:disabled { color: %5; }"
        "QMenu::separator { height: 1px; background: %3; margin: 5px 8px; }")
        .arg(colors.sidebar.name(), colors.text.name(), colors.hairline.name(QColor::HexArgb),
            colors.selected.name(), colors.secondary.name()));
}

MessageDialog::MessageDialog(QMessageBox::Icon, const QString& title, const QString& message,
    QMessageBox::StandardButtons standard, QWidget* parent)
    : DialogShell(parent)
{
    setWindowTitle(title);
    setAccessibleName(title);
    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("dialogTitle"));
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    contentLayout()->addWidget(heading);
    auto* description = new QLabel(message, this);
    description->setObjectName(QStringLiteral("dialogDescription"));
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto descriptionPolicy = description->sizePolicy();
    descriptionPolicy.setHorizontalPolicy(QSizePolicy::Ignored);
    descriptionPolicy.setVerticalPolicy(QSizePolicy::Preferred);
    description->setSizePolicy(descriptionPolicy);
    description->ensurePolished();
    const int textWidth = width() - 74;
    const auto textBounds = description->fontMetrics().boundingRect(QRect(0, 0, textWidth, 10000), Qt::TextWordWrap, message);
    if (textBounds.height() > 160 || textBounds.width() > textWidth) {
        description->hide();
        auto* longDescription = new QTextEdit(this);
        longDescription->setObjectName(QStringLiteral("dialogLongDescription"));
        longDescription->setAccessibleName(QStringLiteral("详细说明"));
        longDescription->setPlainText(message);
        longDescription->setReadOnly(true);
        longDescription->setFixedHeight(160);
        contentLayout()->addWidget(longDescription);
    } else {
        contentLayout()->addWidget(description);
    }
    m_actions = new QHBoxLayout;
    m_actions->setContentsMargins(0, 6, 0, 0);
    m_actions->setSpacing(8);
    m_actions->addStretch();
    contentLayout()->addLayout(m_actions);
    for (const auto& pair : {qMakePair(QMessageBox::Cancel, QStringLiteral("取消")),
             qMakePair(QMessageBox::No, QStringLiteral("取消")), qMakePair(QMessageBox::Yes, QStringLiteral("确认")),
             qMakePair(QMessageBox::Ok, QStringLiteral("好"))}) {
        if (standard.testFlag(pair.first)) {
            auto* action = createButton(pair.second, pair.first);
            m_standard.insert(pair.first, action);
            if (pair.first == QMessageBox::Ok || pair.first == QMessageBox::Yes)
                action->setProperty("primary", true);
        }
    }
    setDefaultButton(standard.testFlag(QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::Ok);
    setEscapeButton(standard.testFlag(QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::Ok);
}

QPushButton* MessageDialog::createButton(const QString& text, StandardButton standard)
{
    auto* action = new QPushButton(text, this);
    action->setAutoDefault(false);
    action->setCursor(Qt::PointingHandCursor);
    m_actions->addWidget(action);
    m_buttons.append(action);
    connect(action, &QPushButton::clicked, this, [this, action, standard] {
        done(standard == QMessageBox::NoButton ? QDialog::Accepted : static_cast<int>(standard));
        emit buttonClicked(action);
    });
    return action;
}
QPushButton* MessageDialog::button(StandardButton standard) const { return m_standard.value(standard); }
QList<QAbstractButton*> MessageDialog::buttons() const { return m_buttons; }
QPushButton* MessageDialog::addButton(const QString& text, QMessageBox::ButtonRole)
{
    return createButton(text, QMessageBox::NoButton);
}
void MessageDialog::setDefaultButton(StandardButton standard)
{
    if (m_default)
        m_default->setDefault(false);
    m_default = button(standard);
    if (m_default) {
        m_default->setDefault(true);
        m_default->setFocus(Qt::OtherFocusReason);
    }
}
void MessageDialog::setEscapeButton(StandardButton standard) { m_escape = button(standard); }
void MessageDialog::setConfirmText(const QString& text, bool destructive)
{
    if (auto* action = button(QMessageBox::Yes)) {
        action->setText(text);
        if (!destructive)
            setDefaultButton(QMessageBox::Yes);
    }
}
void MessageDialog::reject()
{
    if (m_escape)
        m_escape->click();
    else
        QDialog::reject();
}
void MessageDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        event->accept();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_default) {
        auto* focused = qobject_cast<QPushButton*>(focusWidget());
        if (focused && focused->isEnabled())
            focused->click();
        else
            m_default->click();
        event->accept();
    } else {
        QDialog::keyPressEvent(event);
    }
}
void MessageDialog::setDetailedText(const QString& details)
{
    if (!m_details) {
        m_details = new QTextEdit(this);
        m_details->setReadOnly(true);
        m_details->setFixedHeight(144);
        m_details->hide();
        contentLayout()->insertWidget(contentLayout()->count() - 1, m_details);
        m_detailsToggle = new QPushButton(QStringLiteral("查看详情"), this);
        m_detailsToggle->setObjectName(QStringLiteral("detailsToggle"));
        m_detailsToggle->setAutoDefault(false);
        m_detailsToggle->setCursor(Qt::PointingHandCursor);
        m_actions->insertWidget(0, m_detailsToggle);
        m_buttons.prepend(m_detailsToggle);
        connect(m_detailsToggle, &QPushButton::clicked, this, [this] {
            const bool expand = m_details->isHidden();
            m_details->setVisible(expand);
            m_detailsToggle->setText(expand ? QStringLiteral("收起详情") : QStringLiteral("查看详情"));
            fitToContents();
        });
    }
    m_details->setPlainText(details);
    m_detailsToggle->setVisible(!details.isEmpty());
}

MessageDialog::StandardButton MessageDialog::question(QWidget* parent, const QString& title, const QString& message,
    const QString& confirm, bool destructive)
{
    MessageDialog dialog(QMessageBox::Question, title, message, QMessageBox::Yes | QMessageBox::Cancel, parent);
    dialog.setConfirmText(confirm, destructive);
    return static_cast<StandardButton>(dialog.exec());
}
void MessageDialog::warning(QWidget* parent, const QString& title, const QString& message)
{
    MessageDialog dialog(QMessageBox::Warning, title, message, QMessageBox::Ok, parent);
    dialog.exec();
}
void MessageDialog::information(QWidget* parent, const QString& title, const QString& message)
{
    MessageDialog dialog(QMessageBox::Information, title, message, QMessageBox::Ok, parent);
    dialog.exec();
}
void MessageDialog::critical(QWidget* parent, const QString& title, const QString& message)
{
    MessageDialog dialog(QMessageBox::Critical, title, message, QMessageBox::Ok, parent);
    dialog.exec();
}

NameDialog::NameDialog(QWidget* parent, const QString& title, const QString& initial, const QString& confirmLabel)
    : DialogShell(parent)
{
    setFixedWidth(424);
    setWindowTitle(title);
    setAccessibleName(title);
    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("dialogTitle"));
    heading->setTextFormat(Qt::PlainText);
    contentLayout()->addWidget(heading);
    m_field = new QLineEdit(initial, this);
    m_field->setPlaceholderText(QStringLiteral("输入名称"));
    m_field->setAccessibleName(QStringLiteral("名称"));
    m_field->setFixedHeight(36);
    contentLayout()->addWidget(m_field);
    auto* actions = new QDialogButtonBox(this);
    auto* cancel = actions->addButton(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    auto* accept = actions->addButton(QDialogButtonBox::Ok);
    accept->setText(confirmLabel);
    cancel->setAutoDefault(false);
    accept->setDefault(true);
    accept->setProperty("primary", true);
    accept->setEnabled(!initial.trimmed().isEmpty());
    connect(m_field, &QLineEdit::textChanged, this,
        [accept](const QString& text) { accept->setEnabled(!text.trimmed().isEmpty()); });
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    contentLayout()->addWidget(actions);
    m_field->selectAll();
    m_field->setFocus();
}
void NameDialog::setTextValue(const QString& text) { m_field->setText(text); }
QString NameDialog::textValue() const { return m_field->text(); }
QString askName(QWidget* parent, const QString& title, const QString& initial, const QString& confirmLabel)
{
    NameDialog dialog(parent, title, initial, confirmLabel);
    return dialog.exec() == QDialog::Accepted ? dialog.textValue().trimmed() : QString();
}

} // namespace echo
