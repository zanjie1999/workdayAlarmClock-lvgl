# LVGL 外壳 ↔ Go 播放器 协议

外壳（本仓库）拉起 Go 子进程，通过 **stdin 下命令**、**stdout 读事件**，
另有 HTTP 后台（端口见 `conf` 里的 `port`，默认 8080）与外壳无关。

## 1. 启动

```
workdayAlarmClock lvgl [播放器名]
```

| argv | 含义 |
|---|---|
| `workdayAlarmClock app [版本号]` | 被 Android App 以 lib 形式拉起，播放委派给宿主 |
| `workdayAlarmClock lvgl [播放器名]` | 被 LVGL 外壳拉起：本地播放 + 额外上报进度/音量 |
| `workdayAlarmClock <播放器名>` | 老行为，纯本地播放，不上报进度 |

- `播放器名` 是外部播放器可执行文件（`aplay` / `tinyplay` / `mpv` …）。
  **不传**时由 Go 自己探测（`alsa` 可用则用内置 ALSA，否则 `aplay` / `tinyplay`）。
- 外壳用环境变量控制：
  - `WAC_GO_BIN`：Go 可执行文件路径，默认 `./workdayAlarmClock`
  - `WAC_GO_PLAYER`：播放器名，默认不传（让 Go 自己探测）
- Go 退出后外壳会在 1 秒后重拉（Go 自更新时会 exit）。

## 2. stdout：事件（一行一条，UTF-8，≤480 字节）

外壳按 **前缀 + 空格** 匹配，不认识的整行丢弃。`lvgl` 模式额外提供 `POS / DUR / VOL`。

| 行 | 含义 | 说明 |
|---|---|---|
| `ECHO <文本>` | 曲名 / 提示 | 外壳显示在标题与迷你条；**停止播放时时钟页底部也会显示 10 秒** |
| `PLAY <url>` | 开始播放 | 外壳把位置归零 |
| `SONGID <id>` | 曲目 id | |
| `STOP` | 停止 | |
| `PAUSE` | 暂停 | |
| `RESUME` | 继续 | |
| **`DUR <毫秒>`** | **本曲总时长** | `0` = 未知（流式解析不出来）。**单位是毫秒** |
| **`POS <毫秒>`** | **当前位置** | `lvgl` 模式每秒一条；暂停/切歌由它兜住 |
| `VOL <0-100>` | 音量 | 音量变化时 |
| `VOLP` / `VOLM` | 音量 ±1 | 老指令，保留 |
| `ALARM` / `ALARMSTOP` | 闹铃开始 / 结束 | 响铃期间外壳强制停在时钟页，任意键 / 任意触摸都等于停止 |
| `ALARM <文本>` | 闹铃 + 文字 | 文字部分已不使用（屏幕上的提示改走 `WEATHERAL`），只当作响铃 |
| `ALARMON` / `ALARMOFF` | 有/无启用的闹钟 | |
| `WEATHER <文本>` | 天气 | 顶部状态行那一条 |
| **`WEATHERAL <文本>`** | **天气预警** | 时钟页底部一直显示，直到收到一个**不带内容的** `WEATHERAL` |
| **`FBSTART` / `FBEND`** | **屏幕接管开始 / 结束** | `/fb` 投屏长连接期间；外壳据此暂停 / 恢复整屏输出 |
| `WAKELOCK` / `HASUPDATE` / `SCREENON` / `SCREENOFF` / `RESTART` | 杂项 | |

要求：

- **stdout 只允许出现事件行**。日志走 `log`（stderr）；`gin` 的路由表/访问日志
  在 `lvgl` 模式下必须关掉（外壳已让 Go 设置 `gin.DefaultWriter = io.Discard`）。
- 前缀后面**必须带空格**（`ECHO `、`VOL `、`DUR `、`POS `），`ECHO:xxx` 这种不认。
- 并发写多行会交错排列，但单行不会撕裂；外壳按 `\n` 切。

时序（一次切歌）：

```
DUR 0          ← 新曲，暂时不知道总长，外壳切成不确定态
POS 0
DUR 213000     ← 解析出 mp3 头后补发（Xing/Info 帧数 → CBR 估算）
POS 1000
POS 2000  ...
```

### 时钟页底部提示行

**只要不是在播放/暂停**（底部那一条空着），下面两件事的文字就显示在时钟页最下面：

| 事件 | 显示多久 |
|---|---|
| `ECHO <文本>` | 10 秒后自动消失 |
| `WEATHERAL <文本>` | 一直显示，直到收到一个**不带内容的** `WEATHERAL` |

要点：

- 播放/暂停时不显示 —— 那条位置是迷你播放条，两者互斥。
- 长文本用跑马灯（`LV_LABEL_LONG_SCROLL_CIRCULAR`）兜住，不会截断。
- `WEATHERAL` **不会**写进顶部天气行：预警文字比天气长得多，会把那一行挤爆。
  Go 每次刷天气都会发一条，没有预警时发的是空串（`WEATHERAL `），正好当作"清除"。
- 提示行出现时，时间块会自动往上收一点，不跟它叠在一起。
- 时钟页在音量调节模式下，这条位置让给音量条，提示行暂时不画。

### 响铃：ALARM → ALARMSTOP

- 收到 `ALARM` 立刻回时钟页（不管刚才在哪一页）。
- 到 `ALARMSTOP` 之前，**任意键**（短按、长按都算）和**屏幕任意位置触摸**都等于
  "停止"：外壳向 Go 发 `stop`，并在本地立刻解除响铃态，不等回执。
- 停止后停在时钟页。`ALARMSTOP` 单独到达时只收尾，不跳页（避免把人从设置页拽走）。

### 屏幕接管：FBSTART / FBEND

Go 的 `POST/PUT /fb` 是 **MJPEG 长连接透传**（`display/fb_linux.go` 的 `fbStream`），
会话期间一直持有 `/dev/fb0`，把相机/网页推来的帧直接画上屏。这期间 LVGL 要是
继续刷，两边就会互相覆盖、闪成一片。

所以投屏会话一建立就发一行 `FBSTART`，会话结束（含被新流顶掉后旧流关闭）发 `FBEND`：

| 行 | 外壳的动作 |
|---|---|
| `FBSTART` | `lv_timer_enable(false)` —— 所有定时器停掉，一帧都不再写 fb0，屏幕完全交给 Go |
| `FBEND` | `lv_timer_enable(true)` + 整屏 invalidate + `lv_refr_now()`，立刻把 Go 停留的最后一帧盖掉 |

约束：

- **只在 `argv[1] == "lvgl"` 时发**。Go 独立运行时 stdout 是终端，不能污染。
- 外壳读这条日志的路由**不能挂在 `lv_timer` 上**：UI 停摆后 timer 全停，
  `FBEND` 就永远读不到。`wac_go_poll()` 由主循环每轮调用。
- 兜底：Go 被 `kill -9` 或投屏中途崩了发不出 `FBEND`，外壳在回收子进程时会自己恢复 UI。
- 重复 `FBSTART` 幂等；被新流顶掉的旧会话不会发 `FBEND`（否则画面会闪回 LVGL 一瞬）。

## 3. stdin：命令（一行一条，小写）
| 命令 | 含义 |
|---|---|
| `play` | 停止态时一键播放；播放中=暂停；暂停中=继续 |
| `pause` / `resume` | 暂停 / 继续 |
| `stop` | 停止 |
| `next` / `prev` | 下一首 / 上一首 |
| `vol <0-100>` | 设置音量（绝对值） |
| `1key` | 一键听歌（默认歌单） |
| `play <url>` / `playlist <id>` / `playmusic <id>` | 指定播放 |
| `pause0` / `pause1` | 只改暂停状态、不发事件（历史指令，留给 App） |

**重要**：Go 的 stdin 循环在处理完一条命令后，如果输入缓冲里还有数据会
**整批丢弃**（防连按堆积）。所以：

- 外壳必须做乐观更新，不能等回执才改 UI；
- **不要连发**：音量滑块只在松手时发一条 `vol`。

## 4. 进度是怎么来的（Go 侧）

1. 优先用「已写入声卡的 PCM 字节数」：`aplay` 管道写 / 直写 ALSA 两处都计数，
   换算 `位置 = 字节数 / 4 / 采样率`。暂停时写入停止，所以位置天然不涨。
2. 外部播放器路径（看不到子进程内部）退回 `本曲开始时间 - 累计暂停`。
3. 总时长由 `player/mp3dur.go` 解析 mp3 头：Xing/Info 帧数 → VBRI → CBR 估算；
   网络流同理（`bufio.Peek` 前 64KB，不额外发请求）。

## 5. 外壳侧对应实现

| 环节 | 文件 |
|---|---|
| 拉起子进程 / 行解析 / 重启 | `src/proto/go_proc.c` |
| 事件解析 | `src/proto/event_parse.c` |
| 位置插值（POS 之间） | `src/state/app_state.c` `wac_state_tick()` |
| 播放页显示 | `src/ui/page_player.c` |
| 模拟后端（无 Go 时） | `src/proto/mock_go.c` |
