#include "chatwindow.h"

#include <QAbstractSocket>
#include <QFont>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QtEndian>

#include <cstring>

#include "ui_chatwindow.h"

namespace
{
constexpr int kHeaderSize = static_cast<int>(sizeof(PacketHeader));

QString trimTimestampPrefix(const QString& payload)
{
    if (payload.startsWith('['))
    {
        const int closingIndex = payload.indexOf("] ");
        if (closingIndex > 0)
        {
            return payload.mid(closingIndex + 2);
        }
    }

    return payload;
}

QString extractRoomName(const QString& roomText)
{
    const int countIndex = roomText.indexOf(" (");
    if (countIndex > 0)
    {
        return roomText.left(countIndex).trimmed();
    }

    return roomText.trimmed();
}

QString extractOwnerName(const QString& roomText)
{
    const QString ownerPrefix = "(owner: ";
    const int ownerIndex = roomText.indexOf(ownerPrefix, 0, Qt::CaseInsensitive);
    if (ownerIndex < 0)
    {
        return QString();
    }

    const int endIndex = roomText.indexOf(')', ownerIndex);
    if (endIndex < 0)
    {
        return QString();
    }

    return roomText.mid(ownerIndex + ownerPrefix.size(), endIndex - ownerIndex - ownerPrefix.size()).trimmed();
}

QString htmlize(const QString& text)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace('\n', "<br/>");
    return escaped;
}

QString extractLogTag(QString& body)
{
    if (!body.startsWith('['))
    {
        return "CHAT";
    }

    const int end = body.indexOf(']');
    if (end <= 1)
    {
        return "CHAT";
    }

    const QString tag = body.mid(1, end - 1).trimmed().toUpper();
    body = body.mid(end + 1).trimmed();
    return tag.isEmpty() ? "CHAT" : tag;
}

}

ChatWindow::ChatWindow(QWidget* parent)
    : QMainWindow(parent),
      m_ui(new Ui::ChatWindow),
      m_socket(new QTcpSocket(this)),
      m_handshakeComplete(false)
{
    setupUi();

    connect(m_ui->connectButton, &QPushButton::clicked, this, &ChatWindow::connectToServer);
    connect(m_ui->sendButton, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_ui->messageEdit, &QLineEdit::returnPressed, this, &ChatWindow::sendMessage);

    connect(m_socket, &QTcpSocket::connected, this, &ChatWindow::socketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ChatWindow::socketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ChatWindow::readFromSocket);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ChatWindow::socketErrorOccurred);

    connect(m_ui->roomList, &QListWidget::itemDoubleClicked, this, &ChatWindow::joinSelectedRoom);
    connect(m_ui->roomList, &QListWidget::itemClicked, this, &ChatWindow::syncRoomInputFromSelection);
    connect(m_ui->userList, &QListWidget::itemDoubleClicked, this, &ChatWindow::prepareWhisper);
    connect(m_ui->userList, &QListWidget::itemClicked, this, &ChatWindow::selectUserForAdmin);
    connect(m_ui->roomActionButton, &QPushButton::clicked, this, &ChatWindow::createOrJoinRoom);
    connect(m_ui->refreshRoomsButton, &QPushButton::clicked, this, &ChatWindow::refreshRoomData);
    connect(m_ui->leaveRoomButton, &QPushButton::clicked, this, &ChatWindow::leaveCurrentRoom);
    connect(m_ui->roomEdit, &QLineEdit::returnPressed, this, &ChatWindow::createOrJoinRoom);
    connect(m_ui->clearLogButton, &QPushButton::clicked, this, &ChatWindow::clearChatLog);
    connect(m_ui->guideToggleButton, &QPushButton::clicked, this, &ChatWindow::toggleGuidePanel);
    connect(m_ui->helpButton, &QPushButton::clicked, this, &ChatWindow::requestHelp);
    connect(m_ui->usersButton, &QPushButton::clicked, this, &ChatWindow::requestUserList);
    connect(m_ui->roomsButton, &QPushButton::clicked, this, &ChatWindow::refreshRoomData);
    connect(m_ui->announceButton, &QPushButton::clicked, this, &ChatWindow::announceFromInput);
    connect(m_ui->botStatusButton, &QPushButton::clicked, this, &ChatWindow::requestBotStatus);
    connect(m_ui->closeRoomButton, &QPushButton::clicked, this, &ChatWindow::closeCurrentRoom);
    connect(m_ui->kickUserButton, &QPushButton::clicked, this, &ChatWindow::kickSelectedUser);
    connect(m_ui->authModeCombo, &QComboBox::currentTextChanged, this, [this](const QString&) { updateAuthModeUi(); });
    connect(m_ui->roomEdit, &QLineEdit::textChanged, this, [this](const QString&) { updateRoomActionUi(); });

    updateAuthModeUi();
    setConnectedUiState(false);
}

ChatWindow::~ChatWindow()
{
    if (m_socket != nullptr)
    {
        QSignalBlocker blocker(m_socket);
        m_socket->disconnect(this);
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
        {
            m_socket->abort();
        }
        m_socket->setParent(nullptr);
        delete m_socket;
        m_socket = nullptr;
    }

    delete m_ui;
}

void ChatWindow::setupUi()
{
    m_ui->setupUi(this);
    resize(1460, 900);
    setMinimumSize(1180, 760);

    centralWidget()->setStyleSheet(
        "QWidget { background: #f4efe6; color: #2d2a26; font-size: 15px; }"
        "QFrame#connectionCardFrame {"
        "  background: #fffaf1;"
        "  border: 1px solid #dfd2bf;"
        "  border-radius: 16px;"
        "}"
        "QFrame#composerFrame {"
        "  background: #fffaf1;"
        "  border: 1px solid #dfd2bf;"
        "  border-radius: 16px;"
        "}"
        "QFrame#workspaceToolbarFrame, QFrame#guidePanelFrame {"
        "  background: #fffaf1;"
        "  border: 1px solid #dfd2bf;"
        "  border-radius: 14px;"
        "}"
        "QLineEdit, QPlainTextEdit, QListWidget {"
        "  background: #fffdf8;"
        "  border: 1px solid #d9d1c7;"
        "  border-radius: 10px;"
        "  padding: 8px;"
        "}"
        "QTextEdit {"
        "  background: #fffdf8;"
        "  border: 1px solid #d9d1c7;"
        "  border-radius: 14px;"
        "  padding: 8px;"
        "}"
        "QPushButton {"
        "  background: #1f6f78;"
        "  color: white;"
        "  border: 0;"
        "  border-radius: 10px;"
        "  padding: 10px 16px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:disabled { background: #9aa7aa; }"
        "QLabel { color: #473b2f; }"
        "QComboBox {"
        "  background: #fffdf8;"
        "  border: 1px solid #d9d1c7;"
        "  border-radius: 10px;"
        "  padding: 6px 10px;"
        "}"
        "QTabWidget::pane {"
        "  border: 1px solid #d9d1c7;"
        "  border-radius: 12px;"
        "  background: #fffaf4;"
        "  top: -1px;"
        "}"
        "QTabBar::tab {"
        "  background: #e7ddd0;"
        "  color: #5c4b3d;"
        "  padding: 8px 14px;"
        "  margin-right: 4px;"
        "  border-top-left-radius: 8px;"
        "  border-top-right-radius: 8px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: #fffaf4;"
        "  color: #23363d;"
        "  font-weight: 700;"
        "}");

    m_ui->connectionCardTitleLabel->setStyleSheet("font-size: 21px; font-weight: 700; color: #23363d;");
    m_ui->connectionCardSubtitleLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->connectionSummaryLabel->setStyleSheet("color: #5a5147; font-size: 13px; background: #efe4d3; border-radius: 8px; padding: 8px;");
    m_ui->chatFeedTitleLabel->setStyleSheet("font-size: 20px; font-weight: 700; color: #2a3d45;");
    m_ui->composerFrame->setStyleSheet("QFrame#composerFrame { background: #fff8ef; border: 1px solid #dfd2bf; border-radius: 16px; }");
    m_ui->workspaceToolbarFrame->setStyleSheet("QFrame#workspaceToolbarFrame { background: #fff8ef; border: 1px solid #dfd2bf; border-radius: 14px; }");
    m_ui->guidePanelFrame->setStyleSheet("QFrame#guidePanelFrame { background: #fff8ef; border: 1px solid #dfd2bf; border-radius: 14px; }");
    m_ui->workspaceTitleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #23363d;");
    m_ui->workspaceSubtitleLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->activitySummaryLabel->setStyleSheet("color: #5a5147; font-size: 13px; background: #efe4d3; border-radius: 10px; padding: 8px;");
    m_ui->currentRoomCaptionLabel->setStyleSheet("color: #7b6d60; font-size: 12px; font-weight: 600;");
    m_ui->ownerStateLabel->setStyleSheet("color: #6b6257; font-size: 12px; background: #e8ddd0; border-radius: 999px; padding: 4px 10px;");
    m_ui->selectedUserCaptionLabel->setStyleSheet("color: #7b6d60; font-size: 12px; font-weight: 600;");
    m_ui->selectedUserLabel->setStyleSheet("color: #6b6257; font-size: 12px; background: #f2e4cf; border-radius: 999px; padding: 4px 10px;");
    m_ui->guidePanelTitleLabel->setStyleSheet("font-size: 16px; font-weight: 700; color: #23363d;");
    m_ui->connectionGuideLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->roomGuideLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->commandsGuideLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->roomsTitleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #2a3d45;");
    m_ui->roomStatsLabel->setStyleSheet("color: #6b6257; font-size: 12px;");
    m_ui->usersTitleLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #2a3d45;");
    m_ui->userStatsLabel->setStyleSheet("color: #6b6257; font-size: 12px;");
    m_ui->inputHintLabel->setStyleSheet("color: #6b6257; font-size: 13px;");
    m_ui->roomSummaryLabel->setStyleSheet("color: #5a5147; font-size: 13px; background: #efe4d3; border-radius: 8px; padding: 8px;");
    m_ui->statusLabel->setStyleSheet("color: #8a2d2d; font-weight: 700;");
    m_ui->currentRoomLabel->setStyleSheet("color: #8f5a1b; font-weight: 700; background: #f2e4cf; border-radius: 999px; padding: 4px 12px;");

    m_ui->passwordEdit->setEchoMode(QLineEdit::Password);
    m_ui->accountEdit->setPlaceholderText("Account ID");
    m_ui->passwordEdit->setPlaceholderText("Password");
    m_ui->nicknameEdit->setPlaceholderText("Choose a nickname");
    m_ui->logView->setReadOnly(true);
    m_ui->logView->document()->setMaximumBlockCount(500);
    m_ui->logView->setMinimumWidth(940);
    m_ui->logView->setAcceptRichText(true);
    m_ui->rightPanel->setMinimumWidth(330);
    m_ui->rightPanel->setMaximumWidth(400);
    m_ui->roomList->setMinimumHeight(220);
    m_ui->userList->setMinimumHeight(220);
    m_ui->sidebarTabs->setDocumentMode(true);
    m_ui->sidebarTabs->setUsesScrollButtons(false);

    QFont logFont("Consolas");
    logFont.setPointSize(11);
    m_ui->logView->setFont(logFont);
    m_ui->messageEdit->setMinimumHeight(42);
    m_ui->sendButton->setMinimumWidth(120);

    m_ui->centerSplitter->setStretchFactor(0, 7);
    m_ui->centerSplitter->setStretchFactor(1, 3);
    m_ui->centerSplitter->setSizes(QList<int>() << 1080 << 340);

    m_ui->clearLogButton->setMinimumWidth(120);
    m_ui->refreshRoomsButton->setMinimumWidth(110);
    m_ui->helpButton->setMinimumWidth(88);
    m_ui->usersButton->setMinimumWidth(88);
    m_ui->roomsButton->setMinimumWidth(88);
    m_ui->announceButton->setMinimumWidth(96);
    m_ui->botStatusButton->setMinimumWidth(96);
    m_ui->closeRoomButton->setMinimumWidth(96);
    m_ui->kickUserButton->setMinimumWidth(100);
    m_ui->guideToggleButton->setMinimumWidth(92);
    m_ui->guidePanelFrame->setVisible(false);
    m_ui->kickUserButton->setEnabled(false);
    statusBar()->showMessage("Ready");
    updateRoomActionUi();
    updateOwnerControls();
    updateConnectionSummary("Guest mode is the fastest way in. Switch to Login or Register when you want account-based identity.");
    updateActivitySummary("Open the guide or connect to start loading room activity.");
}

void ChatWindow::setConnectedUiState(bool connected)
{
    m_ui->chatFeedTitleLabel->setVisible(connected);
    m_ui->workspaceTitleLabel->setVisible(connected);
    m_ui->workspaceSubtitleLabel->setVisible(connected);
    m_ui->centerSplitter->setVisible(connected);
    m_ui->inputHintLabel->setVisible(connected);
    m_ui->messageEdit->setVisible(connected);
    m_ui->clearLogButton->setVisible(connected);
    m_ui->sendButton->setVisible(connected);

    m_ui->hostEdit->setEnabled(!connected);
    m_ui->portEdit->setEnabled(!connected);
    m_ui->accountEdit->setEnabled(!connected);
    m_ui->passwordEdit->setEnabled(!connected);
    m_ui->authModeCombo->setEnabled(!connected);
    m_ui->nicknameEdit->setEnabled(!connected);
    m_ui->connectButton->setText(connected ? "Disconnect" : "Connect");
    m_ui->connectButton->setEnabled(true);
    m_ui->messageEdit->setEnabled(connected);
    m_ui->sendButton->setEnabled(connected);
    m_ui->roomList->setEnabled(connected);
    m_ui->roomEdit->setEnabled(connected);
    m_ui->roomActionButton->setEnabled(connected);
    m_ui->refreshRoomsButton->setEnabled(connected);
    m_ui->leaveRoomButton->setEnabled(connected);
    m_ui->clearLogButton->setEnabled(true);
    m_ui->helpButton->setEnabled(connected);
    m_ui->usersButton->setEnabled(connected);
    m_ui->roomsButton->setEnabled(connected);
    m_ui->announceButton->setEnabled(connected);
    m_ui->botStatusButton->setEnabled(connected);
    m_ui->closeRoomButton->setEnabled(connected);
    m_ui->messageEdit->setPlaceholderText(
        connected ? "Type a message or command (/help, /list, /w <nickname> ...)"
                  : "Connect first to start chatting");

    if (!connected)
    {
        updateStatus("Disconnected");
        m_ui->currentRoomLabel->setText("Lobby");
        m_ui->ownerStateLabel->setText("Owner controls unavailable");
        m_ui->selectedUserLabel->setText("(none)");
        m_ui->roomList->clear();
        m_ui->userList->clear();
        m_ui->roomStatsLabel->setText("No rooms loaded yet.");
        m_ui->userStatsLabel->setText("No users loaded yet.");
        m_ui->roomSummaryLabel->setText("Choose guest/login/register, then connect to open the chat workspace.");
        m_ui->workspaceSubtitleLabel->setText("Connect first to unlock rooms, member list, and owner controls.");
        updateConnectionSummary("Connection is idle. Pick an auth mode, fill the required fields, and join when ready.");
        updateActivitySummary("Disconnected. Recent room activity will appear here after you reconnect.");
        statusBar()->showMessage("Disconnected");
        m_ui->connectButton->setFocus();
        updateAuthModeUi();
        updateOwnerControls();
    }
    else
    {
        m_ui->workspaceSubtitleLabel->setText("Live room activity, member updates, and owner tools stay organized on the right.");
        updateConnectionSummary("Connected successfully. Host and account inputs are locked until you disconnect.");
        updateActivitySummary("Connected. Syncing current room, people, and recent history.");
        statusBar()->showMessage("Connected");
        m_ui->messageEdit->setFocus();
        updateOwnerControls();
    }
}

void ChatWindow::updateAuthModeUi()
{
    const QString auth_mode = m_ui->authModeCombo->currentText().toLower();
    const bool guest_mode = auth_mode == "guest";
    m_ui->accountTitleLabel->setVisible(!guest_mode);
    m_ui->accountEdit->setVisible(!guest_mode);
    m_ui->passwordTitleLabel->setVisible(!guest_mode);
    m_ui->passwordEdit->setVisible(!guest_mode);

    m_ui->accountEdit->setEnabled(!guest_mode && !m_handshakeComplete);
    m_ui->passwordEdit->setEnabled(!guest_mode && !m_handshakeComplete);

    if (guest_mode)
    {
        m_ui->accountEdit->clear();
        m_ui->passwordEdit->clear();
        m_ui->accountEdit->setPlaceholderText("Guest mode: not required");
        m_ui->passwordEdit->setPlaceholderText("Guest mode: not required");
        m_ui->nicknameEdit->setPlaceholderText("Guest display name");
        if (!m_handshakeComplete)
        {
            m_ui->connectButton->setText("Join as Guest");
            updateConnectionSummary("Guest mode only needs a display name. Use it for quick room testing without an account.");
        }
        m_ui->inputHintLabel->setText("Guest joins immediately. Login/Register keeps account-based history.");
    }
    else
    {
        m_ui->accountEdit->setPlaceholderText("Account ID");
        m_ui->passwordEdit->setPlaceholderText("Password");
        m_ui->nicknameEdit->setPlaceholderText("Display name");
        if (!m_handshakeComplete)
        {
            m_ui->connectButton->setText(auth_mode == "login" ? "Login" : "Register");
            updateConnectionSummary(auth_mode == "login"
                ? "Login reconnects this profile with saved account identity after the server verifies your credentials."
                : "Register creates a new account first, then enters the chat with the display name you choose.");
        }
        m_ui->inputHintLabel->setText("Login uses an existing account. Register creates a new account and joins.");
    }
}

void ChatWindow::toggleGuidePanel()
{
    const bool willShow = !m_ui->guidePanelFrame->isVisible();
    m_ui->guidePanelFrame->setVisible(willShow);
    m_ui->guideToggleButton->setText(willShow ? "Hide Guide" : "Guide");
    statusBar()->showMessage(willShow ? "Guide opened" : "Guide hidden", 2000);
}

void ChatWindow::updateStatus(const QString& text)
{
    m_ui->statusLabel->setText(text);
    const QString color = text.contains("Connected", Qt::CaseInsensitive) ? "#256f3a" : "#8a2d2d";
    m_ui->statusLabel->setStyleSheet(QString("color: %1; font-weight: 700;").arg(color));
}

void ChatWindow::updateConnectionSummary(const QString& text)
{
    m_ui->connectionSummaryLabel->setText(text);
}

void ChatWindow::updateActivitySummary(const QString& text)
{
    m_ui->activitySummaryLabel->setText(text);
}

void ChatWindow::appendLog(const QString& text, const QColor& color)
{
    QString body = text;
    const QString tag = extractLogTag(body);
    const QString tone = color.name();

    QString badgeBackground = tone;
    QString cardBackground = "#fffdf8";
    QString bodyColor = "#2d2a26";

    if (tag == "SYSTEM" || tag == "ROOMS" || tag == "ROOM" || tag == "USERS")
    {
        cardBackground = "#f3efe8";
        bodyColor = "#4b5563";
    }
    else if (tag == "WHISPER")
    {
        cardBackground = "#f6efff";
        bodyColor = "#542788";
    }
    else if (tag == "ERROR")
    {
        cardBackground = "#fff1ef";
        bodyColor = "#b3261e";
    }
    else if (tag == "JOIN" || tag == "LEAVE" || tag == "AUTH" || tag == "CONNECTED")
    {
        cardBackground = "#eef8f0";
        bodyColor = "#256f3a";
    }

    const QString html =
        "<div style=\"margin:0 0 8px 0;\">"
        "<div style=\"background:" + cardBackground + ";"
        "border-left:4px solid " + tone + ";"
        "border-radius:12px;"
        "padding:10px 12px;\">"
        "<span style=\"display:inline-block;"
        "background:" + badgeBackground + ";"
        "color:#ffffff;"
        "font-weight:700;"
        "font-size:11px;"
        "padding:3px 8px;"
        "border-radius:999px;"
        "margin-right:8px;\">"
        + htmlize(tag) +
        "</span>"
        "<span style=\"color:" + bodyColor + "; font-size:13px; line-height:1.5;\">"
        + htmlize(body) +
        "</span>"
        "</div>"
        "</div>";

    m_ui->logView->moveCursor(QTextCursor::End);
    m_ui->logView->insertHtml(html);
    m_ui->logView->insertHtml("<div style=\"height:2px;\"></div>");
    m_ui->logView->moveCursor(QTextCursor::End);
}

void ChatWindow::appendError(const QString& text)
{
    appendLog("[ERROR] " + text, QColor("#b3261e"));
}

void ChatWindow::updateUserList(const QString& payload)
{
    QString cleaned = trimTimestampPrefix(payload);
    const QString roomPrefix = "room [";
    const int roomUsersIndex = cleaned.indexOf("] users: ", 0, Qt::CaseInsensitive);
    if (cleaned.startsWith(roomPrefix, Qt::CaseInsensitive) && roomUsersIndex > 0)
    {
        cleaned = cleaned.mid(roomUsersIndex + 9);
    }

    m_ui->userList->clear();
    const QStringList users = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& user : users)
    {
        if (user == "(none)")
        {
            continue;
        }
        m_ui->userList->addItem(user);
    }

    if (m_ui->selectedUserLabel->text() != "(none)")
    {
        bool selectedStillExists = false;
        for (int i = 0; i < m_ui->userList->count(); ++i)
        {
            if (m_ui->userList->item(i)->text() == m_ui->selectedUserLabel->text())
            {
                selectedStillExists = true;
                break;
            }
        }

        if (!selectedStillExists)
        {
            m_ui->selectedUserLabel->setText("(none)");
        }
    }

    QString summary = m_ui->roomSummaryLabel->text();
    const int users_index = summary.indexOf("\nConnected users");
    if (users_index >= 0)
    {
        summary = summary.left(users_index);
    }
    summary += QString("\nConnected users in room: %1").arg(m_ui->userList->count());
    m_ui->roomSummaryLabel->setText(summary);
    m_ui->userStatsLabel->setText(QString("%1 user(s) currently visible in %2.")
        .arg(m_ui->userList->count())
        .arg(currentRoomName()));
    updateActivitySummary(QString("User list refreshed for %1. %2 member(s) available for whisper or moderation.")
        .arg(currentRoomName())
        .arg(m_ui->userList->count()));
    m_ui->sidebarTabs->setCurrentWidget(m_ui->peopleTab);
    updateOwnerControls();
}

void ChatWindow::updateRoomList(const QString& payload)
{
    QString cleaned = trimTimestampPrefix(payload);
    const QString prefix = "rooms: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_ui->roomList->clear();
    const QStringList rooms = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& room : rooms)
    {
        m_ui->roomList->addItem(room);
    }

    const QString currentRoomName = extractRoomName(m_ui->currentRoomLabel->text());
    for (int i = 0; i < m_ui->roomList->count(); ++i)
    {
        QListWidgetItem* item = m_ui->roomList->item(i);
        if (extractRoomName(item->text()) == currentRoomName)
        {
            item->setSelected(true);
            m_ui->roomList->scrollToItem(item);
            break;
        }
    }

    const QString selected_input = m_ui->roomEdit->text().trimmed();
    m_ui->roomSummaryLabel->setText(QString("Available rooms: %1\nSelected room input: %2")
        .arg(m_ui->roomList->count())
        .arg(selected_input.isEmpty() ? "-" : selected_input));
    m_ui->roomStatsLabel->setText(QString("%1 room(s) available. Double-click a room to join it quickly.")
        .arg(m_ui->roomList->count()));
    updateActivitySummary(QString("Room list refreshed. %1 room(s) are currently available.")
        .arg(m_ui->roomList->count()));
    m_ui->sidebarTabs->setCurrentWidget(m_ui->roomsTab);
    updateRoomActionUi();
    updateOwnerControls();
}

void ChatWindow::updateCurrentRoom(const QString& payload)
{
    QString cleaned = trimTimestampPrefix(payload);
    const QString prefix = "current room: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_ui->currentRoomLabel->setText(cleaned);
    m_ui->roomSummaryLabel->setText("Current room: " + cleaned);
    m_ui->workspaceSubtitleLabel->setText("Now focused on " + extractRoomName(cleaned) + ". Keep chatting while room tools stay grouped by tab.");
    updateActivitySummary("Moved into " + extractRoomName(cleaned) + ". Room-specific activity and history are ready.");
    statusBar()->showMessage("Current room updated: " + cleaned, 3000);
    updateOwnerControls();
}

void ChatWindow::connectToServer()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState || m_handshakeComplete)
    {
        appendLog("[SYSTEM] disconnect requested", QColor("#4c5b61"));
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
        {
            m_socket->abort();
        }
        return;
    }

    const QString host = m_ui->hostEdit->text().trimmed().isEmpty() ? "127.0.0.1" : m_ui->hostEdit->text().trimmed();
    const quint16 port = m_ui->portEdit->text().trimmed().toUShort();
    const QString nickname = m_ui->nicknameEdit->text().trimmed();

    if (nickname.isEmpty())
    {
        appendError("display name is required");
        return;
    }

    if (port == 0)
    {
        appendError("invalid port");
        return;
    }

    m_buffer.clear();
    m_handshakeComplete = false;
    updateStatus("Connecting...");
    updateConnectionSummary(QString("Connecting to %1:%2. Waiting for the socket and authentication handshake to finish.")
        .arg(host)
        .arg(port));
    updateActivitySummary(QString("Attempting connection to %1:%2.")
        .arg(host)
        .arg(port));
    appendLog(QString("[SYSTEM] connecting to %1:%2").arg(host).arg(port), QColor("#4c5b61"));
    statusBar()->showMessage(QString("Connecting to %1:%2").arg(host).arg(port));
    m_ui->connectButton->setText("Disconnect");
    m_socket->connectToHost(host, port);
}

void ChatWindow::socketConnected()
{
    updateStatus("Socket connected");
    updateConnectionSummary("Socket connected. Sending handshake information to the server now.");
    updateActivitySummary("Socket connected. Waiting for nickname or account approval.");
    appendLog("[SYSTEM] socket connected", QColor("#4c5b61"));
    statusBar()->showMessage("Socket connected");

    const QString auth_mode = m_ui->authModeCombo->currentText().toLower();
    if (auth_mode == "login" || auth_mode == "register")
    {
        if (m_ui->accountEdit->text().trimmed().isEmpty()
            || m_ui->passwordEdit->text().isEmpty())
        {
            appendError("account and password are required for login/register");
            m_socket->disconnectFromHost();
            return;
        }

        const QString payload = auth_mode
            + "|"
            + m_ui->accountEdit->text().trimmed()
            + "|"
            + m_ui->passwordEdit->text()
            + "|"
            + m_ui->nicknameEdit->text().trimmed();
        sendPacket(MessageType::AuthRequest, payload);
        return;
    }

    sendPacket(MessageType::Nickname, m_ui->nicknameEdit->text().trimmed());
}

void ChatWindow::socketDisconnected()
{
    setConnectedUiState(false);
    m_handshakeComplete = false;
    updateAuthModeUi();
    updateStatus("Disconnected");
    updateConnectionSummary("Connection closed. You can change auth mode or target host before joining again.");
    updateActivitySummary("Disconnected. Room information is no longer live.");
    appendLog("[SYSTEM] disconnected", QColor("#4c5b61"));
    statusBar()->showMessage("Disconnected");
}

void ChatWindow::socketErrorOccurred(QAbstractSocket::SocketError)
{
    appendError(m_socket->errorString());
    updateConnectionSummary("Connection failed. Check host, port, and required credentials, then try again.");
    updateActivitySummary("Connection failed before synchronization could finish.");
    updateAuthModeUi();
    setConnectedUiState(false);
    statusBar()->showMessage("Connection error");
}

void ChatWindow::sendMessage()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    const QString text = m_ui->messageEdit->text().trimmed();
    if (text.isEmpty())
    {
        return;
    }

    sendPacket(MessageType::Chat, text);
    m_ui->messageEdit->clear();
}

void ChatWindow::joinSelectedRoom(QListWidgetItem* item)
{
    if (!m_handshakeComplete || item == nullptr)
    {
        return;
    }

    const QString roomName = extractRoomName(item->text());
    if (!roomName.isEmpty())
    {
        sendPacket(MessageType::Chat, "/join " + roomName);
    }
}

void ChatWindow::createOrJoinRoom()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    const QString roomName = m_ui->roomEdit->text().trimmed();
    if (roomName.isEmpty())
    {
        appendError("room name is required");
        return;
    }

    bool room_exists = false;
    for (int i = 0; i < m_ui->roomList->count(); ++i)
    {
        if (extractRoomName(m_ui->roomList->item(i)->text()) == roomName)
        {
            room_exists = true;
            break;
        }
    }

    sendPacket(MessageType::Chat, (room_exists ? "/join " : "/create ") + roomName);
    m_ui->roomEdit->clear();
    statusBar()->showMessage(room_exists ? "Join room requested" : "Create room requested", 2500);
}

void ChatWindow::leaveCurrentRoom()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/leave");
}

void ChatWindow::refreshRoomData()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/rooms");
    sendPacket(MessageType::Chat, "/list");
    statusBar()->showMessage("Requested latest room and user data", 3000);
}

void ChatWindow::clearChatLog()
{
    m_ui->logView->clear();
    appendLog("[SYSTEM] chat feed cleared", QColor("#4c5b61"));
    updateActivitySummary("Chat feed cleared locally. Server state is unchanged.");
    statusBar()->showMessage("Chat feed cleared", 2500);
}

void ChatWindow::announceFromInput()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    const QString text = m_ui->messageEdit->text().trimmed();
    if (text.isEmpty())
    {
        appendError("type a notice in the message box first");
        return;
    }

    sendPacket(MessageType::Chat, "/announce " + text);
    m_ui->messageEdit->clear();
    statusBar()->showMessage("Announcement requested", 2500);
}

void ChatWindow::closeCurrentRoom()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/close");
    statusBar()->showMessage("Room close requested", 2500);
}

void ChatWindow::kickSelectedUser()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    const QString target = m_ui->selectedUserLabel->text().trimmed();
    if (target.isEmpty() || target == "(none)")
    {
        appendError("select a user first");
        return;
    }

    sendPacket(MessageType::Chat, "/kick " + target);
    statusBar()->showMessage("Kick requested for " + target, 2500);
}

void ChatWindow::requestBotStatus()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/bot status");
    statusBar()->showMessage("Requested bot status", 2500);
}

void ChatWindow::requestHelp()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/help");
    statusBar()->showMessage("Requested command help", 2500);
}

void ChatWindow::requestUserList()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    sendPacket(MessageType::Chat, "/list");
    statusBar()->showMessage("Requested current room users", 2500);
}

void ChatWindow::selectUserForAdmin(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }

    const QString target = item->text().trimmed();
    m_ui->selectedUserLabel->setText(target.isEmpty() ? "(none)" : target);
    updateOwnerControls();
}

void ChatWindow::prepareWhisper(QListWidgetItem* item)
{
    if (!m_handshakeComplete || item == nullptr)
    {
        return;
    }

    const QString target = item->text().trimmed();
    if (target.isEmpty())
    {
        return;
    }

    m_ui->messageEdit->setText("/w " + target + " ");
    m_ui->messageEdit->setFocus();
    m_ui->messageEdit->setCursorPosition(m_ui->messageEdit->text().size());
    m_ui->sidebarTabs->setCurrentWidget(m_ui->peopleTab);
    statusBar()->showMessage("Whisper target selected: " + target, 3000);
}

void ChatWindow::syncRoomInputFromSelection(QListWidgetItem* item)
{
    if (item == nullptr)
    {
        return;
    }

    m_ui->roomEdit->setText(extractRoomName(item->text()));
    m_ui->sidebarTabs->setCurrentWidget(m_ui->roomsTab);
    updateRoomActionUi();
}

void ChatWindow::updateRoomActionUi()
{
    const QString room_name = m_ui->roomEdit->text().trimmed();
    if (room_name.isEmpty())
    {
        m_ui->roomActionButton->setText("Create / Join");
        return;
    }

    QString action_text = "Create Room";
    for (int i = 0; i < m_ui->roomList->count(); ++i)
    {
        if (extractRoomName(m_ui->roomList->item(i)->text()) == room_name)
        {
            action_text = "Join Selected";
            break;
        }
    }

    m_ui->roomActionButton->setText(action_text);
}

QString ChatWindow::currentOwnerName() const
{
    return extractOwnerName(m_ui->currentRoomLabel->text());
}

QString ChatWindow::currentRoomName() const
{
    return extractRoomName(m_ui->currentRoomLabel->text());
}

void ChatWindow::updateOwnerControls()
{
    const bool connected = m_handshakeComplete;
    const QString owner_name = currentOwnerName();
    const QString my_name = m_ui->nicknameEdit->text().trimmed();
    const QString room_name = currentRoomName();
    const bool is_custom_room = connected && room_name.compare("Lobby", Qt::CaseInsensitive) != 0;
    const bool is_owner = is_custom_room && !owner_name.isEmpty() && owner_name == my_name;

    m_ui->announceButton->setEnabled(is_owner);
    m_ui->closeRoomButton->setEnabled(is_owner);
    m_ui->kickUserButton->setEnabled(is_owner && m_ui->selectedUserLabel->text() != "(none)");
    m_ui->botStatusButton->setEnabled(is_custom_room);

    QString summary = m_ui->roomSummaryLabel->text();
    const int owner_state_index = summary.indexOf("\nOwner controls:");
    if (owner_state_index >= 0)
    {
        summary = summary.left(owner_state_index);
    }

    if (!connected)
    {
        summary += "\nOwner controls: unavailable";
        m_ui->ownerStateLabel->setText("Owner controls unavailable");
        m_ui->ownerStateLabel->setStyleSheet("color: #6b6257; font-size: 12px; background: #e8ddd0; border-radius: 999px; padding: 4px 10px;");
    }
    else if (is_owner)
    {
        summary += "\nOwner controls: active";
        m_ui->ownerStateLabel->setText("Owner controls active");
        m_ui->ownerStateLabel->setStyleSheet("color: #ffffff; font-size: 12px; background: #1f6f78; border-radius: 999px; padding: 4px 10px;");
    }
    else if (is_custom_room)
    {
        summary += "\nOwner controls: view only";
        m_ui->ownerStateLabel->setText("Viewing custom room");
        m_ui->ownerStateLabel->setStyleSheet("color: #5a5147; font-size: 12px; background: #f0dfc4; border-radius: 999px; padding: 4px 10px;");
    }
    else
    {
        summary += "\nOwner controls: Lobby has no owner actions";
        m_ui->ownerStateLabel->setText("Lobby has no owner actions");
        m_ui->ownerStateLabel->setStyleSheet("color: #6b6257; font-size: 12px; background: #e8ddd0; border-radius: 999px; padding: 4px 10px;");
    }

    m_ui->roomSummaryLabel->setText(summary);
    if (is_owner || is_custom_room)
    {
        m_ui->sidebarTabs->setTabText(m_ui->sidebarTabs->indexOf(m_ui->adminTab), is_owner ? "Room Tools *" : "Room Tools");
    }
    else
    {
        m_ui->sidebarTabs->setTabText(m_ui->sidebarTabs->indexOf(m_ui->adminTab), "Room Tools");
    }
}

void ChatWindow::sendPacket(MessageType type, const QString& payload)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
    {
        appendError("socket is not connected");
        return;
    }

    const QByteArray packet = buildPacket(type, payload);
    m_socket->write(packet);
}

void ChatWindow::readFromSocket()
{
    m_buffer.append(m_socket->readAll());
    while (processBufferedPacket())
    {
    }
}

bool ChatWindow::processBufferedPacket()
{
    if (m_buffer.size() < kHeaderSize)
    {
        return false;
    }

    PacketHeader rawHeader;
    std::memcpy(&rawHeader, m_buffer.constData(), sizeof(rawHeader));

    const uint32_t typeValue = qFromBigEndian(rawHeader.type);
    const uint32_t payloadSize = qFromBigEndian(rawHeader.size);

    if (m_buffer.size() < kHeaderSize + static_cast<int>(payloadSize))
    {
        return false;
    }

    const QByteArray payloadBytes = m_buffer.mid(kHeaderSize, static_cast<int>(payloadSize));
    m_buffer.remove(0, kHeaderSize + static_cast<int>(payloadSize));

    handlePacket(static_cast<MessageType>(typeValue), QString::fromUtf8(payloadBytes));
    return true;
}

void ChatWindow::handlePacket(MessageType type, const QString& payload)
{
    switch (type)
    {
    case MessageType::NicknameAccepted:
    case MessageType::AuthSuccess:
        m_handshakeComplete = true;
        m_ui->passwordEdit->clear();
        setConnectedUiState(true);
        updateStatus("Connected");
        updateConnectionSummary("Handshake completed. Room data and member lists are syncing now.");
        updateActivitySummary("Authentication approved. Pulling room list, member list, and recent history.");
        appendLog((type == MessageType::AuthSuccess ? "[AUTH] " : "[CONNECTED] ") + payload, QColor("#256f3a"));
        sendPacket(MessageType::Chat, "/list");
        sendPacket(MessageType::Chat, "/rooms");
        statusBar()->showMessage("Connected and synchronized", 4000);
        updateOwnerControls();
        break;
    case MessageType::NicknameRejected:
    case MessageType::AuthFailure:
        updateConnectionSummary("Authentication was rejected. Review the message below, adjust the inputs, and reconnect.");
        updateActivitySummary("Authentication failed. No room data was loaded.");
        appendError(payload);
        m_socket->disconnectFromHost();
        break;
    case MessageType::System:
    case MessageType::SystemInfo:
        appendLog("[SYSTEM] " + payload, QColor("#4c5b61"));
        break;
    case MessageType::SystemJoin:
        appendLog("[JOIN] " + payload, QColor("#256f3a"));
        break;
    case MessageType::SystemLeave:
        appendLog("[LEAVE] " + payload, QColor("#8a2d2d"));
        break;
    case MessageType::SystemError:
        appendError(payload);
        break;
    case MessageType::UserList:
        appendLog("[USERS] " + payload, QColor("#5e548e"));
        updateUserList(payload);
        break;
    case MessageType::RoomList:
        appendLog("[ROOMS] " + payload, QColor("#15616d"));
        updateRoomList(payload);
        break;
    case MessageType::RoomChanged:
        appendLog("[ROOM] " + payload, QColor("#8f5a1b"));
        updateCurrentRoom(payload);
        sendPacket(MessageType::Chat, "/list");
        break;
    case MessageType::RoomHistory:
        appendLog("[HISTORY]\n" + payload, QColor("#5f0f40"));
        updateActivitySummary("Recent room history loaded into the feed.");
        statusBar()->showMessage("Loaded recent room history", 3000);
        break;
    case MessageType::NicknameChanged:
        appendLog("[NAME] " + payload, QColor("#8f5a1b"));
        updateOwnerControls();
        break;
    case MessageType::Whisper:
        appendLog("[WHISPER] " + payload, QColor("#7b2cbf"));
        break;
    case MessageType::Chat:
        appendLog(payload);
        break;
    default:
        appendError("unknown packet received");
        break;
    }
}


