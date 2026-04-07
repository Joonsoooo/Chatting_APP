#include "chatwindow.h"

#include <QAbstractSocket>
#include <QColor>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QVBoxLayout>

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
}

ChatWindow::ChatWindow(QWidget* parent)
    : QMainWindow(parent),
      m_socket(new QTcpSocket(this)),
      m_handshakeComplete(false),
      m_centralWidget(nullptr),
      m_hostEdit(nullptr),
      m_portEdit(nullptr),
      m_nicknameEdit(nullptr),
      m_connectButton(nullptr),
      m_statusLabel(nullptr),
      m_currentRoomLabel(nullptr),
      m_logView(nullptr),
      m_userList(nullptr),
      m_roomList(nullptr),
      m_roomEdit(nullptr),
      m_roomActionButton(nullptr),
      m_leaveRoomButton(nullptr),
      m_messageEdit(nullptr),
      m_sendButton(nullptr)
{
    setupUi();

    connect(m_connectButton, &QPushButton::clicked, this, &ChatWindow::connectToServer);
    connect(m_sendButton, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_messageEdit, &QLineEdit::returnPressed, this, &ChatWindow::sendMessage);

    connect(m_socket, &QTcpSocket::connected, this, &ChatWindow::socketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &ChatWindow::socketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &ChatWindow::readFromSocket);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &ChatWindow::socketErrorOccurred);
    connect(m_roomList, &QListWidget::itemDoubleClicked, this, &ChatWindow::joinSelectedRoom);
    connect(m_roomActionButton, &QPushButton::clicked, this, &ChatWindow::createOrJoinRoom);
    connect(m_leaveRoomButton, &QPushButton::clicked, this, &ChatWindow::leaveCurrentRoom);
    connect(m_roomEdit, &QLineEdit::returnPressed, this, &ChatWindow::createOrJoinRoom);

    setConnectedUiState(false);
}

void ChatWindow::setupUi()
{
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);
    setWindowTitle("Qt Chat Client");
    resize(980, 620);
    m_centralWidget->setStyleSheet(
        "QWidget { background: #f7f2ea; color: #2d2a26; }"
        "QLineEdit, QPlainTextEdit, QListWidget {"
        "  background: #fffdf8;"
        "  border: 1px solid #d9d1c7;"
        "  border-radius: 8px;"
        "  padding: 6px;"
        "}"
        "QPushButton {"
        "  background: #1f6f78;"
        "  color: white;"
        "  border: 0;"
        "  border-radius: 8px;"
        "  padding: 8px 14px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:disabled { background: #9aa7aa; }"
        "QLabel { color: #473b2f; }");

    m_hostEdit = new QLineEdit("127.0.0.1", this);
    m_portEdit = new QLineEdit("9000", this);
    m_nicknameEdit = new QLineEdit(this);
    m_nicknameEdit->setPlaceholderText("Choose a nickname");
    m_connectButton = new QPushButton("Connect", this);
    m_statusLabel = new QLabel("Disconnected", this);
    m_statusLabel->setStyleSheet("QLabel { color: #8a2d2d; font-weight: 600; }");
    m_currentRoomLabel = new QLabel("Lobby", this);
    m_currentRoomLabel->setStyleSheet("QLabel { color: #8f5a1b; font-weight: 600; }");

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);

    m_userList = new QListWidget(this);
    m_userList->setMinimumWidth(220);
    m_roomList = new QListWidget(this);
    m_roomList->setMinimumWidth(220);
    m_roomEdit = new QLineEdit(this);
    m_roomEdit->setPlaceholderText("Room name");
    m_roomActionButton = new QPushButton("Create / Join", this);
    m_leaveRoomButton = new QPushButton("Leave Room", this);

    m_messageEdit = new QLineEdit(this);
    m_messageEdit->setPlaceholderText("Connect first to start chatting");
    m_sendButton = new QPushButton("Send", this);

    auto* formLayout = new QFormLayout();
    formLayout->addRow("Host", m_hostEdit);
    formLayout->addRow("Port", m_portEdit);
    formLayout->addRow("Nickname", m_nicknameEdit);
    formLayout->addRow("Status", m_statusLabel);
    formLayout->addRow("Room", m_currentRoomLabel);

    auto* topLayout = new QHBoxLayout();
    topLayout->addLayout(formLayout, 1);
    topLayout->addWidget(m_connectButton);

    auto* rightPanelLayout = new QVBoxLayout();
    rightPanelLayout->addWidget(new QLabel("Rooms", this));
    rightPanelLayout->addWidget(m_roomList, 1);
    rightPanelLayout->addWidget(m_roomEdit);
    rightPanelLayout->addWidget(m_roomActionButton);
    rightPanelLayout->addWidget(m_leaveRoomButton);
    rightPanelLayout->addWidget(new QLabel("Connected Users", this));
    rightPanelLayout->addWidget(m_userList, 1);
    rightPanelLayout->addWidget(new QLabel("Commands: /help, /list, /rooms, /create <room>, /join <room>, /leave, /close, /announce <msg>, /kick <nick>, /bot [on|off|status], /name <new>, /w <nick> <msg>", this));

    auto* rightPanel = new QWidget(this);
    rightPanel->setLayout(rightPanelLayout);

    auto* centerSplitter = new QSplitter(this);
    centerSplitter->addWidget(m_logView);
    centerSplitter->addWidget(rightPanel);
    centerSplitter->setStretchFactor(0, 4);
    centerSplitter->setStretchFactor(1, 1);

    auto* inputLayout = new QHBoxLayout();
    inputLayout->addWidget(m_messageEdit, 1);
    inputLayout->addWidget(m_sendButton);

    auto* rootLayout = new QVBoxLayout(m_centralWidget);
    rootLayout->addLayout(topLayout);
    rootLayout->addWidget(new QLabel("Chat Feed", this));
    rootLayout->addWidget(centerSplitter, 1);
    rootLayout->addLayout(inputLayout);
}

void ChatWindow::setConnectedUiState(bool connected)
{
    m_hostEdit->setEnabled(!connected);
    m_portEdit->setEnabled(!connected);
    m_nicknameEdit->setEnabled(!connected);
    m_connectButton->setText(connected ? "Connected" : "Connect");
    m_connectButton->setEnabled(!connected);
    m_messageEdit->setEnabled(connected);
    m_sendButton->setEnabled(connected);
    m_roomList->setEnabled(connected);
    m_roomEdit->setEnabled(connected);
    m_roomActionButton->setEnabled(connected);
    m_leaveRoomButton->setEnabled(connected);
    m_messageEdit->setPlaceholderText(
        connected ? "Type a message or command (/help, /list, /w <nickname> ...)"
                  : "Connect first to start chatting");
    if (!connected)
    {
        updateStatus("Disconnected");
        m_currentRoomLabel->setText("Lobby");
        m_roomList->clear();
        m_userList->clear();
        m_connectButton->setFocus();
    }
    else
    {
        m_messageEdit->setFocus();
    }
}

void ChatWindow::updateStatus(const QString& text)
{
    m_statusLabel->setText(text);
    const QString color = text.contains("Connected", Qt::CaseInsensitive) ? "#256f3a" : "#8a2d2d";
    m_statusLabel->setStyleSheet(QString("QLabel { color: %1; font-weight: 600; }").arg(color));
}

void ChatWindow::appendLog(const QString& text, const QColor& color)
{
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat format;
    format.setForeground(color);

    cursor.insertText(text + "\n", format);
    m_logView->setTextCursor(cursor);
    m_logView->ensureCursorVisible();
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

    m_userList->clear();
    const QStringList users = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& user : users)
    {
        if (user == "(none)")
        {
            continue;
        }
        m_userList->addItem(user);
    }
}

void ChatWindow::updateRoomList(const QString& payload)
{
    QString cleaned = trimTimestampPrefix(payload);
    const QString prefix = "rooms: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_roomList->clear();
    const QStringList rooms = cleaned.split(", ", Qt::SkipEmptyParts);
    for (const QString& room : rooms)
    {
        m_roomList->addItem(room);
    }

    const QString currentRoomName = extractRoomName(m_currentRoomLabel->text());
    for (int i = 0; i < m_roomList->count(); ++i)
    {
        QListWidgetItem* item = m_roomList->item(i);
        if (extractRoomName(item->text()) == currentRoomName)
        {
            item->setSelected(true);
            m_roomList->scrollToItem(item);
            break;
        }
    }
}

void ChatWindow::updateCurrentRoom(const QString& payload)
{
    QString cleaned = trimTimestampPrefix(payload);
    const QString prefix = "current room: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
    }

    m_currentRoomLabel->setText(cleaned);
}

void ChatWindow::connectToServer()
{
    const QString host = m_hostEdit->text().trimmed().isEmpty() ? "127.0.0.1" : m_hostEdit->text().trimmed();
    const quint16 port = m_portEdit->text().trimmed().toUShort();
    const QString nickname = m_nicknameEdit->text().trimmed();

    if (nickname.isEmpty())
    {
        appendError("nickname is required");
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
    appendLog(QString("[SYSTEM] connecting to %1:%2").arg(host).arg(port), QColor("#4c5b61"));
    m_socket->connectToHost(host, port);
}

void ChatWindow::socketConnected()
{
    updateStatus("Socket connected");
    appendLog("[SYSTEM] socket connected", QColor("#4c5b61"));
    sendPacket(MessageType::Nickname, m_nicknameEdit->text().trimmed());
}

void ChatWindow::socketDisconnected()
{
    setConnectedUiState(false);
    m_handshakeComplete = false;
    updateStatus("Disconnected");
    appendLog("[SYSTEM] disconnected", QColor("#4c5b61"));
}

void ChatWindow::socketErrorOccurred(QAbstractSocket::SocketError)
{
    appendError(m_socket->errorString());
    setConnectedUiState(false);
}

void ChatWindow::sendMessage()
{
    if (!m_handshakeComplete)
    {
        appendError("connect and complete handshake first");
        return;
    }

    const QString text = m_messageEdit->text().trimmed();
    if (text.isEmpty())
    {
        return;
    }

    sendPacket(MessageType::Chat, text);
    m_messageEdit->clear();
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

    const QString roomName = m_roomEdit->text().trimmed();
    if (roomName.isEmpty())
    {
        appendError("room name is required");
        return;
    }

    sendPacket(MessageType::Chat, "/create " + roomName);
    m_roomEdit->clear();
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
    memcpy(&rawHeader, m_buffer.constData(), sizeof(rawHeader));

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
        m_handshakeComplete = true;
        setConnectedUiState(true);
        updateStatus("Connected");
        appendLog("[CONNECTED] " + payload, QColor("#256f3a"));
        sendPacket(MessageType::Chat, "/list");
        sendPacket(MessageType::Chat, "/rooms");
        break;
    case MessageType::NicknameRejected:
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
        break;
    case MessageType::NicknameChanged:
        appendLog("[NAME] " + payload, QColor("#8f5a1b"));
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
