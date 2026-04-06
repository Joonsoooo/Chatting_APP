#pragma once

#include <QByteArray>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QListWidget>
#include <QTcpSocket>

#include "protocol.h"

class ChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit ChatWindow(QWidget* parent = nullptr);

private slots:
    void connectToServer();
    void sendMessage();
    void readFromSocket();
    void socketConnected();
    void socketDisconnected();
    void socketErrorOccurred(QAbstractSocket::SocketError socketError);

private:
    void setupUi();
    void setConnectedUiState(bool connected);
    void updateStatus(const QString& text);
    void appendLog(const QString& text, const QColor& color = QColor(Qt::black));
    void appendError(const QString& text);
    void updateUserList(const QString& payload);
    bool processBufferedPacket();
    void handlePacket(MessageType type, const QString& payload);
    void sendPacket(MessageType type, const QString& payload);

    QTcpSocket* m_socket;
    QByteArray m_buffer;
    bool m_handshakeComplete;

    QWidget* m_centralWidget;
    QLineEdit* m_hostEdit;
    QLineEdit* m_portEdit;
    QLineEdit* m_nicknameEdit;
    QPushButton* m_connectButton;
    QLabel* m_statusLabel;
    QPlainTextEdit* m_logView;
    QListWidget* m_userList;
    QLineEdit* m_messageEdit;
    QPushButton* m_sendButton;
};
