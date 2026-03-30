/**
 * @file module_manager.c
 * @brief 模块管理器实现 (Module Manager Implementation)
 *
 * 提供模块的动态注册、生命周期管理和通信功能。
 *
 * 主要功能：
 * - 模块注册表管理（最多 CONFIG_MAX_MODULES 个模块）
 * - 模块生命周期控制（初始化、启动、停止、关闭）
 * - 模块事件订阅和分发
 * - 模块统计信息收集
 * - 模块状态回调通知
 *
 * 线程安全：
 * - 所有公共 API 都是线程安全的
 * - 使用互斥锁保护内部管理结构
 *
 * @copyright Copyright (c) 2026
 * @license SPDX-License-Identifier: Apache-2.0
 */

#include "module_manager.h"
#include "event_system.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(module_manager, CONFIG_SYS_LOG_LEVEL);

/* =============================================================================
 * 内部数据结构 (Internal Data Structures)
 * ============================================================================= */

/**
 * @brief 模块管理器控制块
 * 
 * 包含模块管理器的所有内部状态和数据。
 */
typedef struct {
	module_info_t modules[CONFIG_MAX_MODULES];  /**< 模块信息数组 */
	uint32_t module_count;                       /**< 已注册模块数量 */
	uint32_t next_module_id;                     /**< 下一个可用的模块 ID */
	module_mgr_stats_t stats;                    /**< 统计信息 */
	module_mgr_callback_t callback;              /**< 状态变化回调 */
	void *callback_user_data;                    /**< 回调用户数据 */
	struct k_mutex lock;                         /**< 保护内部数据的互斥锁 */
	bool initialized;                            /**< 管理器是否已初始化 */
	bool running;                                /**< 管理器是否正在运行 */
} module_manager_cb_t;

/**
 * @brief 启动/停止排序条目
 *
 * 用于按优先级或依赖关系排序。
 */
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
typedef struct {
	uint32_t id;
	module_priority_t priority;
	const char *const *depends_on;
} start_order_entry_t;
#else
typedef struct {
	uint32_t id;
	module_priority_t priority;
} start_order_entry_t;
#endif

/* =============================================================================
 * 静态变量 (Static Variables)
 * ============================================================================= */

/** 全局模块管理器控制块实例 */
static module_manager_cb_t g_module_mgr;

/* =============================================================================
 * 前置声明 (Forward Declarations)
 * ============================================================================= */

/**
 * @brief 按 ID 查找模块（内部使用，需持有锁）
 */
static module_info_t *find_module_by_id_locked(uint32_t module_id);

/**
 * @brief 清空模块槽位（内部使用，需持有锁）
 */
static void clear_module_slot_unlocked(module_info_t *info);

/**
 * @brief 清除模块的所有事件订阅（内部使用，需持有锁）
 */
static void module_event_clear_all_unlocked(module_info_t *info);

/**
 * @brief 模块事件处理函数（内部使用）
 */
static void module_event_handler(const event_t *event, void *user_data);

/**
 * @brief 通知回调（内部使用，需持有锁）
 */
static void notify_callback_unlocked(uint32_t module_id, module_mgr_event_t evt);

/* =============================================================================
 * 内部辅助函数 (Internal Helpers)
 * 注意：除非特别说明，这些函数需要持有锁
 * ============================================================================= */

/**
 * @brief 按 ID 查找模块
 * 
 * @param module_id 模块 ID
 * @return 指向模块信息的指针，未找到返回 NULL
 * 
 * @note 必须持有 g_module_mgr.lock
 */
static module_info_t *find_module_by_id_locked(uint32_t module_id)
{
	if (module_id == 0U) {
		return NULL;
	}

	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].id == module_id) {
			return &g_module_mgr.modules[i];
		}
	}

	return NULL;
}

/**
 * @brief 按名称查找模块 ID（需已持有 g_module_mgr.lock）
 */
static uint32_t find_module_id_by_name_locked(const char *name)
{
	if (name == NULL) {
		return 0U;
	}

	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].interface != NULL &&
		    g_module_mgr.modules[i].interface->name != NULL &&
		    strcmp(g_module_mgr.modules[i].interface->name, name) == 0) {
			return g_module_mgr.modules[i].id;
		}
	}

	return 0U;
}

/**
 * @brief 清空模块槽位
 * 
 * 将模块信息重置为未初始化状态。
 * 
 * @param info 模块信息指针
 * 
 * @note 必须持有 g_module_mgr.lock
 */
static void clear_module_slot_unlocked(module_info_t *info)
{
	if (info == NULL) {
		return;
	}

	info->interface = NULL;
	info->config = NULL;
	info->internal_data = NULL;
	info->status = MODULE_STATUS_UNINITIALIZED;
	info->id = 0U;
	info->event_subscription_count = 0U;
	(void)memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));
}

/**
 * @brief 清除模块的所有事件订阅
 * 
 * @param info 模块信息指针
 * 
 * @note 必须持有 g_module_mgr.lock
 */
static void module_event_clear_all_unlocked(module_info_t *info)
{
	if (info == NULL) {
		return;
	}

	for (uint8_t i = 0; i < info->event_subscription_count; i++) {
		(void)event_unsubscribe(info->event_subscriptions[i].type,
				       info->event_subscriptions[i].subscriber_id);
	}

	info->event_subscription_count = 0U;
	(void)memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));
}

/**
 * @brief 通知状态变化回调
 * 
 * @param module_id 模块 ID
 * @param evt 事件类型
 * 
 * @note 必须持有 g_module_mgr.lock
 */
static void notify_callback_unlocked(uint32_t module_id, module_mgr_event_t evt)
{
	module_mgr_callback_t cb;
	void *ud;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	cb = g_module_mgr.callback;
	ud = g_module_mgr.callback_user_data;
	k_mutex_unlock(&g_module_mgr.lock);

	if (cb != NULL) {
		cb(module_id, evt, ud);
	}
}

/**
 * @brief 查找事件订阅索引
 * 
 * @param info 模块信息
 * @param type 事件类型
 * @return 索引值，未找到返回 -1
 * 
 * @note 不需要持有锁（只读操作）
 */
static int find_event_sub_index(const module_info_t *info, event_type_t type)
{
	for (uint8_t i = 0; i < info->event_subscription_count; i++) {
		if (info->event_subscriptions[i].type == type) {
			return (int)i;
		}
	}
	return -1;
}

/**
 * @brief 按优先级排序启动条目（冒泡排序）
 * 
 * @param entries 启动条目数组
 * @param n 数组长度
 * 
 * @note 优先级数值小的排在前面（先启动）
 */
static void sort_start_entries(start_order_entry_t *entries, int n)
{
	for (int i = 0; i < n - 1; i++) {
		for (int j = i + 1; j < n; j++) {
			if ((int)entries[j].priority < (int)entries[i].priority) {
				start_order_entry_t t = entries[i];

				entries[i] = entries[j];
				entries[j] = t;
			}
		}
	}
}

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)

/**
 * @brief 按 priority 降序（停止时与启动优先级顺序相反）
 */
static void sort_stop_entries_reverse_priority(start_order_entry_t *entries, int n)
{
	for (int i = 0; i < n - 1; i++) {
		for (int j = i + 1; j < n; j++) {
			if ((int)entries[j].priority > (int)entries[i].priority) {
				start_order_entry_t t = entries[i];

				entries[i] = entries[j];
				entries[j] = t;
			}
		}
	}
}

/**
 * @brief 按依赖拓扑排序启动批次；无效项剔除，成环时退回仅 priority
 */
static int dependency_order_start_batch(start_order_entry_t *entries, int n)
{
	bool valid[CONFIG_MAX_MODULES];
	bool adj[CONFIG_MAX_MODULES][CONFIG_MAX_MODULES];
	int indegree[CONFIG_MAX_MODULES];
	uint32_t pick_order[CONFIG_MAX_MODULES];
	int n2;

	if (n <= 1) {
		return n;
	}

	for (int i = 0; i < n; i++) {
		valid[i] = true;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	for (int i = 0; i < n; i++) {
		if (entries[i].depends_on == NULL) {
			continue;
		}
		for (const char *const *p = entries[i].depends_on; *p != NULL; p++) {
			const uint32_t did = find_module_id_by_name_locked(*p);

			if (did == 0U) {
				LOG_ERR("Module id=%u: unknown dependency '%s'",
					(unsigned int)entries[i].id, *p);
				valid[i] = false;
				break;
			}
			if (did == entries[i].id) {
				LOG_ERR("Module id=%u: self dependency '%s'",
					(unsigned int)entries[i].id, *p);
				valid[i] = false;
				break;
			}
			module_info_t *dep = find_module_by_id_locked(did);

			if (dep == NULL) {
				valid[i] = false;
				break;
			}
			if (dep->status == MODULE_STATUS_RUNNING) {
				continue;
			}
			int found = -1;

			for (int k = 0; k < n; k++) {
				if (entries[k].id == did) {
					found = k;
					break;
				}
			}
			if (found < 0) {
				LOG_ERR("Module id=%u: dependency '%s' not runnable in this batch",
					(unsigned int)entries[i].id, *p);
				valid[i] = false;
				break;
			}
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	n2 = 0;
	for (int i = 0; i < n; i++) {
		if (valid[i]) {
			if (n2 != i) {
				entries[n2] = entries[i];
			}
			n2++;
		}
	}
	if (n2 <= 1) {
		return n2;
	}

	(void)memset(adj, 0, sizeof(adj));
	(void)memset(indegree, 0, sizeof(indegree));

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	for (int i = 0; i < n2; i++) {
		if (entries[i].depends_on == NULL) {
			continue;
		}
		for (const char *const *p = entries[i].depends_on; *p != NULL; p++) {
			const uint32_t did = find_module_id_by_name_locked(*p);
			module_info_t *const dep = find_module_by_id_locked(did);

			if (dep != NULL && dep->status == MODULE_STATUS_RUNNING) {
				continue;
			}
			int j = -1;

			for (int k = 0; k < n2; k++) {
				if (entries[k].id == did) {
					j = k;
					break;
				}
			}
			if (j < 0) {
				continue;
			}
			if (!adj[j][i]) {
				adj[j][i] = true;
				indegree[i]++;
			}
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	bool remaining[CONFIG_MAX_MODULES];

	for (int i = 0; i < n2; i++) {
		remaining[i] = true;
	}

	for (int out = 0; out < n2; out++) {
		int best = -1;

		for (int i = 0; i < n2; i++) {
			if (!remaining[i] || indegree[i] != 0) {
				continue;
			}
			if (best < 0 || (int)entries[i].priority < (int)entries[best].priority) {
				best = i;
			}
		}
		if (best < 0) {
			LOG_ERR("Dependency cycle; using priority order for start");
			sort_start_entries(entries, n2);
			return n2;
		}
		pick_order[out] = (uint32_t)best;
		remaining[best] = false;
		for (int i = 0; i < n2; i++) {
			if (adj[best][i]) {
				indegree[i]--;
			}
		}
	}

	start_order_entry_t tmp[CONFIG_MAX_MODULES];

	for (int i = 0; i < n2; i++) {
		tmp[i] = entries[(int)pick_order[i]];
	}
	(void)memcpy(entries, tmp, sizeof(entries[0]) * (size_t)n2);
	return n2;
}

/**
 * @brief 停止顺序：依赖拓扑的逆序；成环时退回 priority 降序
 */
static int dependency_order_stop_batch(start_order_entry_t *entries, int n)
{
	bool adj[CONFIG_MAX_MODULES][CONFIG_MAX_MODULES];
	int indegree[CONFIG_MAX_MODULES];
	uint32_t pick_order[CONFIG_MAX_MODULES];

	if (n <= 1) {
		return n;
	}

	(void)memset(adj, 0, sizeof(adj));
	(void)memset(indegree, 0, sizeof(indegree));

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	for (int i = 0; i < n; i++) {
		if (entries[i].depends_on == NULL) {
			continue;
		}
		for (const char *const *p = entries[i].depends_on; *p != NULL; p++) {
			const uint32_t did = find_module_id_by_name_locked(*p);
			module_info_t *const dep = find_module_by_id_locked(did);

			if (dep == NULL || dep->status != MODULE_STATUS_RUNNING) {
				continue;
			}
			int j = -1;

			for (int k = 0; k < n; k++) {
				if (entries[k].id == did) {
					j = k;
					break;
				}
			}
			if (j < 0) {
				continue;
			}
			if (!adj[j][i]) {
				adj[j][i] = true;
				indegree[i]++;
			}
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	bool remaining[CONFIG_MAX_MODULES];

	for (int i = 0; i < n; i++) {
		remaining[i] = true;
	}

	for (int out = 0; out < n; out++) {
		int best = -1;

		for (int i = 0; i < n; i++) {
			if (!remaining[i] || indegree[i] != 0) {
				continue;
			}
			if (best < 0 || (int)entries[i].priority < (int)entries[best].priority) {
				best = i;
			}
		}
		if (best < 0) {
			LOG_ERR("Dependency cycle; using reverse priority order for stop");
			sort_stop_entries_reverse_priority(entries, n);
			return n;
		}
		pick_order[out] = (uint32_t)best;
		remaining[best] = false;
		for (int i = 0; i < n; i++) {
			if (adj[best][i]) {
				indegree[i]--;
			}
		}
	}

	start_order_entry_t tmp[CONFIG_MAX_MODULES];

	for (int i = 0; i < n; i++) {
		tmp[i] = entries[(int)pick_order[i]];
	}
	for (int i = 0; i < n / 2; i++) {
		const int j = n - 1 - i;
		start_order_entry_t t = tmp[i];

		tmp[i] = tmp[j];
		tmp[j] = t;
	}
	(void)memcpy(entries, tmp, sizeof(entries[0]) * (size_t)n);
	return n;
}
#endif /* CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES */

/* =============================================================================
 * 核心 API 实现 (Core API Implementation)
 * ============================================================================= */

/**
 * @brief 初始化模块管理器
 * 
 * @return 0 成功，-1 失败
 */
int module_manager_init(void)
{
	LOG_INF("Initializing module manager...");

	(void)memset(&g_module_mgr, 0, sizeof(g_module_mgr));
	k_mutex_init(&g_module_mgr.lock);

	/* 初始化所有模块槽位 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		g_module_mgr.modules[i].status = MODULE_STATUS_UNINITIALIZED;
		g_module_mgr.modules[i].id = 0U;
	}

	g_module_mgr.next_module_id = 1U;
	g_module_mgr.initialized = true;

	LOG_INF("Module manager initialized");
	return 0;
}

/**
 * @brief 启动模块管理器
 * 
 * @return 0 成功，-1 未初始化
 */
int module_manager_start(void)
{
	if (!g_module_mgr.initialized) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	g_module_mgr.running = true;
	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module manager started");
	return 0;
}

/**
 * @brief 停止模块管理器
 * 
 * @return 0 成功，-1 未初始化
 */
int module_manager_stop(void)
{
	if (!g_module_mgr.initialized) {
		return -1;
	}

	(void)module_manager_stop_all();

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	g_module_mgr.running = false;
	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module manager stopped");
	return 0;
}

/**
 * @brief 关闭模块管理器
 * 
 * 停止所有模块，调用 shutdown 回调，清空注册表。
 * 
 * @return 0 成功，-1 失败
 */
int module_manager_shutdown(void)
{
	int (*shutdown_fn[CONFIG_MAX_MODULES])(void);
	bool need_shutdown[CONFIG_MAX_MODULES];

	LOG_INF("Shutting down module manager...");

	(void)module_manager_stop();

	(void)memset(need_shutdown, 0, sizeof(need_shutdown));
	(void)memset(shutdown_fn, 0, sizeof(shutdown_fn));

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		module_info_t *info = &g_module_mgr.modules[i];

		/* 清除所有事件订阅 */
		module_event_clear_all_unlocked(info);

		/* 收集需要 shutdown 的模块 */
		if (info->status != MODULE_STATUS_UNINITIALIZED && info->interface != NULL &&
		    info->interface->shutdown != NULL) {
			shutdown_fn[i] = info->interface->shutdown;
			need_shutdown[i] = true;
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	/* 在锁外调用 shutdown 函数 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (need_shutdown[i] && shutdown_fn[i] != NULL) {
			shutdown_fn[i]();
		}
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	/* 清空所有模块槽位 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		clear_module_slot_unlocked(&g_module_mgr.modules[i]);
	}

	g_module_mgr.module_count = 0U;
	(void)memset(&g_module_mgr.stats, 0, sizeof(g_module_mgr.stats));
	g_module_mgr.initialized = false;

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module manager shutdown complete");
	return 0;
}

/* =============================================================================
 * 模块注册 API 实现 (Module Registration API Implementation)
 * ============================================================================= */

/**
 * @brief 注册模块
 * 
 * @param interface 模块接口指针
 * @param config 模块配置数据
 * @param module_id 输出参数：分配的模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_register(const module_interface_t *interface, void *config,
			    uint32_t *module_id)
{
	if (!g_module_mgr.initialized || interface == NULL) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	/* 查找空闲槽位 */
	module_info_t *info = NULL;

	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].status == MODULE_STATUS_UNINITIALIZED) {
			info = &g_module_mgr.modules[i];
			break;
		}
	}

	if (info == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		LOG_ERR("Maximum module count reached");
		return -1;
	}

	/* 初始化模块信息 */
	info->interface = interface;
	info->config = config;
	info->internal_data = NULL;
	info->status = MODULE_STATUS_INITIALIZING;
	info->id = g_module_mgr.next_module_id++;
	info->event_subscription_count = 0U;
	(void)memset(info->event_subscriptions, 0, sizeof(info->event_subscriptions));

	if (module_id != NULL) {
		*module_id = info->id;
	}

	/* 调用模块初始化函数 */
	if (interface->init != NULL) {
		const uint32_t t0 = k_uptime_get_32();
		const int ret = interface->init(config);
		const uint32_t elapsed = k_uptime_get_32() - t0;

		if (ret != 0) {
			LOG_ERR("Module '%s' init failed: %d", interface->name, ret);
			g_module_mgr.next_module_id--;
			clear_module_slot_unlocked(info);
			g_module_mgr.stats.error_modules++;
			k_mutex_unlock(&g_module_mgr.lock);
			return ret;
		}

		/* 检查初始化超时 */
		if (CONFIG_MODULE_INIT_TIMEOUT_MS > 0 &&
		    elapsed > (uint32_t)CONFIG_MODULE_INIT_TIMEOUT_MS) {
			LOG_ERR("Module '%s' init exceeded timeout (%u ms)", interface->name,
				(unsigned int)elapsed);
			if (interface->shutdown != NULL) {
				interface->shutdown();
			}
			g_module_mgr.next_module_id--;
			clear_module_slot_unlocked(info);
			g_module_mgr.stats.error_modules++;
			k_mutex_unlock(&g_module_mgr.lock);
			return -1;
		}
	}

	info->status = MODULE_STATUS_INITIALIZED;
	g_module_mgr.module_count++;
	g_module_mgr.stats.total_modules++;

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module registered: %s (id=%d)", interface->name, info->id);
	notify_callback_unlocked(info->id, MODULE_MGR_EVENT_REGISTERED);
	return 0;
}

/**
 * @brief 注销模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_unregister(uint32_t module_id)
{
	if (!g_module_mgr.initialized || module_id == 0U) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	/* 如果模块正在运行，先停止 */
	if (info->status == MODULE_STATUS_RUNNING) {
		int (*stop_fn)(void) = NULL;

		if (info->interface != NULL) {
			stop_fn = info->interface->stop;
		}
		k_mutex_unlock(&g_module_mgr.lock);
		if (stop_fn != NULL) {
			stop_fn();
		}
		k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
		info = find_module_by_id_locked(module_id);
		if (info == NULL) {
			k_mutex_unlock(&g_module_mgr.lock);
			return -1;
		}
		if (info->status == MODULE_STATUS_RUNNING) {
			info->status = MODULE_STATUS_STOPPED;
			if (g_module_mgr.stats.active_modules > 0U) {
				g_module_mgr.stats.active_modules--;
			}
		}
	}

	/* 清除所有事件订阅 */
	module_event_clear_all_unlocked(info);

	/* 调用模块 shutdown 函数 */
	if (info->interface != NULL && info->interface->shutdown != NULL) {
		int (*sd)(void) = info->interface->shutdown;

		k_mutex_unlock(&g_module_mgr.lock);
		sd();
		k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
		info = find_module_by_id_locked(module_id);
		if (info == NULL) {
			k_mutex_unlock(&g_module_mgr.lock);
			return -1;
		}
	}

	/* 更新统计信息 */
	if (g_module_mgr.module_count > 0U) {
		g_module_mgr.module_count--;
	}
	if (g_module_mgr.stats.total_modules > 0U) {
		g_module_mgr.stats.total_modules--;
	}

	/* 清空模块槽位 */
	clear_module_slot_unlocked(info);

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module unregistered: id=%d", module_id);
	notify_callback_unlocked(module_id, MODULE_MGR_EVENT_UNREGISTERED);
	return 0;
}

/**
 * @brief 获取模块信息
 * 
 * @param module_id 模块 ID
 * @param out 输出结构指针
 * @return 0 成功，-1 失败
 */
int module_manager_get_module_info(uint32_t module_id, module_info_t *out)
{
	if (!g_module_mgr.initialized || module_id == 0U || out == NULL) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	*out = *info;
	k_mutex_unlock(&g_module_mgr.lock);
	return 0;
}

/**
 * @brief 按名称获取模块 ID
 * 
 * @param name 模块名称
 * @return 模块 ID，0 表示未找到
 */
uint32_t module_manager_get_id_by_name(const char *name)
{
	if (!g_module_mgr.initialized || name == NULL) {
		return 0U;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	const uint32_t id = find_module_id_by_name_locked(name);

	k_mutex_unlock(&g_module_mgr.lock);
	return id;
}

/**
 * @brief 遍历所有模块
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void module_manager_foreach(void (*callback)(module_info_t *, void *), void *user_data)
{
	if (!g_module_mgr.initialized || callback == NULL) {
		return;
	}

	module_info_t snapshot[CONFIG_MAX_MODULES];

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	int n = 0;

	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED) {
			snapshot[n++] = g_module_mgr.modules[i];
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	for (int i = 0; i < n; i++) {
		callback(&snapshot[i], user_data);
	}
}

/* =============================================================================
 * 模块生命周期 API 实现 (Module Lifecycle API Implementation)
 * ============================================================================= */

/**
 * @brief 启动指定模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_start_module(uint32_t module_id)
{
	int (*start_fn)(void);
	const char *name;
	int ret = 0;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (info->status != MODULE_STATUS_INITIALIZED &&
	    info->status != MODULE_STATUS_STOPPED) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	start_fn = info->interface->start;
	name = info->interface->name;

	k_mutex_unlock(&g_module_mgr.lock);

	/* 在锁外调用 start 函数 */
	if (start_fn != NULL) {
		ret = start_fn();
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	info = find_module_by_id_locked(module_id);
	if (info == NULL || info->interface == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (ret != 0) {
		LOG_ERR("Module '%s' start failed: %d", name != NULL ? name : "?", ret);
		info->status = MODULE_STATUS_ERROR;
		g_module_mgr.stats.error_modules++;
		k_mutex_unlock(&g_module_mgr.lock);
		notify_callback_unlocked(module_id, MODULE_MGR_EVENT_ERROR);
		return ret;
	}

	info->status = MODULE_STATUS_RUNNING;
	g_module_mgr.stats.active_modules++;

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module started: %s", name != NULL ? name : "?");
	notify_callback_unlocked(module_id, MODULE_MGR_EVENT_STARTED);
	return 0;
}

/**
 * @brief 停止指定模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_stop_module(uint32_t module_id)
{
	int (*stop_fn)(void);
	const char *name;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (info->status != MODULE_STATUS_RUNNING) {
		k_mutex_unlock(&g_module_mgr.lock);
		return 0;
	}

	stop_fn = info->interface->stop;
	name = info->interface->name;

	k_mutex_unlock(&g_module_mgr.lock);

	if (stop_fn != NULL) {
		stop_fn();
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	info = find_module_by_id_locked(module_id);
	if (info != NULL && info->status == MODULE_STATUS_RUNNING) {
		info->status = MODULE_STATUS_STOPPED;
		if (g_module_mgr.stats.active_modules > 0U) {
			g_module_mgr.stats.active_modules--;
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module stopped: %s", name != NULL ? name : "?");
	notify_callback_unlocked(module_id, MODULE_MGR_EVENT_STOPPED);
	return 0;
}

/**
 * @brief 启动所有模块
 * 
 * 按优先级顺序启动所有已注册的模块；若启用运行时依赖则先满足拓扑序。
 * 
 * @return 成功启动的模块数量
 */
int module_manager_start_all(void)
{
	start_order_entry_t entries[CONFIG_MAX_MODULES];
	int n = 0;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	/* 收集需要启动的模块 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		module_info_t *m = &g_module_mgr.modules[i];

		if ((m->status == MODULE_STATUS_INITIALIZED ||
		     m->status == MODULE_STATUS_STOPPED) &&
		    m->interface != NULL) {
			entries[n].id = m->id;
			entries[n].priority = m->interface->priority;
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
			entries[n].depends_on = m->interface->depends_on;
#endif
			n++;
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
	n = dependency_order_start_batch(entries, n);
#else
	sort_start_entries(entries, n);
#endif

	int started = 0;

	for (int i = 0; i < n; i++) {
		if (module_manager_start_module(entries[i].id) == 0) {
			started++;
		}
	}

	return started;
}

/**
 * @brief 停止所有模块
 * 
 * @return 成功停止的模块数量
 */
int module_manager_stop_all(void)
{
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
	start_order_entry_t entries[CONFIG_MAX_MODULES];
#else
	uint32_t ids[CONFIG_MAX_MODULES];
#endif
	int n = 0;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		module_info_t *const m = &g_module_mgr.modules[i];

		if (m->status == MODULE_STATUS_RUNNING && m->interface != NULL) {
			entries[n].id = m->id;
			entries[n].priority = m->interface->priority;
			entries[n].depends_on = m->interface->depends_on;
			n++;
		}
	}
#else
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].status == MODULE_STATUS_RUNNING) {
			ids[n++] = g_module_mgr.modules[i].id;
		}
	}
#endif

	k_mutex_unlock(&g_module_mgr.lock);

#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
	(void)dependency_order_stop_batch(entries, n);
#endif

	int stopped = 0;

	for (int i = 0; i < n; i++) {
#if IS_ENABLED(CONFIG_MODULE_MANAGER_RUNTIME_DEPENDENCIES)
		if (module_manager_stop_module(entries[i].id) == 0) {
#else
		if (module_manager_stop_module(ids[i]) == 0) {
#endif
			stopped++;
		}
	}

	return stopped;
}

/**
 * @brief 挂起模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_suspend_module(uint32_t module_id)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (info->status != MODULE_STATUS_RUNNING) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	info->status = MODULE_STATUS_SUSPENDED;
	const char *name = info->interface->name;

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module suspended: %s", name != NULL ? name : "?");
	return 0;
}

/**
 * @brief 恢复模块
 * 
 * @param module_id 模块 ID
 * @return 0 成功，-1 失败
 */
int module_manager_resume_module(uint32_t module_id)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (info->status != MODULE_STATUS_SUSPENDED) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	info->status = MODULE_STATUS_RUNNING;
	const char *name = info->interface->name;

	k_mutex_unlock(&g_module_mgr.lock);

	LOG_INF("Module resumed: %s", name != NULL ? name : "?");
	return 0;
}

/* =============================================================================
 * 事件处理 API 实现 (Event Handling API Implementation)
 * ============================================================================= */

/**
 * @brief 模块订阅事件类型
 * 
 * @param module_id 模块 ID
 * @param event_type 事件类型
 * @return 0 成功，-1 失败
 */
int module_manager_subscribe(uint32_t module_id, event_type_t event_type)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL ||
	    info->interface->on_event == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	/* 检查是否已订阅 */
	if (find_event_sub_index(info, event_type) >= 0) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	/* 检查订阅数量上限 */
	if (info->event_subscription_count >= CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	k_mutex_unlock(&g_module_mgr.lock);

	/* 在锁外调用事件系统订阅 */
	uint32_t subscriber_id = 0U;
	const event_status_t status =
		event_subscribe(event_type, module_event_handler,
				(void *)(uintptr_t)module_id, &subscriber_id);

	if (status != EVENT_OK) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	info = find_module_by_id_locked(module_id);
	if (info == NULL) {
		(void)event_unsubscribe(event_type, subscriber_id);
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	/* 双重检查订阅数量 */
	if (info->event_subscription_count >= CONFIG_MODULE_MAX_EVENT_SUBSCRIPTIONS ||
	    find_event_sub_index(info, event_type) >= 0) {
		(void)event_unsubscribe(event_type, subscriber_id);
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	const uint8_t idx = info->event_subscription_count;

	info->event_subscriptions[idx].type = event_type;
	info->event_subscriptions[idx].subscriber_id = subscriber_id;
	info->event_subscription_count++;

	k_mutex_unlock(&g_module_mgr.lock);
	return 0;
}

/**
 * @brief 模块取消订阅事件类型
 * 
 * @param module_id 模块 ID
 * @param event_type 事件类型
 * @return 0 成功，-1 失败
 */
int module_manager_unsubscribe(uint32_t module_id, event_type_t event_type)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	const int idx = find_event_sub_index(info, event_type);

	if (idx < 0) {
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	const uint32_t sub_id = info->event_subscriptions[idx].subscriber_id;

	(void)event_unsubscribe(event_type, sub_id);

	/* 使用最后一个元素填补空位 */
	const uint8_t last = (uint8_t)(info->event_subscription_count - 1U);

	if ((uint8_t)idx != last) {
		info->event_subscriptions[idx] = info->event_subscriptions[last];
	}

	(void)memset(&info->event_subscriptions[last], 0, sizeof(info->event_subscriptions[last]));
	info->event_subscription_count = last;

	k_mutex_unlock(&g_module_mgr.lock);
	return 0;
}

/**
 * @brief 发送事件到指定模块
 * 
 * @param module_id 模块 ID
 * @param event 事件指针
 * @return 0 成功，-1 失败
 */
int module_manager_send_to_module(uint32_t module_id, const event_t *event)
{
	module_event_handler_t on_ev;
	void *idata;

	if (event == NULL) {
		k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
		g_module_mgr.stats.events_dropped++;
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL) {
		g_module_mgr.stats.events_dropped++;
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	if (info->status != MODULE_STATUS_RUNNING) {
		g_module_mgr.stats.events_dropped++;
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	on_ev = info->interface->on_event;
	idata = info->internal_data;

	if (on_ev == NULL) {
		g_module_mgr.stats.events_dropped++;
		k_mutex_unlock(&g_module_mgr.lock);
		return -1;
	}

	g_module_mgr.stats.events_processed++;
	k_mutex_unlock(&g_module_mgr.lock);

	on_ev(event, idata);
	return 0;
}

/**
 * @brief 广播事件到所有模块
 * 
 * @param event 事件指针
 * @return 接收事件的模块数量
 */
int module_manager_broadcast(const event_t *event)
{
	module_event_handler_t handlers[CONFIG_MAX_MODULES];
	void *datas[CONFIG_MAX_MODULES];
	int n = 0;

	if (event == NULL) {
		return -1;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	/* 收集所有运行中模块的事件处理器 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		module_info_t *info = &g_module_mgr.modules[i];

		if (info->status == MODULE_STATUS_RUNNING && info->interface != NULL &&
		    info->interface->on_event != NULL) {
			handlers[n] = info->interface->on_event;
			datas[n] = info->internal_data;
			n++;
		}
	}

	g_module_mgr.stats.events_processed += (uint32_t)n;

	k_mutex_unlock(&g_module_mgr.lock);

	/* 在锁外调用所有处理器 */
	for (int i = 0; i < n; i++) {
		handlers[i](event, datas[i]);
	}

	return n;
}

/* =============================================================================
 * 统计与调试 API 实现 (Statistics & Debug API Implementation)
 * ============================================================================= */

/**
 * @brief 获取模块管理器统计信息
 * 
 * @param stats 输出：统计信息结构
 */
void module_manager_get_stats(module_mgr_stats_t *stats)
{
	if (stats == NULL) {
		return;
	}

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	*stats = g_module_mgr.stats;
	k_mutex_unlock(&g_module_mgr.lock);
}

/**
 * @brief 重置模块管理器统计信息
 */
void module_manager_reset_stats(void)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	(void)memset(&g_module_mgr.stats, 0, sizeof(g_module_mgr.stats));
	k_mutex_unlock(&g_module_mgr.lock);
}

/**
 * @brief 打印模块信息到控制台
 */
void module_manager_dump_info(void)
{
	module_info_t snap[CONFIG_MAX_MODULES];
	uint32_t mod_count;
	uint32_t active;
	uint32_t errors;
	int n = 0;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	mod_count = g_module_mgr.module_count;
	active = g_module_mgr.stats.active_modules;
	errors = g_module_mgr.stats.error_modules;

	/* 创建模块快照 */
	for (int i = 0; i < CONFIG_MAX_MODULES; i++) {
		if (g_module_mgr.modules[i].status != MODULE_STATUS_UNINITIALIZED) {
			snap[n++] = g_module_mgr.modules[i];
		}
	}

	k_mutex_unlock(&g_module_mgr.lock);

	/* 打印模块信息 */
	printk("\n=== Module Manager Info ===\n");
	printk("Total modules: %u / %d\n", (unsigned int)mod_count, CONFIG_MAX_MODULES);
	printk("Active: %u, Errors: %u\n\n", (unsigned int)active, (unsigned int)errors);

	for (int i = 0; i < n; i++) {
		module_info_t *info = &snap[i];
		const char *status_str;

		switch (info->status) {
		case MODULE_STATUS_INITIALIZING:
			status_str = "INITING";
			break;
		case MODULE_STATUS_INITIALIZED:
			status_str = "INIT";
			break;
		case MODULE_STATUS_RUNNING:
			status_str = "RUNNING";
			break;
		case MODULE_STATUS_STOPPED:
			status_str = "STOPPED";
			break;
		case MODULE_STATUS_ERROR:
			status_str = "ERROR";
			break;
		case MODULE_STATUS_SUSPENDED:
			status_str = "SUSPENDED";
			break;
		default:
			status_str = "UNKNOWN";
			break;
		}

		printk("  [%u] %s - %s (v%u.%u.%u)\n", (unsigned int)info->id,
		       info->interface != NULL && info->interface->name != NULL ? info->interface->name
									      : "N/A",
		       status_str,
		       info->interface != NULL ? MODULE_VERSION_MAJOR(info->interface->version) : 0,
		       info->interface != NULL ? MODULE_VERSION_MINOR(info->interface->version) : 0,
		       info->interface != NULL ? MODULE_VERSION_PATCH(info->interface->version) : 0);
	}

	printk("\n");
}

/**
 * @brief 注册模块事件回调
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void module_manager_set_callback(module_mgr_callback_t callback, void *user_data)
{
	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);
	g_module_mgr.callback = callback;
	g_module_mgr.callback_user_data = user_data;
	k_mutex_unlock(&g_module_mgr.lock);
}

/* =============================================================================
 * 内部函数 (Internal Functions)
 * ============================================================================= */

/**
 * @brief 模块事件处理函数
 * 
 * 这是事件系统回调到模块管理器的入口函数。
 * 
 * @param event 事件指针
 * @param user_data 用户数据（模块 ID）
 */
static void module_event_handler(const event_t *event, void *user_data)
{
	if (event == NULL || user_data == NULL) {
		return;
	}

	const uint32_t module_id = (uint32_t)(uintptr_t)user_data;

	k_mutex_lock(&g_module_mgr.lock, K_FOREVER);

	module_info_t *info = find_module_by_id_locked(module_id);

	if (info == NULL || info->interface == NULL ||
	    info->status != MODULE_STATUS_RUNNING) {
		k_mutex_unlock(&g_module_mgr.lock);
		return;
	}

	module_event_handler_t handler = info->interface->on_event;
	void *idata = info->internal_data;

	g_module_mgr.stats.events_processed++;
	k_mutex_unlock(&g_module_mgr.lock);

	if (handler != NULL) {
		handler(event, idata);
	}
}
