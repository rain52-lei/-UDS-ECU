#include "remote_control.h"
#include "app_config.h"
#include <string.h>

void remote_control_init(remote_control_t *control)
{
    memset(control, 0, sizeof(*control));
    control->target_dC = 450;
}

void remote_control_set_mode(remote_control_t *control, bool remote)
{
    /* 切换模式后必须收到一条新命令，不能恢复旧的启用请求。 */
    control->remote = remote;
    control->seen = false;
    control->enable = false;
    control->inhibit = false;
}

bool remote_control_accept(remote_control_t *control,
                           const can_command_t *command, uint32_t now)
{
    uint8_t delta;
    if (command == NULL || command->target_dC < TG_TARGET_MIN_DC ||
        command->target_dC > TG_TARGET_MAX_DC) {
        ++control->rejected;
        return false;
    }
    if (control->seen && now - control->last_ms < TG_COMMAND_TIMEOUT_MS) {
        delta = (uint8_t)(command->counter - control->counter);
        /* 模256计数：重复或向后/歧义序号均不刷新通信超时。 */
        if (delta == 0U || delta >= 128U) {
            ++control->rejected;
            return false;
        }
        control->missing += (uint32_t)delta - 1U;
    }
    control->seen = true;
    control->counter = command->counter;
    control->last_ms = now;
    control->target_dC = command->target_dC;
    control->enable = command->enable_request;
    if (!command->enable_request) {
        control->inhibit = false;
    }
    ++control->accepted;
    return true;
}

bool remote_control_timed_out(const remote_control_t *control, uint32_t now)
{
    return control->remote && (!control->seen ||
           now - control->last_ms >= TG_COMMAND_TIMEOUT_MS);
}

bool remote_control_enabled(const remote_control_t *control, uint32_t now)
{
    return control->remote && !remote_control_timed_out(control, now) &&
           control->enable && !control->inhibit;
}

void remote_control_stop(remote_control_t *control)
{
    /* 本地停机优先；上位机必须先发送停用帧，再发送启用帧才能解除。 */
    control->inhibit = true;
}
