/* WODLE-PORT: task-notification emulation for the FreeRTOS façade. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SHIM_MAX_TASKS 4
#define SHIM_TASK_PRIO 20 /* below RT main thread (15): render work yields to input */

struct ShimTask
{
    rt_thread_t thread;
    rt_sem_t notify;
};
static ShimTask s_tasks[SHIM_MAX_TASKS];

static rt_sem_t notifySemFor(rt_thread_t t)
{
    for (auto &slot : s_tasks)
        if (slot.thread == t) return slot.notify;
    return RT_NULL;
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stackDepth,
                       void *param, UBaseType_t prio, TaskHandle_t *handle)
{
    (void)prio;
    /* CrossPoint is written against ESP-IDF, whose xTaskCreate stack arg is
     * BYTES (not vanilla-FreeRTOS words). x2 = blind-phase headroom; a render-
     * thread stack overflow would be a brutal HIL debug session. */
    rt_thread_t t = rt_thread_create(name ? name : "cptask", fn, param,
                                     stackDepth * 2, SHIM_TASK_PRIO, 10);
    if (!t) return pdFAIL;

    for (auto &slot : s_tasks)
    {
        if (slot.thread == RT_NULL)
        {
            slot.thread = t;
            slot.notify = rt_sem_create("cpntf", 0, RT_IPC_FLAG_PRIO);
            break;
        }
    }
    if (handle) *handle = t;
    rt_thread_startup(t);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    rt_thread_t t = task ? task : rt_thread_self();
    for (auto &slot : s_tasks)
    {
        if (slot.thread == t)
        {
            if (slot.notify) rt_sem_delete(slot.notify);
            slot = {};
            break;
        }
    }
    rt_thread_delete(t);
}

BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action)
{
    (void)value;
    (void)action;
    rt_sem_t sem = notifySemFor(task);
    if (!sem) return pdFAIL;
    rt_sem_release(sem);
    return pdPASS;
}

BaseType_t xTaskNotifyGive(TaskHandle_t task) { return xTaskNotify(task, 0, eIncrement); }

uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks)
{
    rt_sem_t sem = notifySemFor(rt_thread_self());
    if (!sem) return 0;
    if (rt_sem_take(sem, (rt_int32_t)ticks) != RT_EOK) return 0;
    if (clearOnExit)
        while (rt_sem_trytake(sem) == RT_EOK) {}
    return 1;
}

BaseType_t xTaskNotifyWait(uint32_t, uint32_t, uint32_t *value, TickType_t ticks)
{
    if (value) *value = 0;
    return ulTaskNotifyTake(pdTRUE, ticks) ? pdTRUE : pdFALSE;
}
