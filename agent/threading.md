# 线程模型

## 谁在主线程，谁不在

| 工作 | 线程 | 说明 |
|---|---|---|
| QML 渲染、所有 QObject 的槽 | 主线程 | |
| 互联网探测（`msftconnecttest`） | 主线程发起，**I/O 异步** | `QNetworkAccessManager` 本身不阻塞 |
| 本地网卡探测 | **工作线程池** | `GetIpForwardTable` + 网卡枚举是阻塞系统调用 |
| 验证码 OCR | **专属 QThread** | Tesseract 初始化 + 多次变体识别，几百 ms 到数秒 |
| 系统设置写入（自启动 / NCSI 策略） | **工作线程池**（`Controller::m_workPool`） | 写注册表；改 NCSI 还要弹 UAC 并等提权进程，最长 60 s |
| Portal 协议请求 | 主线程发起，I/O 异步 | |
| 退避定时器 | 主线程 | `QTimer` |

原则：**只把"阻塞的 CPU / 系统调用"挪出主线程，异步 I/O 不需要挪。**

## 1. 本地网卡探测（`Network` + `LanProbe`）

```cpp
void Network::checkLan()
{
    if (m_lanProbeRunning)              // 同时只允许一个在途
        return;

    m_lanProbeRunning = true;
    updateChecking();

    m_lanPool.start(QRunnable::create([this]()
        {
            const LanProbe::Result result = LanProbe::probe();   // 工作线程：只算

            QMetaObject::invokeMethod(this, [this, result]()
                {
                    m_lanProbeRunning = false;                    // 主线程：只在这改状态
                    applyLanState(result.connected, result.description);
                    updateChecking();
                }, Qt::QueuedConnection);
        }));
}
```

线程安全要点：

- `LanProbe::probe()` 是**纯函数**：只碰局部变量 + `Platform::defaultRouteInterfaceIndex()`
  和 `QNetworkInterface`（都是可重入的），不读不写任何成员；
- 结果通过 `Qt::QueuedConnection` 回到主线程，成员变量只在主线程修改；
- `m_lanPool` 是 `Network` 的成员，`maxThreadCount = 1`（探测串行，天然互斥）；
- 析构里先 `m_lanPool.waitForDone()`，保证没有线程还在跑，排队的回调由
  `~QObject` 统一清掉。

## 2. 验证码 OCR（`CaptchaOcr`）

两段式排队：

```
主线程                         工作线程                     主线程
recognize(img, cb)
  m_token++
  invokeMethod(m_context, →)  ──→  m_engine->recognize(img)
                                    invokeMethod(this, ←)  ──→  token 比对
                                                                  cb(text)
```

线程安全要点：

- `CaptchaOcrEngine`（含 `tesseract::TessBaseAPI`）**只在工作线程里被访问**；
  主线程只在 `m_thread->wait()` 之后才 `delete` 它；
- 引擎常驻复用（`Init` 一次，之后 `Clear()` + `SetImage()`），避免每次重载语言包；
- `m_token` / `m_callback` **只在主线程**读写；
- 过期结果靠 token 比对丢弃：`if (token != m_token) return;`
- `cancel()` 只 `++m_token` 并清回调，不打断正在进行的识别（只是不再回调）。

> 曾试过"空闲 90 秒自动卸载模型再懒加载"，能省几十 MB 常驻内存，
> 但会给下次识别加几百毫秒且逻辑更绕，最后撤回了 —— 引擎保持常驻。

## 3. 系统设置写入（`Controller`）

`setAutoStart()` 和 `setBlockAutoBrowser()` 都是「立即返回 + 后台执行」：

```cpp
void Controller::setBlockAutoBrowser(bool blocked)
{
    if (m_blockAutoBrowserBusy || m_blockAutoBrowser == blocked)
        return;
    m_blockAutoBrowserBusy = true;
    emit blockAutoBrowserBusyChanged();   // 界面禁用开关 + 转圈
    applyBlockAutoBrowser(blocked);       // 丢到 m_workPool
}
```

后台做的工作：

```
直接写注册表  ──失败──>  ShellExecuteEx(runas) 弹 UAC
                              └─ WaitForSingleObject 等提权进程（最长 60 s）
读完注册表确认实际状态 → invokeMethod 回主线程更新 m_blockAutoBrowser / busy / error
```

线程安全要点：

- `m_workPool` 只有一条线程，两个设置操作天然串行，`NcsiProbing` 内部那个
  静态 `errorBuffer()` 不会被并发访问；
- 工作线程只捕获 `blocked` / `enabled` 这两个**值**，不读任何成员变量；
- 所有成员（`m_blockAutoBrowser` / `m_autoStart` / busy 标记）只在主线程改；
- 析构里 `m_workPool.waitForDone()`。

**注意 `SEE_MASK_NOASYNC`**：`Platform::runElevatedAndWait()` 在后台线程调用
`ShellExecuteExW`，而线程池线程没有消息循环。不加这个标志，ShellExecuteEx
会把操作转给后台线程后立即返回，"等提权进程结束"的语义就失效了。

## 4. 为什么不用 QtConcurrent

`QtConcurrent::run` 需要额外链接 `Qt::Concurrent` 模块。
这里用 `QThreadPool` + `QRunnable::create`（都在 QtCore）就够了，
不增加构建依赖。

## 加新异步工作时的检查清单

1. 这段代码会不会阻塞超过几毫秒？会 → 挪到线程 / 线程池。
2. 工作线程里有没有碰任何成员变量？有 → 改成只用局部变量，结果回主线程再写。
3. 对象析构时，工作线程还可能回调吗？→ 要么 `wait()`，要么用 `QPointer` / token 校验。
4. 有没有"只有一个在途"的约束？→ 用 `bool m_xxxRunning` 或 `maxThreadCount = 1` 保证。
5. 取消路径是否完整？→ `stop()` 里要把所有异步源都掐掉（HTTP abort、OCR cancel、定时器 stop）。

## 容易踩的坑

- **不要在 `QThreadPool` 任务里 `delete` 主线程对象**，也不要 `emit` 后直接析构发送者。
- **`QImage` 可以跨线程传**（隐式共享，拷贝指针不拷贝像素），但别在工作线程里
  `scanLine()` 改写主线程正在显示的同一个实例。
- **`QNetworkReply` 只在主线程用**，别把整个 `Network` 对象 `moveToThread`。
- 线程池任务里访问 `this` 的成员前，确认 `this` 还活着（析构里 `waitForDone()`）。