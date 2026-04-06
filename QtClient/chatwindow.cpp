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
      m_logView(nullptr),
      m_userList(nullptr),
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

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);

    m_userList = new QListWidget(this);
    m_userList->setMinimumWidth(220);

    m_messageEdit = new QLineEdit(this);
    m_messageEdit->setPlaceholderText("Connect first to start chatting");
    m_sendButton = new QPushButton("Send", this);

    auto* formLayout = new QFormLayout();
    formLayout->addRow("Host", m_hostEdit);
    formLayout->addRow("Port", m_portEdit);
    formLayout->addRow("Nickname", m_nicknameEdit);
    formLayout->addRow("Status", m_statusLabel);

    auto* topLayout = new QHBoxLayout();
    topLayout->addLayout(formLayout, 1);
    topLayout->addWidget(m_connectButton);

    auto* rightPanelLayout = new QVBoxLayout();
    rightPanelLayout->addWidget(new QLabel("Connected Users", this));
    rightPanelLayout->addWidget(m_userList, 1);
    rightPanelLayout->addWidget(new QLabel("Commands: /help, /list, /name <new>, /w <nick> <msg>", this));

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
    m_messageEdit->setPlaceholderText(
        connected ? "Type a message or command (/help, /list, /w <nickname> ...)"
                  : "Connect first to start chatting");
    if (!connected)
    {
        updateStatus("Disconnected");
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
    const QString prefix = "connected users: ";
    if (cleaned.startsWith(prefix, Qt::CaseInsensitive))
    {
        cleaned = cleaned.mid(prefix.size());
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
    m_userList->clear();
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
        sendPacket(MessageType::Chat, "/list");
        break;
    case MessageType::SystemLeave:
        appendLog("[LEAVE] " + payload, QColor("#8a2d2d"));
        sendPacket(MessageType::Chat, "/list");
        break;
    case MessageType::SystemError:
        appendError(payload);
        break;
    case MessageType::UserList:
        appendLog("[USERS] " + payload, QColor("#5e548e"));
        updateUserList(payload);
        break;
    case MessageType::NicknameChanged:
        appendLog("[NAME] " + payload, QColor("#8f5a1b"));
        sendPacket(MessageType::Chat, "/list");
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
