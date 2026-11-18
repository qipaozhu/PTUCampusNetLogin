#include "singleinstance.h"

#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>

namespace
{
	/// 第二个实例最多等这么久来连上已有实例
	const int kConnectTimeoutMs = 300;
	/// 连上后发送的请求内容，目前只用“显示主界面”这一种
	const char kShowCommand[] = "show";

	/// 尝试连接已有实例并发送“显示主界面”请求，成功返回 true
	bool notifyExistingInstance(const QString& key)
	{
		QLocalSocket socket;
		socket.connectToServer(key);
		if (!socket.waitForConnected(kConnectTimeoutMs))
			return false;

		socket.write(kShowCommand);
		socket.flush();
		socket.waitForBytesWritten(kConnectTimeoutMs);
		socket.disconnectFromServer();
		return true;
	}
}

/// 构造函数：先尝试连接已有实例，连不上再自己监听
SingleInstance::SingleInstance(const QString& key, QObject* parent)
	: QObject(parent)
	, m_key(key)
{
	// 已经有实例在运行：通知它显示主界面，本进程不再继续初始化
	if (notifyExistingInstance(m_key))
		return;

	// 没有实例在运行（或上次异常退出留下了残留套接字）：清掉残留后开始监听
	QLocalServer::removeServer(m_key);

	m_server = new QLocalServer(this);
	m_primary = m_server->listen(m_key);

	if (m_primary)
	{
		connect(m_server, &QLocalServer::newConnection,
			this, &SingleInstance::handleNewConnection);
		return;
	}

	// 极端情况下两个进程同时启动，自己抢监听失败：再试着通知刚抢到的那一个
	if (notifyExistingInstance(m_key))
	{
		m_primary = false;
		return;
	}

	// 监听失败但也没人响应（例如同名管道被其他用户占用）：仍然放行，避免程序无法启动
	qWarning() << "单实例监听失败：" << m_server->errorString();
	m_primary = true;
}

/// 析构函数：关闭本地套接字监听
SingleInstance::~SingleInstance()
{
	if (m_server)
		m_server->close();
}

/// 有新实例连入：读取请求并通知界面显示
void SingleInstance::handleNewConnection()
{
	if (!m_server)
		return;

	while (QLocalSocket* socket = m_server->nextPendingConnection())
	{
		connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

		// 请求内容目前不用区分，连接上就视为“显示主界面”
		socket->readAll();
		emit showRequested();
	}
}
