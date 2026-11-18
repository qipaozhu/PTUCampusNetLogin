#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

/// 单实例守卫：
/// 第一个启动的进程用 QLocalServer 监听固定名字，后续启动的进程用 QLocalSocket 连接它，
/// 通知它把主界面显示出来，然后自己直接退出。
class SingleInstance : public QObject
{
	Q_OBJECT

public:
	explicit SingleInstance(const QString& key, QObject* parent = nullptr);
	~SingleInstance() override;

	/// true 表示本进程是第一个实例，应当继续运行
	bool isPrimary() const { return m_primary; }

signals:
	/// 收到其他实例的“显示主界面”请求
	void showRequested();

private:
	void handleNewConnection();

	QString m_key;
	bool m_primary = false;
	QLocalServer* m_server = nullptr;
};
