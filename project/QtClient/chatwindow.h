#pragma once

#include <QByteArray>
#include <QColor>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QTcpSocket>

#include "protocol.h"

namespace Ui
{
class ChatWindow;
}

class ChatWindow : public QMainWindow
{
public:
    explicit ChatWindow(QWidget* parent = nullptr);
    ~ChatWindow();

private:
    void connectToServer();
    void sendMessage();
    void readFromSocket();
    void socketConnected();
    void socketDisconnected();
    void socketErrorOccurred(QAbstractSocket::SocketError socketError);
    void joinSelectedRoom(QListWidgetItem* item);
    void createOrJoinRoom();
    void leaveCurrentRoom();
    void refreshRoomData();
    void clearChatLog();
    void updateAuthModeUi();
    void toggleGuidePanel();
    void requestHelp();
    void requestUserList();
    void prepareWhisper(QListWidgetItem* item);
    void selectUserForAdmin(QListWidgetItem* item);
    void syncRoomInputFromSelection(QListWidgetItem* item);
    void announceFromInput();
    void closeCurrentRoom();
    void kickSelectedUser();
    void requestBotStatus();
    void updateOwnerControls();

private:
    void setupUi();
    void setConnectedUiState(bool connected);
    void updateStatus(const QString& text);
    void updateConnectionSummary(const QString& text);
    void updateActivitySummary(const QString& text);
    void appendLog(const QString& text, const QColor& color = QColor(Qt::black));
    void appendError(const QString& text);
    void updateUserList(const QString& payload);
    void updateRoomList(const QString& payload);
    void updateCurrentRoom(const QString& payload);
    void updateRoomActionUi();
    QString currentOwnerName() const;
    QString currentRoomName() const;
    bool processBufferedPacket();
    void handlePacket(MessageType type, const QString& payload);
    void sendPacket(MessageType type, const QString& payload);

    Ui::ChatWindow* m_ui;
    QTcpSocket* m_socket;
    QByteArray m_buffer;
    bool m_handshakeComplete;
};
