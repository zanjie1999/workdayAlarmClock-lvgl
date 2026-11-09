/**
 * go_proc.c -- 拉起 Go 播放器子进程（真机）
 *
 * 约定（见 docs/protocol.md）：
 *   workdayAlarmClock lvgl [播放器名]
 * 我们永远是 argv[1]="lvgl" 的调用方：Go 因此知道 stdout 有人在按行读，
 * 会额外打 POS/DUR/VOL；播放器名留空则由 Go 自己探测（alsa/aplay/tinyplay）。
 *
 * 三条约束：
 *   - stdout/stdin 都必须非阻塞，主循环 20ms 轮询，不能卡住 LVGL；
 *   - 子进程死了要重拉（Go 自己更新时会 exit），退避 1s；
 *   - 必须忽略 SIGPIPE，否则往已经死掉的子进程写命令会把自己干掉。
 */
#include "wac.h"
#include "event_parse.h"
#include "ui_pause.h"

#ifndef WAC_SIM

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define GO_LINE_MAX      512
#define GO_RESTART_DELAY 1000

static pid_t    s_pid = -1;
static int      s_out = -1;
static int      s_in  = -1;
static char     s_line[GO_LINE_MAX];
static size_t   s_len;
static uint32_t s_restart_at;

static void close_pipes(void)
{
    if (s_out >= 0) {
        close(s_out);
        s_out = -1;
    }
    if (s_in >= 0) {
        close(s_in);
        s_in = -1;
    }
}

static void start_child(void)
{
    const char *bin    = getenv("WAC_GO_BIN");
    const char *player = getenv("WAC_GO_PLAYER");
    if (!bin || !*bin) bin = "./workdayAlarmClock";

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) < 0) {
        WAC_LOG("go: pipe failed (%d)", errno);
        return;
    }
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        WAC_LOG("go: pipe failed (%d)", errno);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        WAC_LOG("go: fork failed (%d)", errno);
        return;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);

        if (player && *player) {
            execl(bin, bin, "lvgl", player, (char *)NULL);
        }
        else {
            execl(bin, bin, "lvgl", (char *)NULL);
        }
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);
    s_in  = in_pipe[1];
    s_out = out_pipe[0];
    fcntl(s_in, F_SETFL, fcntl(s_in, F_GETFL) | O_NONBLOCK);
    fcntl(s_out, F_SETFL, fcntl(s_out, F_GETFL) | O_NONBLOCK);

    s_pid            = pid;
    s_len            = 0;
    g_state.go_pid   = pid;
    g_state.go_alive = true;

    WAC_LOG("go: started pid=%d bin=%s player=%s", (int)pid, bin,
            (player && *player) ? player : "(auto)");
}

void wac_go_write(const char *line)
{
    if (s_in < 0 || !line) return;

    char buf[GO_LINE_MAX];
    int  n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n <= 0) return;

    ssize_t w = write(s_in, buf, (size_t)n);
    if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        WAC_LOG("go: write failed (%d)", errno);
    }
}

/* 按 \n 切行喂给解析器；超长行直接丢掉（Go 那边承诺 ≤480B） */
static void feed_line(const char *s)
{
    event_handle_line(s);
}

static void pump_stdout(void)
{
    if (s_out < 0) return;

    char    chunk[256];
    ssize_t n;
    while ((n = read(s_out, chunk, sizeof(chunk))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char c = chunk[i];
            if (c == '\n') {
                s_line[s_len] = '\0';
                if (s_len > 0) feed_line(s_line);
                s_len = 0;
                continue;
            }
            if (c == '\r') continue;
            if (s_len < sizeof(s_line) - 1) {
                s_line[s_len++] = c;
            }
            else {
                s_len = 0; /* 超长：丢掉这半行，等下一个换行重新开始 */
            }
        }
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        WAC_LOG("go: read failed (%d)", errno);
    }
}

static void check_child(void)
{
    if (s_pid <= 0) return;

    int   st  = 0;
    pid_t pid = waitpid(s_pid, &st, WNOHANG);
    if (pid != s_pid) return;

    if (WIFEXITED(st)) {
        WAC_LOG("go: exited code=%d", WEXITSTATUS(st));
    }
    else if (WIFSIGNALED(st)) {
        WAC_LOG("go: killed by signal %d", WTERMSIG(st));
    }

    close_pipes();
    s_pid            = -1;
    g_state.go_alive = false;
    g_state.go_pid   = 0;
    s_restart_at     = wac_millis() + GO_RESTART_DELAY;

    /* 兜底：Go 要是被 kill -9 或者投屏中途崩了，FBEND 就发不出来了，
       屏幕会永远停在它画的最后一帧。子进程一没就把 UI 收回来。 */
    if (ui_pause_active()) {
        WAC_LOG("go: gone while paused -> take the screen back");
        ui_pause_set(false);
    }
}

/* 由主循环每轮调用一次，**不是** LVGL 定时器。
 *
 * 这一点很关键：Go 发 FBSTART 之后外壳会 lv_timer_enable(false) 把整个 UI 停摆，
 * 如果读管道还挂在 lv_timer 上，FBEND 就永远读不到、画面再也回不来。
 * I/O 本来也不该依赖 UI 定时器。 */
void wac_go_poll(void)
{
    pump_stdout();
    check_child();

    if (s_pid <= 0 && s_restart_at && wac_millis() >= s_restart_at) {
        s_restart_at = 0;
        WAC_LOG("go: restarting");
        start_child();
    }
}

void go_proc_init(void)
{
    /* 子进程死掉后往管道写命令会收到 SIGPIPE，默认动作是杀进程 */
    signal(SIGPIPE, SIG_IGN);

    start_child();
}

#endif /* !WAC_SIM */
