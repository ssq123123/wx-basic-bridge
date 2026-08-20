# 微信 4.1.9.23 基础版

从完整版 `4.1.9.23` 抽出的最小收发工程，仅保留：

- 文本发送
- `DoAddMsg` 消息接收
- `bridge/outbox` / `bridge/inbox` 文件桥
- SQLite 消息归档
- 编译与注入工具

不包含红包、转发、朋友圈、公众号、群管理、联系人扫描、HTTP 面板、防撤回等扩展功能。

## 环境

- Windows x64
- 微信 PC `4.1.9.23`
- Python 3.11+
- Visual Studio 2022 C++ Build Tools

偏移仅适配 `Weixin.dll 4.1.9.23`。其他版本不能直接使用。

## 编译

```powershell
powershell -ExecutionPolicy Bypass -File native\wx_hook_bridge\build.ps1
```

输出：`dist\wx_hook_bridge.dll`

## 启动

先登录微信，再确认目标进程：

```powershell
python python\tools\inject_bridge.py --list
```

输出中的 `version` 必须是 `4.1.9.23`，注入器默认拒绝其他版本。

启动 SQLite 归档器：

```powershell
powershell -ExecutionPolicy Bypass -File examples\watch_messages.ps1
```

另开一个终端注入：

```powershell
python python\tools\inject_bridge.py
```

如果当前微信进程已加载完整版桥，基础版会拒绝覆盖其 Hook。先退出并重新启动微信，再注入基础版。

## 发送文本

发给文件传输助手：

```powershell
powershell -ExecutionPolicy Bypass -File examples\send_text.ps1 `
  -To filehelper `
  -Content "hello from basic bridge"
```

也可以直接使用 Python：

```powershell
python python\tools\wx_basic_bridge.py send filehelper "hello"
```

底层协议与完整版兼容：

```text
<seq>\t1\t<to_wxid>\t<content>
```

命令文件：`bridge\outbox\next.txt`

结果文件：`bridge\control\result_<seq>.json`

## 接收消息

原生 DLL 将每条消息写为：

```text
bridge\inbox\<msgId>_<localId>_<timestamp>_<eventKey>.json
```

JSON 包含会话、发送者、方向、消息类型、正文和原始端点等字段。

SQLite 归档器默认写入：

```text
bridge\messages.sqlite3
```

一次性导入：

```powershell
python python\tools\wx_basic_bridge.py ingest
```

查看最近消息：

```powershell
python python\tools\wx_basic_bridge.py show --limit 20
```

SQLite 主表为 `messages`，已按事件键去重；`inbox_files` 记录已处理文件。

## 停止

在注入终端按 `Ctrl+C`，或创建停止文件：

```powershell
Set-Content bridge\control\stop.txt stop -Encoding ascii
```

重新启动时，注入器会自动删除旧的 `stop.txt`。

## 验证

```powershell
python -m unittest discover -s tests -v
powershell -ExecutionPolicy Bypass -File native\wx_hook_bridge\build.ps1 -SyntaxOnly
```

## 参考文章

[零基础实现微信自动发消息：从 0 到发出第一条文本消息](https://bk.47claude.com/posts/wechat-4-1-9-23-send-message-beginner-guide/#%E9%9B%B6%E5%9F%BA%E7%A1%80%E5%AE%9E%E7%8E%B0%E5%BE%AE%E4%BF%A1%E8%87%AA%E5%8A%A8%E5%8F%91%E6%B6%88%E6%81%AF%E4%BB%8E-0-%E5%88%B0%E5%8F%91%E5%87%BA%E7%AC%AC%E4%B8%80%E6%9D%A1%E6%96%87%E6%9C%AC%E6%B6%88%E6%81%AF)
